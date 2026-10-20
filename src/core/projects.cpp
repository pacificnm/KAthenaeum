// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "projects.h"

#include "citations.h"
#include "database.h"
#include "library.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUrl>

namespace
{
void touch(qint64 projectId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE projects SET updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    q.addBindValue(projectId);
    q.exec();
}

bool exists(qint64 projectId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT 1 FROM projects WHERE id = ?"));
    q.addBindValue(projectId);
    return q.exec() && q.next();
}

QByteArray refsJson(const QList<ArticleRef> &refs)
{
    QJsonArray a;
    for (const ArticleRef &r : refs)
        a.append(QJsonObject{{QStringLiteral("libraryId"), r.libraryId}, {QStringLiteral("path"), r.path}, {QStringLiteral("title"), r.title}});
    return QJsonDocument(a).toJson(QJsonDocument::Compact);
}

QList<ArticleRef> refsFrom(const QByteArray &json)
{
    QList<ArticleRef> out;
    for (const auto array = QJsonDocument::fromJson(json).array(); const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        out.append(
            {qint64(o.value(QLatin1String("libraryId")).toDouble()), o.value(QLatin1String("path")).toString(), o.value(QLatin1String("title")).toString()});
    }
    return out;
}

const char *ProjectSelect =
    "SELECT p.id, p.title, p.description, p.updated_at,"
    " (SELECT count(*) FROM project_items i WHERE i.project_id = p.id AND i.kind = 'article'),"
    " (SELECT count(*) FROM project_items i WHERE i.project_id = p.id AND i.kind = 'clip'),"
    " (SELECT count(*) FROM chats c WHERE c.project_id = p.id"
    "   OR c.id IN (SELECT i.chat_id FROM project_items i WHERE i.project_id = p.id AND i.kind = 'chat')),"
    " (SELECT count(*) FROM project_documents d WHERE d.project_id = p.id)"
    " FROM projects p";

ProjectRecord project(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(),
            q.value(1).toString(),
            q.value(2).toString(),
            Database::timestamp(q.value(3)),
            q.value(4).toInt(),
            q.value(5).toInt(),
            q.value(6).toInt(),
            q.value(7).toInt()};
}

// [text](zim://lib<id>/<path>) as written by articleLink, or by hand
const QRegularExpression &zimLink()
{
    static const QRegularExpression re(QStringLiteral("\\[([^\\]\\n]*)\\]\\(zim://lib(\\d+)/([^)\\s]+)\\)"));
    return re;
}
}

// ---- projects ----

qint64 Projects::create(const QString &title, const QString &description)
{
    const QString t = title.simplified().left(200);
    if (t.isEmpty())
        return 0;
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO projects (title, description) VALUES (?, ?)"));
    q.addBindValue(t);
    q.addBindValue(Database::text(description.trimmed()));
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

bool Projects::update(qint64 id, const QString &title, const QString &description)
{
    const QString t = title.simplified().left(200);
    if (t.isEmpty())
        return false;
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE projects SET title = ?, description = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    q.addBindValue(t);
    q.addBindValue(Database::text(description.trimmed()));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

bool Projects::remove(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM projects WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

QList<ProjectRecord> Projects::all()
{
    QList<ProjectRecord> out;
    QSqlQuery q;
    if (q.exec(QLatin1String(ProjectSelect) + QStringLiteral(" ORDER BY p.updated_at DESC, p.id DESC")))
        while (q.next())
            out.append(project(q));
    return out;
}

std::optional<ProjectRecord> Projects::byId(qint64 id)
{
    QSqlQuery q;
    q.prepare(QLatin1String(ProjectSelect) + QStringLiteral(" WHERE p.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return project(q);
    return std::nullopt;
}

QList<qint64> Projects::containing(qint64 libraryId, const QString &path)
{
    QList<qint64> out;
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT project_id FROM project_items WHERE kind = 'article' AND library_id = ? AND path = ?"));
    q.addBindValue(libraryId);
    q.addBindValue(path);
    if (q.exec())
        while (q.next())
            out << q.value(0).toLongLong();
    return out;
}

// ---- sources ----

qint64 Projects::addArticle(qint64 projectId, const ArticleRef &a, const QString &note)
{
    if (a.libraryId <= 0 || a.path.isEmpty() || !exists(projectId))
        return 0;
    QSqlQuery q;
    q.prepare(
        QStringLiteral("INSERT INTO project_items (project_id, kind, library_id, path, title, note) VALUES (?, 'article', ?, ?, ?, ?) "
                       "ON CONFLICT (project_id, library_id, path) WHERE kind = 'article' DO UPDATE SET "
                       "note = CASE WHEN excluded.note <> '' THEN excluded.note ELSE project_items.note END "
                       "RETURNING id"));
    q.addBindValue(projectId);
    q.addBindValue(a.libraryId);
    q.addBindValue(a.path);
    q.addBindValue(Database::text(a.title));
    q.addBindValue(Database::text(note.trimmed()));
    if (!q.exec() || !q.next())
        return 0;
    const qint64 id = q.value(0).toLongLong();
    q.finish();
    touch(projectId);
    return id;
}

qint64 Projects::addClip(qint64 projectId, const QString &text, const QList<ArticleRef> &refs, const QString &section, const QString &note)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || !exists(projectId))
        return 0;
    const ArticleRef first = refs.value(0);
    QSqlQuery q;
    q.prepare(
        QStringLiteral("INSERT INTO project_items (project_id, kind, library_id, path, title, section, text, refs, note) "
                       "VALUES (?, 'clip', ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(projectId);
    q.addBindValue(Database::nullableId(first.libraryId));
    q.addBindValue(Database::text(first.path));
    q.addBindValue(Database::text(first.title));
    q.addBindValue(Database::text(section));
    q.addBindValue(t);
    q.addBindValue(QString::fromUtf8(refsJson(refs)));
    q.addBindValue(Database::text(note.trimmed()));
    if (!q.exec())
        return 0;
    const qint64 id = q.lastInsertId().toLongLong();
    touch(projectId);
    return id;
}

bool Projects::setLesson(qint64 itemId, const QString &lesson)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE project_items SET lesson = ? WHERE id = ? AND lesson = ''"));
    q.addBindValue(Database::text(lesson));
    q.addBindValue(itemId);
    return q.exec();
}

qint64 Projects::attachChat(qint64 projectId, qint64 chatId)
{
    if (!exists(projectId))
        return 0;
    QSqlQuery q;
    // not a chat started in this project: it is listed there already (attached as well, it was counted and shown twice)
    q.prepare(
        QStringLiteral("INSERT INTO project_items (project_id, kind, chat_id) SELECT ?, 'chat', id FROM chats WHERE id = ? "
                       "AND coalesce(project_id, 0) <> ? "
                       "ON CONFLICT (project_id, chat_id) WHERE kind = 'chat' DO UPDATE SET chat_id = excluded.chat_id RETURNING id"));
    q.addBindValue(projectId);
    q.addBindValue(chatId);
    q.addBindValue(projectId);
    if (!q.exec() || !q.next())
        return 0;
    const qint64 id = q.value(0).toLongLong();
    q.finish();
    touch(projectId);
    return id;
}

bool Projects::setNote(qint64 projectId, qint64 itemId, const QString &note)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE project_items SET note = ? WHERE id = ? AND project_id = ?"));
    q.addBindValue(Database::text(note.trimmed()));
    q.addBindValue(itemId);
    q.addBindValue(projectId);
    const bool ok = q.exec() && q.numRowsAffected() == 1;
    if (ok)
        touch(projectId);
    return ok;
}

bool Projects::removeItem(qint64 projectId, qint64 itemId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM project_items WHERE id = ? AND project_id = ?"));
    q.addBindValue(itemId);
    q.addBindValue(projectId);
    const bool ok = q.exec() && q.numRowsAffected() == 1;
    if (ok)
        touch(projectId);
    return ok;
}

bool Projects::setEmbedding(qint64 itemId, const Vector &v, const QString &model)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE project_items SET embedding = ?, embed_model = ? WHERE id = ? AND kind = 'clip'"));
    q.addBindValue(Embedding::toBlob(v));
    q.addBindValue(Database::text(Embedding::normalizeName(model)));
    q.addBindValue(itemId);
    return q.exec() && q.numRowsAffected() == 1;
}

QList<ProjectItem> Projects::items(qint64 projectId, const QString &kind)
{
    QList<ProjectItem> out;
    QSqlQuery q;
    q.prepare(
        QStringLiteral("SELECT i.id, i.project_id, i.kind, coalesce(i.library_id, 0), i.path, i.title, i.section, i.text, i.refs, i.note,"
                       " i.embedding, i.embed_model, coalesce(i.chat_id, 0), coalesce(c.title, ''), i.created_at, i.lesson"
                       " FROM project_items i LEFT JOIN chats c ON c.id = i.chat_id"
                       " WHERE i.project_id = ? AND (? = '' OR i.kind = ?) ORDER BY i.created_at DESC, i.id DESC"));
    q.addBindValue(projectId);
    q.addBindValue(Database::text(kind));
    q.addBindValue(Database::text(kind));
    if (!q.exec())
        return out;
    while (q.next()) {
        ProjectItem i;
        i.id = q.value(0).toLongLong();
        i.projectId = q.value(1).toLongLong();
        i.kind = q.value(2).toString();
        i.article = {q.value(3).toLongLong(), q.value(4).toString(), q.value(5).toString()};
        i.section = q.value(6).toString();
        i.text = q.value(7).toString();
        i.refs = refsFrom(q.value(8).toString().toUtf8());
        i.note = q.value(9).toString();
        i.embedding = Embedding::fromBlob(q.value(10).toByteArray());
        i.embedModel = q.value(11).toString();
        i.chatId = q.value(12).toLongLong();
        i.chatTitle = q.value(13).toString();
        i.createdAt = Database::timestamp(q.value(14));
        i.lesson = q.value(15).toString();
        out << i;
    }
    return out;
}

QList<Highlight> Projects::highlightsFor(qint64 libraryId, const QString &path)
{
    QSqlQuery q;
    q.prepare(
        QStringLiteral("SELECT i.text, p.title, i.note FROM project_items i JOIN projects p ON p.id = i.project_id "
                       "WHERE i.kind = 'clip' AND i.library_id = ? AND i.path = ? ORDER BY i.created_at, i.id"));
    q.addBindValue(libraryId);
    q.addBindValue(path);
    QStringList order;
    QHash<QString, QStringList> lines;
    QHash<QString, QString> texts;
    if (q.exec()) {
        while (q.next()) {
            const QString text = q.value(0).toString();
            const QString key = text.simplified().toLower();
            if (!lines.contains(key)) {
                order << key;
                texts.insert(key, text);
            }
            const QString note = q.value(2).toString();
            lines[key] << (note.isEmpty()
                               ? i18nc("@info:tooltip %1 is a project's title", "Saved in “%1”", q.value(1).toString())
                               : i18nc("@info:tooltip %1 is a project's title, %2 the user's note", "Saved in “%1”: %2", q.value(1).toString(), note));
        }
    }
    QList<Highlight> out;
    for (const QString &k : std::as_const(order))
        out.append(Highlight(texts.value(k), lines.value(k).join(QLatin1Char('\n'))));
    return out;
}

bool Projects::forgetLibrary(qint64 libraryId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM project_items WHERE kind = 'article' AND library_id = ?"));
    q.addBindValue(libraryId);
    return q.exec();
}

// ---- documents ----

qint64 Projects::createDocument(qint64 projectId, const QString &title, const QString &content)
{
    if (!exists(projectId))
        return 0;
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO project_documents (project_id, title, content) VALUES (?, ?, ?)"));
    q.addBindValue(projectId);
    q.addBindValue(title.simplified().isEmpty() ? i18nc("@title a document with no title", "Untitled") : title.simplified().left(200));
    q.addBindValue(Database::text(content));
    if (!q.exec())
        return 0;
    const qint64 id = q.lastInsertId().toLongLong();
    touch(projectId);
    return id;
}

std::optional<DocumentRecord> Projects::document(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT id, project_id, title, content, updated_at FROM project_documents WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return DocumentRecord{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(), q.value(3).toString(), Database::timestamp(q.value(4))};
}

bool Projects::saveDocument(qint64 id, const QString &title, const QString &content)
{
    QSqlQuery q;
    q.prepare(
        QStringLiteral("UPDATE project_documents SET title = ?, content = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') "
                       "WHERE id = ? RETURNING project_id"));
    q.addBindValue(title.simplified().isEmpty() ? i18nc("@title a document with no title", "Untitled") : title.simplified().left(200));
    q.addBindValue(Database::text(content));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    const qint64 projectId = q.value(0).toLongLong();
    q.finish();
    touch(projectId);
    return true;
}

bool Projects::removeDocument(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM project_documents WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

QList<DocumentRecord> Projects::documents(qint64 projectId)
{
    QList<DocumentRecord> out;
    QSqlQuery q;
    q.prepare(
        QStringLiteral("SELECT id, project_id, title, content, updated_at FROM project_documents WHERE project_id = ? ORDER BY updated_at DESC, id DESC"));
    q.addBindValue(projectId);
    if (q.exec())
        while (q.next())
            out << DocumentRecord{q.value(0).toLongLong(),
                                  q.value(1).toLongLong(),
                                  q.value(2).toString(),
                                  q.value(3).toString(),
                                  Database::timestamp(q.value(4))};
    return out;
}

// ---- Markdown ----

QString Projects::articleLink(const ArticleRef &a)
{
    // parentheses are encoded: "Ray_(film)" inside a Markdown link's (...) would end it early
    const QString path = QString::fromLatin1(QUrl::toPercentEncoding(a.path, "/_-.,!~*'"));
    QString text = a.title;
    text.replace(QLatin1Char('['), QLatin1Char('(')).replace(QLatin1Char(']'), QLatin1Char(')'));
    // one arg() call: chained, a title containing "%1" would be filled in by the next one
    return QStringLiteral("[%1](zim://lib%2/%3)").arg(text, QString::number(a.libraryId), path);
}

QString Projects::quote(const ProjectItem &clip)
{
    QStringList lines;
    for (const auto clipLines = clip.text.trimmed().split(QLatin1Char('\n')); const QString &l : clipLines)
        lines << QStringLiteral("> ") + l;
    QStringList from;
    for (const ArticleRef &r : clip.refs)
        if (!r.title.isEmpty())
            from << r.title;
    QString source = from.join(QStringLiteral(", "));
    if (!clip.section.isEmpty())
        source += QStringLiteral(" (%1)").arg(clip.section);
    if (!source.trimmed().isEmpty())
        lines << QStringLiteral(">") << QStringLiteral("> — ") + source.trimmed();
    return lines.join(QLatin1Char('\n'));
}

namespace
{
Citations::Source citationFor(qint64 libraryId, const QString &path, const QString &title)
{
    const QString shown = title.isEmpty() ? QString(path).replace(QLatin1Char('_'), QLatin1Char(' ')) : title;
    if (const auto library = Library::byId(libraryId))
        return Citations::source(*library, path, shown);
    Citations::Source s; // its library is gone: cite what is known
    s.title = shown;
    s.path = path;
    return s;
}

// A heading that names a list of sources: the user's own, or one inserted with "Insert Works Cited"
bool hasSourceList(const QString &markdown)
{
    static const QRegularExpression heading(QStringLiteral("^#{1,3}\\s*(Works Cited|References|Bibliography)\\s*$"),
                                            QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if (heading.match(markdown).hasMatch())
        return true;
    // the headings as the app writes them in the user's language
    QStringList translated;
    for (const auto style : {Citations::Style::Mla, Citations::Style::Apa, Citations::Style::Chicago})
        translated << QRegularExpression::escape(Citations::listHeading(style));
    const QRegularExpression local(QStringLiteral("^#{1,3}\\s*(%1)\\s*$").arg(translated.join(QLatin1Char('|'))),
                                   QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    return local.match(markdown).hasMatch();
}
}

QList<Citations::Source> Projects::citedSources(const QString &markdown)
{
    QList<Citations::Source> out;
    QSet<QString> seen;
    auto it = zimLink().globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        const qint64 libraryId = m.captured(2).toLongLong();
        const QString path = QUrl::fromPercentEncoding(m.captured(3).toUtf8());
        const QString key = m.captured(2) + QLatin1Char('/') + path;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out << citationFor(libraryId, path, {});
    }
    return out;
}

QString Projects::sourceList(const QString &markdown, Citations::Style style, const QDate &accessed)
{
    const QStringList entries = Citations::sorted(style, citedSources(markdown), accessed, true);
    if (entries.isEmpty())
        return {};
    return QStringLiteral("## ") + Citations::listHeading(style) + QStringLiteral("\n\n") + entries.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

QList<Citations::Source> Projects::articleSources(qint64 projectId)
{
    QList<Citations::Source> sources;
    for (const auto articles = items(projectId, QStringLiteral("article")); const ProjectItem &a : articles)
        sources << citationFor(a.article.libraryId, a.article.path, a.article.title);
    return sources;
}

QStringList Projects::bibliography(qint64 projectId, Citations::Style style, const QDate &accessed)
{
    return Citations::sorted(style, articleSources(projectId), accessed, true);
}

QString Projects::exportMarkdown(const QString &markdown, Citations::Style style, const QDate &accessed)
{
    QString out;
    qsizetype last = 0;
    auto it = zimLink().globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        out += markdown.mid(last, m.capturedStart() - last) + m.captured(1);
        last = m.capturedEnd();
    }
    out += QStringView(markdown).mid(last);
    // the articles it links to, cited in the chosen style; a list the document already has (the user's own, or one
    // inserted with "Insert Works Cited") is kept instead
    if (hasSourceList(markdown))
        return out;
    const QString list = sourceList(markdown, style, accessed);
    return list.isEmpty() ? out : out.trimmed() + QStringLiteral("\n\n") + list;
}

namespace
{
QString squeezed(const QString &s)
{
    QString out;
    for (const QChar c : s)
        if (!c.isSpace())
            out += c.toLower();
    return out;
}
}

QString Projects::previewMarkdown(const QString &markdown, const QList<ProjectItem> &items)
{
    // links to the project's articles: the note after the link
    QHash<QString, QString> articleNotes;
    QList<std::pair<QString, QString>> clipNotes; // (squeezed text, note)
    for (const ProjectItem &i : items) {
        if (i.note.isEmpty())
            continue;
        if (i.kind == QLatin1String("article"))
            articleNotes.insert(QString::number(i.article.libraryId) + QLatin1Char('/') + i.article.path, i.note);
        else if (i.kind == QLatin1String("clip"))
            clipNotes.append({squeezed(i.text), i.note});
    }
    QString linked;
    qsizetype last = 0;
    auto it = zimLink().globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        linked += QStringView(markdown).mid(last, m.capturedEnd() - last);
        last = m.capturedEnd();
        const QString note = articleNotes.value(m.captured(2) + QLatin1Char('/') + QUrl::fromPercentEncoding(m.captured(3).toUtf8()));
        if (!note.isEmpty() && !markdown.contains(note)) // a summary draft already writes the note out
            linked += QLatin1Char(' ')
                + i18nc("@info a note shown after a link in a document preview; keep the asterisks (Markdown italics)", "*(note: %1)*", note.simplified());
    }
    linked += QStringView(markdown).mid(last);

    // quotations of saved excerpts: the note under the quotation
    const QStringList lines = linked.split(QLatin1Char('\n'));
    QStringList out;
    for (int i = 0; i < lines.size();) {
        if (!lines.at(i).startsWith(QLatin1Char('>'))) {
            out << lines.at(i++);
            continue;
        }
        QString quoted;
        while (i < lines.size() && lines.at(i).startsWith(QLatin1Char('>'))) {
            const QString body = lines.at(i).mid(1).trimmed();
            if (!body.startsWith(QStringLiteral("— ")) && !body.startsWith(QLatin1String("-- ")))
                quoted += body + QLatin1Char(' ');
            out << lines.at(i++);
        }
        const QString q = squeezed(quoted);
        for (const auto &[text, note] : std::as_const(clipNotes)) {
            const bool same = q == text || (q.size() >= 30 && (text.contains(q) || q.contains(text)));
            if (same && !markdown.contains(note)) {
                out << QString() << i18nc("@info keep the asterisks (Markdown italics)", "*Your note: %1*", note.simplified());
                break;
            }
        }
    }
    return out.join(QLatin1Char('\n'));
}

QString Projects::exportProject(qint64 projectId, Citations::Style style, const QDate &accessed)
{
    const auto p = byId(projectId);
    if (!p)
        return {};
    QStringList out{QStringLiteral("# ") + p->title, QString()};
    if (!p->description.isEmpty())
        out << p->description << QString();
    const auto docs = documents(projectId);
    for (auto it = docs.crbegin(); it != docs.crend(); ++it) // oldest first, as they were written
        out << QStringLiteral("---") << QString() << QStringLiteral("## ") + it->title << QString() << exportMarkdown(it->content, style, accessed).trimmed()
            << QString();
    const auto articles = items(projectId, QStringLiteral("article"));
    if (!articles.isEmpty()) {
        out << QStringLiteral("---") << QString() << QStringLiteral("## ") + i18nc("@title heading in an exported project", "Sources") << QString();
        for (const ProjectItem &a : articles) {
            // each one cited as the chosen style has it, with the note under it
            QString line = QStringLiteral("- ") + Citations::format(style, citationFor(a.article.libraryId, a.article.path, a.article.title), accessed, true);
            if (!a.note.isEmpty())
                line += QStringLiteral("\n  ") + i18nc("@info the user's note on a source in an exported project", "Note: %1", a.note);
            out << line;
        }
        out << QString();
    }
    const auto clips = items(projectId, QStringLiteral("clip"));
    if (!clips.isEmpty()) {
        out << QStringLiteral("## ") + i18nc("@title heading in an exported project", "Saved excerpts") << QString();
        for (const ProjectItem &c : clips) {
            out << quote(c);
            if (!c.note.isEmpty())
                out << QString() << i18nc("@info the user's note on a saved excerpt in an exported project", "Note: %1", c.note);
            out << QString();
        }
    }
    return out.join(QLatin1Char('\n')).trimmed() + QLatin1Char('\n');
}

QString Projects::slug(const QString &text, const QString &fallback)
{
    static const QRegularExpression other(QStringLiteral("[^a-z0-9]+"));
    QString s = text.toLower();
    s.replace(other, QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('-')))
        s.chop(1);
    return (s.isEmpty() ? fallback : s).left(60);
}
