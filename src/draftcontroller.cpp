// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "draftcontroller.h"

#include "core/drafts.h"
#include "modelsettings.h"
#include "core/library.h"
#include "projectlist.h"

#include <KLocalizedString>

DraftController::DraftController(QObject *parent)
    : QObject(parent)
    , m_drafter(new Drafter(ModelSettings::instance()->client(), this))
{
    connect(m_drafter, &Drafter::progress, this, [this](int step, int total, const QString &label) {
        m_step = step;
        m_total = total;
        m_label = label;
        Q_EMIT stateChanged();
    });
    connect(m_drafter, &Drafter::textAdded, this, [this](const QString &text) {
        m_streamed += text;
        Q_EMIT stateChanged();
    });
    connect(m_drafter, &Drafter::finished, this, [this](const QString &status, const QString &title, const QString &markdown, const QString &error) {
        m_label.clear();
        m_streamed.clear();
        Q_EMIT stateChanged();
        if (status == QLatin1String("error")) {
            Q_EMIT failed(error);
        } else if (status == QLatin1String("done")) {
            const qint64 id = Projects::createDocument(m_projectId, title, markdown);
            Q_EMIT ProjectsNotifier::instance()->changed();
            if (id > 0)
                Q_EMIT documentReady(id, m_projectId, title);
            else
                Q_EMIT failed(i18n("The draft could not be saved."));
        }
    });
}

bool DraftController::running() const
{
    return m_drafter->running();
}

void DraftController::summarizeProject(qint64 projectId)
{
    m_projectId = projectId;
    m_streamed.clear();
    ModelSettings *m = ModelSettings::instance();
    m_drafter->summarizeProject(projectId, {m->answerModel(), m->embeddingModel()});
    Q_EMIT stateChanged();
}

void DraftController::summarizeChat(qint64 projectId, qint64 chatId)
{
    m_projectId = projectId;
    m_streamed.clear();
    ModelSettings *m = ModelSettings::instance();
    m_drafter->summarizeChat(chatId, {m->answerModel(), m->embeddingModel()});
    Q_EMIT stateChanged();
}

void DraftController::writeReport(qint64 projectId, const QString &title, const QString &instructions, bool full, bool alsoLibrary)
{
    m_projectId = projectId;
    m_streamed.clear();
    Drafts::ReportRequest request;
    request.title = title;
    request.instructions = instructions;
    request.sections = full ? 6 : 3;
    request.words = full ? 300 : 180;
    request.alsoLibrary = alsoLibrary;
    QList<qint64> libraries;
    for (const auto rows = Library::all(); const LibraryRecord &l : rows)
        if (l.fileExists())
            libraries << l.id;
    ModelSettings *m = ModelSettings::instance();
    m_drafter->writeReport(projectId, request, libraries, {m->answerModel(), m->embeddingModel()});
    Q_EMIT stateChanged();
}

void DraftController::stop()
{
    m_drafter->stop();
}
