// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class OllamaClient;
class QQmlEngine;
class QJSEngine;

// Ollama and the models Ask uses, for Settings and the Ask page: where Ollama is, which models answer and embed, whether
// it is reachable and has them, and whether it keeps both models loaded. One instance, created in main().
class ModelSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Models)
    QML_SINGLETON
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString answerModel READ answerModel WRITE setAnswerModel NOTIFY settingsChanged)
    Q_PROPERTY(QString embeddingModel READ embeddingModel WRITE setEmbeddingModel NOTIFY settingsChanged)
    Q_PROPERTY(QString defaultAnswerModel READ defaultAnswerModel CONSTANT)
    Q_PROPERTY(QString defaultEmbeddingModel READ defaultEmbeddingModel CONSTANT)
    Q_PROPERTY(bool checking READ checking NOTIFY statusChanged)
    Q_PROPERTY(bool checked READ checked NOTIFY statusChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QStringList installed READ installed NOTIFY statusChanged)
    Q_PROPERTY(QStringList loaded READ loaded NOTIFY statusChanged)
    Q_PROPERTY(bool answerModelInstalled READ answerModelInstalled NOTIFY statusChanged)
    Q_PROPERTY(bool embeddingModelInstalled READ embeddingModelInstalled NOTIFY statusChanged)
    Q_PROPERTY(bool swapping READ swapping NOTIFY statusChanged)

public:
    static ModelSettings *createInstance(QObject *parent);
    static ModelSettings *instance();
    static ModelSettings *create(QQmlEngine *, QJSEngine *);

    OllamaClient *client() const
    {
        return m_client;
    }

    QString url() const;
    void setUrl(const QString &url);
    QString answerModel() const;
    void setAnswerModel(const QString &model);
    QString embeddingModel() const;
    void setEmbeddingModel(const QString &model);
    QString defaultAnswerModel() const;
    QString defaultEmbeddingModel() const;

    bool checking() const
    {
        return m_checking;
    }
    bool checked() const
    {
        return m_checked;
    }
    bool reachable() const
    {
        return m_reachable;
    }
    QString error() const
    {
        return m_error;
    }
    QStringList installed() const
    {
        return m_installed;
    }
    QStringList loaded() const
    {
        return m_loaded;
    }
    bool answerModelInstalled() const;
    bool embeddingModelInstalled() const;
    bool swapping() const
    {
        return m_swapping;
    }

    // Asks Ollama what it has installed and loaded
    Q_INVOKABLE void refresh();
    // After an answer: if the answer model is no longer loaded, Ollama is swapping models for every question
    void checkSwapping();

Q_SIGNALS:
    void settingsChanged();
    void statusChanged();

private:
    explicit ModelSettings(QObject *parent);
    bool has(const QString &model) const;

    OllamaClient *m_client;
    bool m_checking = false;
    quint64 m_refresh = 0; // a newer refresh (another Ollama address) makes an older one's replies stale
    bool m_checked = false;
    bool m_reachable = false;
    QString m_error;
    QStringList m_installed;
    QStringList m_loaded;
    bool m_swapping = false;
};
