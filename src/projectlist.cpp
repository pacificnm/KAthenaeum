// SPDX-License-Identifier: GPL-3.0-or-later
#include "projectlist.h"

ProjectsNotifier *ProjectsNotifier::instance()
{
    static ProjectsNotifier notifier;
    return &notifier;
}

ProjectList::ProjectList(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(ProjectsNotifier::instance(), &ProjectsNotifier::changed, this, &ProjectList::refresh);
    refresh();
}

int ProjectList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_projects.size());
}

QHash<int, QByteArray> ProjectList::roleNames() const
{
    return {{ProjectIdRole, "projectId"}, {TitleRole, "title"},       {DescriptionRole, "description"}, {UpdatedRole, "updated"},
            {ArticlesRole, "articles"},   {ClipsRole, "excerpts"},    {ChatsRole, "chats"},             {DocumentsRole, "documents"}};
}

QVariant ProjectList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_projects.size())
        return {};
    const ProjectRecord &p = m_projects.at(index.row());
    switch (role) {
    case ProjectIdRole:
        return p.id;
    case TitleRole:
        return p.title;
    case DescriptionRole:
        return p.description;
    case UpdatedRole:
        return p.updatedAt.toLocalTime();
    case ArticlesRole:
        return p.articles;
    case ClipsRole:
        return p.clips;
    case ChatsRole:
        return p.chats;
    case DocumentsRole:
        return p.documents;
    }
    return {};
}

void ProjectList::refresh()
{
    beginResetModel();
    m_projects = Projects::all();
    endResetModel();
    Q_EMIT countChanged();
}

qint64 ProjectList::create(const QString &title, const QString &description)
{
    const qint64 id = Projects::create(title, description);
    if (id)
        Q_EMIT ProjectsNotifier::instance()->changed();
    return id;
}

bool ProjectList::remove(qint64 projectId)
{
    const bool ok = Projects::remove(projectId);
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

QVariantList ProjectList::containing(qint64 libraryId, const QString &path) const
{
    QVariantList out;
    for (qint64 id : Projects::containing(libraryId, path))
        out << id;
    return out;
}

bool ProjectList::addArticle(qint64 projectId, qint64 libraryId, const QString &path, const QString &title, const QString &note)
{
    const bool ok = Projects::addArticle(projectId, {libraryId, path, title}, note) > 0;
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

bool ProjectList::addExcerpt(qint64 projectId, const QString &text, qint64 libraryId, const QString &path, const QString &title,
                             const QString &section, const QString &note)
{
    const QList<ArticleRef> refs = libraryId > 0 ? QList<ArticleRef>{{libraryId, path, title}} : QList<ArticleRef>{};
    const bool ok = Projects::addClip(projectId, text, refs, section, note) > 0;
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

bool ProjectList::addAnswer(qint64 projectId, const QString &text, const QVariantList &sources, const QString &note)
{
    QList<ArticleRef> refs;
    for (const QVariant &v : sources) {
        const QVariantMap s = v.toMap();
        const ArticleRef r{s.value(QStringLiteral("libraryId")).toLongLong(), s.value(QStringLiteral("path")).toString(),
                           s.value(QStringLiteral("title")).toString()};
        const bool seen = std::any_of(refs.cbegin(), refs.cend(), [&r](const ArticleRef &o) { return o.libraryId == r.libraryId && o.path == r.path; });
        if (r.libraryId > 0 && !r.path.isEmpty() && !seen)
            refs << r;
    }
    const bool ok = Projects::addClip(projectId, text, refs, {}, note) > 0;
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

bool ProjectList::attachChat(qint64 projectId, qint64 chatId)
{
    const bool ok = Projects::attachChat(projectId, chatId) > 0;
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

QString ProjectList::title(qint64 projectId) const
{
    const auto p = Projects::byId(projectId);
    return p ? p->title : QString();
}

QVariantList ProjectList::list() const
{
    QVariantList out;
    for (const ProjectRecord &p : m_projects)
        out << QVariantMap{{QStringLiteral("projectId"), p.id}, {QStringLiteral("title"), p.title}};
    return out;
}
