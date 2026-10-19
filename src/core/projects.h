// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "articlerenderer.h"
#include "embedding.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

struct ProjectRecord {
    qint64 id = 0;
    QString title;
    QString description;  // the research question or goal; the project chat's model is told it
    QDateTime updatedAt;
    int articles = 0;
    int clips = 0;
    int chats = 0;  // started in the project or attached to it
    int documents = 0;
};

// Where an article is: a library and a path in it
struct ArticleRef {
    qint64 libraryId = 0;
    QString path;
    QString title;
};

struct ProjectItem {
    qint64 id = 0;
    qint64 projectId = 0;
    QString kind;  // article | clip | chat
    ArticleRef article;      // an article; for a clip, the first article it came from (none for a saved answer with no source)
    QList<ArticleRef> refs;  // a clip: every article it came from
    QString section;         // a clip: the heading it was taken under
    QString text;            // a clip: the saved passage
    QString note;
    Vector embedding;  // a clip, once embedded
    QString embedModel;
    qint64 chatId = 0;  // an attached chat
    QString chatTitle;
    QDateTime createdAt;
};

struct DocumentRecord {
    qint64 id = 0;
    qint64 projectId = 0;
    QString title;
    QString content;  // Markdown
    QDateTime updatedAt;
};

// Research projects (tables projects, project_items, project_documents; schema v6). Port of Athenaeum's projects.py.
namespace Projects
{
// ---- projects ----
qint64 create(const QString &title, const QString &description = {});  // 0 on failure (an empty title is one)
bool update(qint64 id, const QString &title, const QString &description);
bool remove(qint64 id);  // its sources and documents go with it; its chats stay, as ordinary chats
QList<ProjectRecord> all();  // most recently used first
std::optional<ProjectRecord> byId(qint64 id);
QList<qint64> containing(qint64 libraryId, const QString &path);  // projects that have this article as a source

// ---- sources ----
// Idempotent: adding an article twice keeps one item, and never replaces a note with an empty one. Returns the item id.
qint64 addArticle(qint64 projectId, const ArticleRef &article, const QString &note = {});
qint64 addClip(qint64 projectId, const QString &text, const QList<ArticleRef> &refs, const QString &section = {}, const QString &note = {});
qint64 attachChat(qint64 projectId, qint64 chatId);  // idempotent
bool setNote(qint64 projectId, qint64 itemId, const QString &note);
bool removeItem(qint64 projectId, qint64 itemId);
bool setEmbedding(qint64 itemId, const Vector &v, const QString &model);
QList<ProjectItem> items(qint64 projectId, const QString &kind = {});  // newest first
// The excerpts saved from this article, in any project, as highlights for its page: the same passage saved in several
// projects is one highlight naming them all, with their notes
QList<Highlight> highlightsFor(qint64 libraryId, const QString &path);
// A library was removed: its articles leave every project (saved excerpts stay: they hold their own text)
void forgetLibrary(qint64 libraryId);

// ---- documents ----
qint64 createDocument(qint64 projectId, const QString &title, const QString &content = {});
std::optional<DocumentRecord> document(qint64 id);
bool saveDocument(qint64 id, const QString &title, const QString &content);
bool removeDocument(qint64 id);
QList<DocumentRecord> documents(qint64 projectId);  // most recently changed first

// ---- Markdown ----
// A link to an article inside the app (zim://lib<id>/<path>) as it goes into a document
QString articleLink(const ArticleRef &a);
// A saved excerpt quoted in a document, with its source on a "— " line
QString quote(const ProjectItem &clip);
// A document as it leaves the app: links to articles become plain text, and a References list names those articles and
// their libraries (outside the app, zim:// links lead nowhere)
QString exportMarkdown(const QString &markdown);
// A document as its preview shows it: the stored Markdown is not changed, but a link to one of the project's articles
// that has a note gets it after the link, and a quotation of a saved excerpt that has a note gets it under the quotation
// (the "— source" line is set aside when comparing; a long quote may be lightly trimmed)
QString previewMarkdown(const QString &markdown, const QList<ProjectItem> &items);
// The whole project as one Markdown file: goal, documents, sources with notes, saved excerpts
QString exportProject(qint64 projectId);
// A file name from a title: "Why is water polar?" -> "why-is-water-polar"
QString slug(const QString &text, const QString &fallback = QStringLiteral("document"));
}
