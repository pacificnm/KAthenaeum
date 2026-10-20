// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentmodel.h"

#include "core/settings.h"

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
    if (m_dirty)
        save(); // leaving the editor keeps what was typed, including after an earlier save failed
}

void DocumentModel::setDocumentId(qint64 id)
{
    if (id == m_id)
        return;
    if (m_dirty)
        save();
    m_id = id;
    const auto d = Projects::document(id);
    m_exists = bool(d);
    m_projectId = d ? d->projectId : 0;
    m_title = d ? d->title : QString();
    m_content = d ? d->content : QString();
    m_error.clear();
    m_dirty = false;
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
    m_dirty = true;
    m_timer.start();
    Q_EMIT savedChanged();
}

void DocumentModel::save()
{
    m_timer.stop();
    if (!m_exists)
        return;
    // a failed save must not look saved (Athenaeum: "failed note saves looked saved")
    const bool ok = Projects::saveDocument(m_id, m_title, m_content);
    m_dirty = !ok;
    m_error = ok ? QString() : i18n("This document could not be saved. It will be tried again; you can also export it.");
    Q_EMIT savedChanged();
    // only the project's page needs to know (every list and the article cache were refreshed on each pause in typing)
    Q_EMIT ProjectsNotifier::instance()->documentSaved(m_projectId);
}

QString DocumentModel::exportTo(const QUrl &file) const
{
    return writeTextFile(
        file,
        QStringLiteral("# %1\n\n%2").arg(m_title, Projects::exportMarkdown(m_content, Citations::style(AppSettings::citationStyle())).trimmed())
            + QLatin1Char('\n'));
}

QString DocumentModel::exportName() const
{
    return Projects::slug(m_title) + QStringLiteral(".md");
}
