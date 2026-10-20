// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "embedding.h"

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QStringList>

#include <functional>
#include <optional>

// Ollama's HTTP API, the parts the app uses. Parsing is separate so it can be tested without a server.
namespace Ollama
{
// One line of a streamed /api/chat reply
struct ChatLine {
    QString text;
    bool done = false;
    QString error;
    QJsonObject json; // the whole line: the last one carries eval_count, eval_duration, done_reason
};
ChatLine parseChatLine(const QByteArray &line);

// /api/embed's {"embeddings": [[...], ...]}; nullopt (and `error`) when it is not that
std::optional<QList<Vector>> parseEmbeddings(const QByteArray &body, QString *error);

// What to tell the user when a request failed: Ollama not running, a model not installed, or Ollama's own message
QString errorMessage(QNetworkReply::NetworkError error, int httpStatus, const QByteArray &body, const QString &baseUrl, const QString &model = {});
}

class OllamaClient : public QObject
{
    Q_OBJECT

public:
    explicit OllamaClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    QString baseUrl() const
    {
        return m_baseUrl;
    }

    // Callbacks run on this object's thread, only while `context` lives, and never for a request that was aborted.
    // Every call returns its reply so the caller can abort() it.
    using EmbedDone = std::function<void(const QList<Vector> &vectors, const QString &error)>;
    QNetworkReply *embed(const QString &model, const QStringList &input, QObject *context, EmbedDone done);

    // A streamed /api/chat. `body` gets "stream": true added.
    using ChatText = std::function<void(const QString &text)>;
    using ChatDone = std::function<void(const QJsonObject &last, const QString &error)>;
    QNetworkReply *chat(const QJsonObject &body, QObject *context, ChatText text, ChatDone done);

    // The model's capabilities ("completion", "thinking", "embedding", ...), from /api/show; cached per model
    using Capabilities = std::function<void(const QStringList &capabilities, const QString &error)>;
    void capabilities(const QString &model, QObject *context, Capabilities done);

    // Installed models (/api/tags) and models loaded in memory now (/api/ps)
    using Names = std::function<void(const QStringList &names, const QString &error)>;
    QNetworkReply *installed(QObject *context, Names done);
    QNetworkReply *loaded(QObject *context, Names done);

    // Stops a request without calling its callback. (A timeout also shows up as "operation cancelled", and that one is
    // reported.)
    static void cancel(QNetworkReply *reply);

private:
    QNetworkRequest request(const QString &path, int timeoutMs) const;
    QNetworkReply *owned(QNetworkReply *reply);
    QNetworkReply *post(const QString &path, const QJsonObject &body, int timeoutMs);
    QNetworkReply *names(const QString &path, const QString &listKey, QObject *context, Names done);

    QNetworkAccessManager m_nam;
    QString m_baseUrl = QStringLiteral("http://localhost:11434");
    QHash<QString, QStringList> m_capabilities;
};
