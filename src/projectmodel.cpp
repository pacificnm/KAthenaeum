// SPDX-License-Identifier: GPL-3.0-or-later
#include "projectmodel.h"

#include "core/chats.h"
#include "core/library.h"
#include "core/projects.h"
#include "projectlist.h"

#include <KLocalizedString>

#include <QHash>
#include <QSaveFile>

QString writeTextFile(const QUrl &file, const QString &text)
{
    if (!file.isLocalFile())
        return i18n("Only files on this computer can be written.");
    QSaveFile f(file.toLocalFile());
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit())
        return i18n("Could not write %1: %2", file.toLocalFile(), f.errorString());
    return {};
}

ProjectModel::ProjectModel(QObject *parent)
    : QObject(parent)
{
    connect(ProjectsNotifier::instance(), &ProjectsNotifier::changed, this, &ProjectModel::refresh);
}

void ProjectModel::setProjectId(qint64 id)
{
    if (id == m_id)
        return;
    m_id = id;
    refresh();
}

void ProjectModel::refresh()
{
    m_articles.clear();
    m_excerpts.clear();
    m_chats.clear();
    m_documents.clear();
    m_items.clear();
    const auto p = m_id > 0 ? Projects::byId(m_id) : std::nullopt;
    m_exists = bool(p);
    m_title = p ? p->title : QString();
    m_description = p ? p->description : QString();
    if (p) {
        QHash<qint64, std::optional<LibraryRecord>> libraries;
        auto library = [&libraries](qint64 id) {
            if (!libraries.contains(id))
                libraries.insert(id, Library::byId(id));
            return libraries.value(id);
        };
        m_items = Projects::items(m_id);
        for (const ProjectItem &i : std::as_const(m_items)) {
            const auto lib = i.article.libraryId > 0 ? library(i.article.libraryId) : std::nullopt;
            const bool available = lib && lib->fileExists();
            if (i.kind == QLatin1String("article"))
                m_articles << QVariantMap{{QStringLiteral("itemId"), i.id},
                                          {QStringLiteral("libraryId"), i.article.libraryId},
                                          {QStringLiteral("path"), i.article.path},
                                          {QStringLiteral("title"), i.article.title},
                                          {QStringLiteral("libraryTitle"), lib ? lib->title : QString()},
                                          {QStringLiteral("note"), i.note},
                                          {QStringLiteral("available"), available}};
            else if (i.kind == QLatin1String("clip"))
                m_excerpts << QVariantMap{{QStringLiteral("itemId"), i.id},
                                          {QStringLiteral("text"), i.text},
                                          {QStringLiteral("title"), i.refs.value(0).title},
                                          {QStringLiteral("sources"), int(i.refs.size())},
                                          {QStringLiteral("section"), i.section},
                                          {QStringLiteral("libraryId"), i.article.libraryId},
                                          {QStringLiteral("path"), i.article.path},
                                          {QStringLiteral("note"), i.note},
                                          {QStringLiteral("available"), available}};
            else if (i.kind == QLatin1String("chat")) {
                const auto chat = Chats::byId(i.chatId);
                m_chats << QVariantMap{{QStringLiteral("chatId"), i.chatId},
                                       {QStringLiteral("title"), i.chatTitle},
                                       {QStringLiteral("updated"), chat ? chat->updatedAt.toLocalTime() : QDateTime()},
                                       {QStringLiteral("questions"), chat ? chat->questions : 0},
                                       {QStringLiteral("articleLibraryId"), chat ? chat->articleLibraryId : 0},
                                       {QStringLiteral("articlePath"), chat ? chat->articlePath : QString()},
                                       {QStringLiteral("itemId"), i.id}};
            }
        }
        for (const ChatRecord &c : Chats::inProject(m_id))
            m_chats << QVariantMap{{QStringLiteral("chatId"), c.id},
                                   {QStringLiteral("title"), c.title},
                                   {QStringLiteral("updated"), c.updatedAt.toLocalTime()},
                                   {QStringLiteral("questions"), c.questions},
                                   {QStringLiteral("articleLibraryId"), 0},
                                   {QStringLiteral("articlePath"), QString()},
                                   {QStringLiteral("itemId"), 0}};
        std::stable_sort(m_chats.begin(), m_chats.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("updated")).toDateTime() > b.toMap().value(QStringLiteral("updated")).toDateTime();
        });
        for (const DocumentRecord &d : Projects::documents(m_id))
            m_documents << QVariantMap{{QStringLiteral("documentId"), d.id},
                                       {QStringLiteral("title"), d.title},
                                       {QStringLiteral("updated"), d.updatedAt.toLocalTime()},
                                       {QStringLiteral("characters"), int(d.content.size())}};
    }
    Q_EMIT changed();
}

bool ProjectModel::update(const QString &title, const QString &description)
{
    const bool ok = Projects::update(m_id, title, description);
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

bool ProjectModel::setNote(qint64 itemId, const QString &note)
{
    const bool ok = Projects::setNote(m_id, itemId, note);
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

bool ProjectModel::removeItem(qint64 itemId)
{
    const bool ok = Projects::removeItem(m_id, itemId);
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

qint64 ProjectModel::createDocument(const QString &title)
{
    const qint64 id = Projects::createDocument(m_id, title);
    Q_EMIT ProjectsNotifier::instance()->changed();
    return id;
}

bool ProjectModel::removeDocument(qint64 documentId)
{
    const bool ok = Projects::removeDocument(documentId);
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}

QString ProjectModel::articleLink(qint64 itemId) const
{
    for (const ProjectItem &i : Projects::items(m_id, QStringLiteral("article")))
        if (i.id == itemId)
            return Projects::articleLink(i.article);
    return {};
}

QString ProjectModel::quote(qint64 itemId) const
{
    for (const ProjectItem &i : Projects::items(m_id, QStringLiteral("clip")))
        if (i.id == itemId)
            return Projects::quote(i);
    return {};
}

QString ProjectModel::exportTo(const QUrl &file) const
{
    return writeTextFile(file, Projects::exportProject(m_id));
}

QString ProjectModel::preview(const QString &markdown) const
{
    return Projects::previewMarkdown(markdown, m_items);
}

QString ProjectModel::exportName() const
{
    return Projects::slug(m_title, QStringLiteral("project")) + QStringLiteral(".md");
}
