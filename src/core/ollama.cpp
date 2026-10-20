// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

namespace
{
constexpr qsizetype MaxLineBytes = 1 << 20; // a streamed line is one JSON object of a few hundred bytes
constexpr int ShortTimeout = 10'000;
constexpr int EmbedTimeout = 120'000; // a model loads from disk on first use
constexpr int ChatTimeout = 300'000; // without a byte: loading the model and reading the prompt come first

bool cancelled(QNetworkReply *reply)
{
    return reply->error() == QNetworkReply::OperationCanceledError;
}

int status(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}
}

Ollama::ChatLine Ollama::parseChatLine(const QByteArray &line)
{
    ChatLine out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (!doc.isObject()) {
        out.error = i18n("Unreadable reply from Ollama: %1", QString::fromUtf8(line.left(200)));
        return out;
    }
    out.json = doc.object();
    out.error = out.json.value(QLatin1String("error")).toString();
    out.text = out.json.value(QLatin1String("message")).toObject().value(QLatin1String("content")).toString();
    out.done = out.json.value(QLatin1String("done")).toBool();
    return out;
}

std::optional<QList<Vector>> Ollama::parseEmbeddings(const QByteArray &body, QString *error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonValue list = doc.object().value(QLatin1String("embeddings"));
    if (!list.isArray()) {
        if (error)
            *error = doc.object().value(QLatin1String("error")).toString(i18n("Unreadable reply from Ollama"));
        return std::nullopt;
    }
    QList<Vector> out;
    for (const auto array = list.toArray(); const QJsonValue &row : array) {
        Vector v;
        const QJsonArray a = row.toArray();
        v.reserve(a.size());
        for (const QJsonValue &x : a)
            v.append(float(x.toDouble()));
        out.append(v);
    }
    return out;
}

QString Ollama::errorMessage(QNetworkReply::NetworkError error, int httpStatus, const QByteArray &body, const QString &baseUrl, const QString &model)
{
    const QString said = QJsonDocument::fromJson(body).object().value(QLatin1String("error")).toString();
    if (httpStatus == 404 && !model.isEmpty() && (said.contains(QLatin1String("not found")) || said.isEmpty()))
        return i18n("The model “%1” is not installed in Ollama. Install it with: ollama pull %1", model);
    if (!said.isEmpty())
        return i18nc("@info %1 is an error message from the Ollama server", "Ollama: %1", said);
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::RemoteHostClosedError:
        return i18n("Could not reach Ollama at %1. Is it running? (systemctl status ollama)", baseUrl);
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TimeoutError:
        return i18n("Ollama at %1 did not answer in time.", baseUrl);
    default:
        break;
    }
    if (httpStatus >= 400)
        return i18n("Ollama returned HTTP %1.", httpStatus);
    return i18n("Could not talk to Ollama at %1.", baseUrl);
}

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent)
{
}

void OllamaClient::setBaseUrl(const QString &url)
{
    QString u = url.trimmed();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    if (u != m_baseUrl)
        m_capabilities.clear();
    m_baseUrl = u;
}

QNetworkRequest OllamaClient::request(const QString &path, int timeoutMs) const
{
    QNetworkRequest r(QUrl(m_baseUrl + path));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    r.setTransferTimeout(timeoutMs);
    return r;
}

QNetworkReply *OllamaClient::post(const QString &path, const QJsonObject &body, int timeoutMs)
{
    return owned(m_nam.post(request(path, timeoutMs), QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

QNetworkReply *OllamaClient::owned(QNetworkReply *reply)
{
    // freed when it finishes whatever happens to the object that asked: its own handlers are connected with that object
    // as context, and never run once it is gone, so the reply was kept until the client went
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    return reply;
}

QNetworkReply *OllamaClient::embed(const QString &model, const QStringList &input, QObject *context, EmbedDone done)
{
    QNetworkReply *reply =
        post(QStringLiteral("/api/embed"), {{QStringLiteral("model"), model}, {QStringLiteral("input"), QJsonArray::fromStringList(input)}}, EmbedTimeout);
    const QString base = m_baseUrl;
    connect(reply, &QNetworkReply::finished, context, [reply, done, base, model, n = input.size()] {
        reply->deleteLater();
        if (cancelled(reply) && reply->property("aborted").toBool())
            return;
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, Ollama::errorMessage(reply->error(), status(reply), body, base, model));
            return;
        }
        QString error;
        const auto vectors = Ollama::parseEmbeddings(body, &error);
        if (!vectors || vectors->size() != n)
            done({}, vectors ? i18np("Ollama returned %2 embeddings for 1 text", "Ollama returned %2 embeddings for %1 texts", n, vectors->size()) : error);
        else
            done(*vectors, {});
    });
    return reply;
}

QNetworkReply *OllamaClient::chat(const QJsonObject &body, QObject *context, ChatText text, ChatDone done)
{
    QJsonObject b = body;
    b.insert(QStringLiteral("stream"), true);
    QNetworkReply *reply = post(QStringLiteral("/api/chat"), b, ChatTimeout);
    auto buffer = std::make_shared<QByteArray>();
    auto last = std::make_shared<QJsonObject>();
    auto failed = std::make_shared<QString>();
    // Lines are handled as they arrive; an HTTP error's body is only read at the end
    auto drain = [reply, buffer, last, failed, text](bool all) {
        if (status(reply) != 200 || !failed->isEmpty())
            return;
        *buffer += reply->readAll();
        // a line is one JSON object of a few hundred bytes: a megabyte without a line break is not Ollama talking
        if (buffer->size() > MaxLineBytes && buffer->indexOf('\n') < 0) {
            *failed = i18n("The model server sent a malformed reply");
            reply->abort();
            return;
        }
        qsizetype nl;
        while ((nl = buffer->indexOf('\n')) >= 0 || (all && !buffer->trimmed().isEmpty())) {
            const QByteArray line = nl >= 0 ? buffer->left(nl) : *buffer;
            buffer->remove(0, nl >= 0 ? nl + 1 : buffer->size());
            if (line.trimmed().isEmpty())
                continue;
            const Ollama::ChatLine l = Ollama::parseChatLine(line);
            if (!l.error.isEmpty()) {
                *failed = l.error;
                return;
            }
            if (!l.text.isEmpty())
                text(l.text);
            if (l.done)
                *last = l.json;
        }
    };
    connect(reply, &QNetworkReply::readyRead, context, [drain] {
        drain(false);
    });
    const QString base = m_baseUrl;
    const QString model = body.value(QLatin1String("model")).toString();
    connect(reply, &QNetworkReply::finished, context, [reply, drain, last, failed, done, base, model] {
        reply->deleteLater();
        if (cancelled(reply) && reply->property("aborted").toBool())
            return;
        if (reply->error() != QNetworkReply::NoError || status(reply) != 200) {
            done({}, Ollama::errorMessage(reply->error(), status(reply), reply->readAll(), base, model));
            return;
        }
        drain(true);
        if (!failed->isEmpty())
            done({}, i18nc("@info %1 is an error message from the Ollama server", "Ollama: %1", *failed));
        else if (last->isEmpty())
            done({}, i18n("Ollama ended the answer without finishing it."));
        else
            done(*last, {});
    });
    return reply;
}

void OllamaClient::capabilities(const QString &model, QObject *context, Capabilities done)
{
    if (const auto it = m_capabilities.constFind(model); it != m_capabilities.cend()) {
        done(*it, {});
        return;
    }
    QNetworkReply *reply = post(QStringLiteral("/api/show"), {{QStringLiteral("model"), model}}, ShortTimeout);
    const QString base = m_baseUrl;
    QPointer<OllamaClient> self(this);
    connect(reply, &QNetworkReply::finished, context, [self, reply, done, base, model] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, Ollama::errorMessage(reply->error(), status(reply), body, base, model));
            return;
        }
        QStringList caps;
        for (const auto array = QJsonDocument::fromJson(body).object().value(QLatin1String("capabilities")).toArray(); const QJsonValue &c : array)
            caps << c.toString();
        if (self)
            self->m_capabilities.insert(model, caps);
        done(caps, {});
    });
}

QNetworkReply *OllamaClient::names(const QString &path, const QString &listKey, QObject *context, Names done)
{
    QNetworkReply *reply = owned(m_nam.get(request(path, ShortTimeout)));
    const QString base = m_baseUrl;
    connect(reply, &QNetworkReply::finished, context, [reply, done, base, listKey] {
        reply->deleteLater();
        if (cancelled(reply) && reply->property("aborted").toBool())
            return;
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, Ollama::errorMessage(reply->error(), status(reply), body, base));
            return;
        }
        QStringList out;
        for (const auto array = QJsonDocument::fromJson(body).object().value(listKey).toArray(); const QJsonValue &m : array)
            out << m.toObject().value(QLatin1String("name")).toString();
        done(out, {});
    });
    return reply;
}

void OllamaClient::cancel(QNetworkReply *reply)
{
    if (!reply)
        return;
    reply->setProperty("aborted", true);
    reply->abort();
}

QNetworkReply *OllamaClient::installed(QObject *context, Names done)
{
    return names(QStringLiteral("/api/tags"), QStringLiteral("models"), context, done);
}

QNetworkReply *OllamaClient::loaded(QObject *context, Names done)
{
    return names(QStringLiteral("/api/ps"), QStringLiteral("models"), context, done);
}
