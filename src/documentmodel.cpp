// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentmodel.h"

#include "core/projects.h"
#include "projectlist.h"
#include "projectmodel.h"

#include <KLocalizedString>

DocumentModel::DocumentModel(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(900);
    connect(&m_timer, &QTimer::timeout, this, &DocumentModel::save);
}

DocumentModel::~DocumentModel()
{
    if (m_timer.isActive())
        save();  // leaving the editor keeps what was typed
}

void DocumentModel::setDocumentId(qint64 id)
{
    if (id == m_id)
        return;
    if (m_timer.isActive())
        save();
    m_id = id;
    const auto d = Projects::document(id);
    m_exists = bool(d);
    m_projectId = d ? d->projectId : 0;
    m_title = d ? d->title : QString();
    m_content = d ? d->content : QString();
    m_error.clear();
    Q_EMIT loaded();
    Q_EMIT titleChanged();
    Q_EMIT contentChanged();
    Q_EMIT savedChanged();
}

void DocumentModel::setTitle(const QString &title)
{
    if (title == m_title)
        return;
    m_title = title;
    Q_EMIT titleChanged();
    changedByUser();
}

void DocumentModel::setContent(const QString &content)
{
    if (content == m_content)
        return;
    m_content = content;
    Q_EMIT contentChanged();
    changedByUser();
}

void DocumentModel::changedByUser()
{
    if (!m_exists)
        return;
    m_timer.start();
    Q_EMIT savedChanged();
}

void DocumentModel::save()
{
    m_timer.stop();
    if (!m_exists)
        return;
    // a failed save must not look saved (Athenaeum: "failed note saves looked saved")
    m_error = Projects::saveDocument(m_id, m_title, m_content) ? QString() : i18n("This document could not be saved.");
    Q_EMIT savedChanged();
    Q_EMIT ProjectsNotifier::instance()->changed();
}

QString DocumentModel::exportTo(const QUrl &file) const
{
    return writeTextFile(file, QStringLiteral("# %1\n\n%2").arg(m_title, Projects::exportMarkdown(m_content).trimmed()) + QLatin1Char('\n'));
}

QString DocumentModel::exportName() const
{
    return Projects::slug(m_title) + QStringLiteral(".md");
}
