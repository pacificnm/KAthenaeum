// SPDX-License-Identifier: GPL-3.0-or-later
#include "modelsettings.h"

#include "core/embedding.h"
#include "core/ollama.h"
#include "core/settings.h"

#include <QQmlEngine>

namespace
{
ModelSettings *s_instance = nullptr;
}

ModelSettings *ModelSettings::createInstance(QObject *parent)
{
    Q_ASSERT(!s_instance);
    s_instance = new ModelSettings(parent);
    return s_instance;
}

ModelSettings *ModelSettings::instance()
{
    return s_instance;
}

ModelSettings *ModelSettings::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

ModelSettings::ModelSettings(QObject *parent)
    : QObject(parent)
    , m_client(new OllamaClient(this))
{
    m_client->setBaseUrl(url());
}

QString ModelSettings::url() const
{
    return AppSettings::ollamaUrl();
}

void ModelSettings::setUrl(const QString &url)
{
    QString u = url.trimmed();
    if (!u.contains(QLatin1String("://")))
        u.prepend(QLatin1String("http://"));
    if (u == this->url())
        return;
    AppSettings::setValue(QStringLiteral("ollama.url"), u);
    m_client->setBaseUrl(u);
    Q_EMIT settingsChanged();
    refresh();
}

QString ModelSettings::answerModel() const
{
    return AppSettings::answerModel();
}

void ModelSettings::setAnswerModel(const QString &model)
{
    if (model.trimmed().isEmpty() || model.trimmed() == answerModel())
        return;
    AppSettings::setValue(QStringLiteral("models.answer"), model.trimmed());
    m_swapping = false;
    Q_EMIT settingsChanged();
    Q_EMIT statusChanged();
}

QString ModelSettings::embeddingModel() const
{
    return AppSettings::embeddingModel();
}

void ModelSettings::setEmbeddingModel(const QString &model)
{
    if (model.trimmed().isEmpty() || model.trimmed() == embeddingModel())
        return;
    AppSettings::setValue(QStringLiteral("models.embedding"), model.trimmed());
    m_swapping = false;
    Q_EMIT settingsChanged();
    Q_EMIT statusChanged();
}

QString ModelSettings::defaultAnswerModel() const
{
    return QStringLiteral("qwen3.5:2b");
}

QString ModelSettings::defaultEmbeddingModel() const
{
    return QStringLiteral("nomic-embed-text");
}

bool ModelSettings::has(const QString &model) const
{
    const QString want = Embedding::normalizeName(model);
    for (const QString &m : m_installed)
        if (Embedding::normalizeName(m) == want)
            return true;
    return false;
}

bool ModelSettings::answerModelInstalled() const
{
    return has(answerModel());
}

bool ModelSettings::embeddingModelInstalled() const
{
    return has(embeddingModel());
}

void ModelSettings::refresh()
{
    m_checking = true;
    const quint64 refresh = ++m_refresh;
    Q_EMIT statusChanged();
    m_client->installed(this, [this, refresh](const QStringList &names, const QString &error) {
        if (refresh != m_refresh)
            return;
        m_reachable = error.isEmpty();
        m_error = error;
        m_installed = names;
        m_installed.sort();
        m_client->loaded(this, [this, refresh](const QStringList &loaded, const QString &) {
            if (refresh != m_refresh)
                return;
            m_loaded = loaded;
            m_checking = false;
            m_checked = true;
            Q_EMIT statusChanged();
        });
    });
}

void ModelSettings::checkSwapping()
{
    m_client->loaded(this, [this](const QStringList &loaded, const QString &error) {
        if (!error.isEmpty())
            return;
        m_loaded = loaded;
        bool answer = false, embedding = false;
        for (const QString &m : loaded) {
            answer = answer || Embedding::normalizeName(m) == Embedding::normalizeName(answerModel());
            embedding = embedding || Embedding::normalizeName(m) == Embedding::normalizeName(embeddingModel());
        }
        // Right after an answer the embedding model has just checked the citations. If the answer model is gone, Ollama
        // made room for it: every question then loads both again (about 2 s on this laptop, measured)
        m_swapping = embedding && !answer;
        Q_EMIT statusChanged();
    });
}
