// SPDX-License-Identifier: GPL-3.0-or-later
#include "highlights.h"

#include "database.h"
#include "projects.h"

#include <QHash>
#include <QSqlQuery>
#include <QTimeZone>

namespace
{
constexpr QLatin1StringView Columns("id, library_id, path, title, text, color, note, created_at, updated_at");

QDateTime timestamp(const QVariant &v)
{
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    t.setTimeZone(QTimeZone::UTC);
    return t;
}

HighlightRecord fromRow(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(), q.value(3).toString(), q.value(4).toString(),
            q.value(5).toString(), q.value(6).toString(), timestamp(q.value(7)), timestamp(q.value(8))};
}

QString validColor(const QString &color)
{
    return Highlights::colors().contains(color) ? color : Highlights::colors().constFirst();
}

// what two highlights of the same passage share: whitespace and case are not what makes them different
QString squeezed(const QString &s)
{
    QString out;
    for (const QChar c : s)
        if (!c.isSpace())
            out += c.toLower();
    return out;
}

bool update(qint64 id, const char *column, const QString &value)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE highlights SET %1 = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?").arg(QLatin1String(column)));
    q.addBindValue(Database::text(value));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}
}

QStringList Highlights::colors()
{
    return {QStringLiteral("yellow"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("pink")};
}

bool Highlights::tooShort(const QString &text)
{
    return squeezed(text).size() < 8;  // ArticleRenderer will not place a shorter one
}

qint64 Highlights::add(qint64 libraryId, const QString &path, const QString &title, const QString &text, const QString &color, const QString &note)
{
    const QString passage = text.trimmed();
    if (libraryId <= 0 || path.isEmpty() || tooShort(passage))
        return 0;
    const QString key = squeezed(passage);
    for (const auto existing = forArticle(libraryId, path); const HighlightRecord &h : existing) {
        if (squeezed(h.text) != key)
            continue;
        if (!color.isEmpty())
            setColor(h.id, validColor(color));
        if (!note.trimmed().isEmpty())
            setNote(h.id, note);
        return h.id;
    }
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO highlights (library_id, path, title, text, color, note) VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(libraryId);
    q.addBindValue(path);
    q.addBindValue(Database::text(title));
    q.addBindValue(passage);
    q.addBindValue(validColor(color));
    q.addBindValue(Database::text(note.trimmed()));
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

bool Highlights::setNote(qint64 id, const QString &note)
{
    return update(id, "note", note.trimmed());
}

bool Highlights::setColor(qint64 id, const QString &color)
{
    return colors().contains(color) && update(id, "color", color);
}

bool Highlights::remove(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM highlights WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

std::optional<HighlightRecord> Highlights::byId(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM highlights WHERE id = ?").arg(Columns));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return fromRow(q);
    return std::nullopt;
}

QList<HighlightRecord> Highlights::forArticle(qint64 libraryId, const QString &path)
{
    QList<HighlightRecord> out;
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM highlights WHERE library_id = ? AND path = ? ORDER BY created_at, id").arg(Columns));
    q.addBindValue(libraryId);
    q.addBindValue(path);
    if (q.exec())
        while (q.next())
            out.append(fromRow(q));
    return out;
}

QList<HighlightRecord> Highlights::all(const QString &color, const QString &text)
{
    QString like = text.simplified();
    like.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('%'), QLatin1String("\\%")).replace(QLatin1Char('_'), QLatin1String("\\_"));
    like = QLatin1Char('%') + like + QLatin1Char('%');
    QSqlQuery q;
    // the article whose highlights changed last first; within an article, the order they were made
    q.prepare(QStringLiteral("SELECT %1 FROM highlights h "
                             "JOIN (SELECT library_id AS l, path AS p, max(updated_at) AS latest FROM highlights GROUP BY library_id, path) a "
                             "ON a.l = h.library_id AND a.p = h.path "
                             "WHERE (? = '' OR h.color = ?) AND (h.text LIKE ? ESCAPE '\\' OR h.note LIKE ? ESCAPE '\\' OR h.title LIKE ? ESCAPE '\\') "
                             "ORDER BY a.latest DESC, h.library_id, h.path, h.created_at, h.id")
                  .arg(QStringLiteral("h.id, h.library_id, h.path, h.title, h.text, h.color, h.note, h.created_at, h.updated_at")));
    q.addBindValue(Database::text(color));
    q.addBindValue(Database::text(color));
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(like);
    QList<HighlightRecord> out;
    if (q.exec())
        while (q.next())
            out.append(fromRow(q));
    return out;
}

int Highlights::count()
{
    QSqlQuery q(QStringLiteral("SELECT count(*) FROM highlights"));
    return q.next() ? q.value(0).toInt() : 0;
}

QList<Highlight> Highlights::marksFor(qint64 libraryId, const QString &path)
{
    QList<Highlight> out;
    for (const auto existing = forArticle(libraryId, path); const HighlightRecord &h : existing) {
        Highlight m;
        m.text = h.text;
        m.label = h.note;
        m.cssClass = QStringLiteral("hl hl-") + validColor(h.color);
        m.anchor = QStringLiteral("hl-%1").arg(h.id);
        out.append(m);
    }
    return out;
}

QString Highlights::markdown(const QList<HighlightRecord> &highlights)
{
    QStringList out;
    QString article;
    for (const HighlightRecord &h : highlights) {
        const QString key = QString::number(h.libraryId) + QLatin1Char('/') + h.path;
        if (key != article) {
            article = key;
            const QString title = h.title.isEmpty() ? QString(h.path).replace(QLatin1Char('_'), QLatin1Char(' ')) : h.title;
            out << QStringLiteral("## ") + Projects::articleLink({h.libraryId, h.path, title}) << QString();
        }
        for (const auto qLatin1Char = h.text.trimmed().split(QLatin1Char('\n')); const QString &line : qLatin1Char)
            out << QStringLiteral("> ") + line;
        out << QString();
        if (!h.note.isEmpty())
            out << h.note << QString();
    }
    return out.join(QLatin1Char('\n')).trimmed() + (out.isEmpty() ? QString() : QStringLiteral("\n"));
}
