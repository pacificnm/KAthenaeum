// SPDX-License-Identifier: GPL-3.0-or-later
#include "conversation.h"

#include "core/answerer.h"
#include "core/chats.h"
#include "core/library.h"
#include "modelsettings.h"

#include <KLocalizedString>

#include <QJsonDocument>

namespace
{
QByteArray json(const QVariant &v)
{
    return QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact);
}

QVariant parse(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).toVariant();
}

QVariantList toVariant(const QList<Rag::Citation> &citations)
{
    QVariantList out;
    for (const Rag::Citation &c : citations) {
        QVariantList sources;
        for (int n : c.sources)
            sources << n;
        out << QVariantMap{{QStringLiteral("start"), c.start},
                           {QStringLiteral("end"), c.end},
                           {QStringLiteral("sources"), sources},
                           {QStringLiteral("score"), qRound(c.score * 1000) / 1000.0},
                           {QStringLiteral("reason"), c.reason},
                           {QStringLiteral("numbers"), c.numbers}};
    }
    return out;
}

QList<Rag::Citation> fromVariant(const QVariantList &list)
{
    QList<Rag::Citation> out;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        Rag::Citation c;
        c.start = m.value(QStringLiteral("start")).toInt();
        c.end = m.value(QStringLiteral("end")).toInt();
        for (const QVariant &n : m.value(QStringLiteral("sources")).toList())
            c.sources << n.toInt();
        c.score = m.value(QStringLiteral("score")).toDouble();
        c.reason = m.value(QStringLiteral("reason")).toString();
        c.numbers = m.value(QStringLiteral("numbers")).toStringList();
        out << c;
    }
    return out;
}

QVariantList toVariant(const QList<Rag::Source> &sources)
{
    QVariantList out;
    for (const Rag::Source &s : sources) {
        const auto library = Library::byId(s.libraryId);
        out << QVariantMap{{QStringLiteral("n"), s.n},
                           {QStringLiteral("libraryId"), s.libraryId},
                           {QStringLiteral("libraryTitle"), library ? library->title : QString()},
                           {QStringLiteral("path"), s.path},
                           {QStringLiteral("title"), s.title},
                           {QStringLiteral("section"), s.section},
                           {QStringLiteral("snippet"), s.snippet}};
    }
    return out;
}

QString seconds(int ms)
{
    return QString::number(ms / 1000.0, 'f', 1);
}
}

Conversation::Conversation(QObject *parent)
    : QAbstractListModel(parent)
    , m_answerer(new Answerer(ModelSettings::instance()->client(), this))
{
    connect(m_answerer, &Answerer::phaseChanged, this, [this](Answerer::Phase phase, int count) {
        if (m_answering < 0)
            return;
        QString &p = m_messages[m_answering].phase;
        switch (phase) {
        case Answerer::Finding:
            p = i18n("Finding articles…");
            break;
        case Answerer::Ranking:
            p = i18np("Comparing %1 passage with the question…", "Comparing %1 passages with the question…", count);
            break;
        case Answerer::Reading:
            p = i18np("The model is reading %1 source…", "The model is reading %1 sources…", count);
            break;
        case Answerer::Writing:
            p.clear();
            break;
        case Answerer::Checking:
            p = i18n("Matching sentences to sources…");
            break;
        }
        changed(m_answering);
    });
    connect(m_answerer, &Answerer::sourcesReady, this, [this](const QList<Rag::Source> &sources, bool degraded, const QString &note) {
        if (m_answering < 0)
            return;
        Message &m = m_messages[m_answering];
        m.sources = toVariant(sources);
        if (degraded)
            m.note = i18n("Sources were ranked by words only, and sentences are not linked to them: %1", note);
        changed(m_answering);
    });
    connect(m_answerer, &Answerer::textAdded, this, [this](const QString &text) {
        if (m_answering < 0)
            return;
        m_messages[m_answering].content += text;
        changed(m_answering);
    });
    connect(m_answerer, &Answerer::finished, this, &Conversation::finished);
}

Conversation::~Conversation()
{
    m_answerer->stop();  // saves what was written as stopped
}

int Conversation::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_messages.size());
}

QHash<int, QByteArray> Conversation::roleNames() const
{
    return {{RoleRole, "role"},     {TextRole, "text"},           {StatusRole, "status"},
            {PhaseRole, "phase"},   {ErrorRole, "error"},         {NoteRole, "note"},
            {SourcesRole, "sources"}, {CitationsRole, "citations"}, {StatsRole, "stats"}};
}

QVariant Conversation::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_messages.size())
        return {};
    const Message &m = m_messages.at(index.row());
    switch (role) {
    case RoleRole:
        return m.role;
    case TextRole:
        return displayText(m);
    case StatusRole:
        return m.status;
    case PhaseRole:
        return m.phase;
    case ErrorRole:
        return m.error;
    case NoteRole:
        return m.note;
    case SourcesRole:
        return m.sources;
    case CitationsRole:
        return m.citations;
    case StatsRole:
        return statsLine(m);
    }
    return {};
}

QString Conversation::displayText(const Message &m) const
{
    if (m.role != QLatin1String("assistant"))
        return m.content;
    if (m.status == QLatin1String("working"))
        return Rag::tidyAnswer(m.content);
    return Rag::annotate(m.content, fromVariant(m.citations));
}

QString Conversation::statsLine(const Message &m) const
{
    if (m.role != QLatin1String("assistant") || m.status == QLatin1String("working"))
        return {};
    const int total = m.meta.value(QStringLiteral("totalMs")).toInt();
    const int first = m.meta.value(QStringLiteral("firstTokenMs")).toInt();
    if (total <= 0)
        return {};
    QStringList parts{i18n("%1 s", seconds(total))};
    if (first > 0)
        parts << i18n("first words after %1 s", seconds(first));
    if (const int n = int(m.sources.size()); n > 0)
        parts << i18np("%1 source", "%1 sources", n);
    if (m.meta.value(QStringLiteral("truncated")).toBool())
        parts << i18n("cut off at the length limit");
    parts << m.meta.value(QStringLiteral("answerModel")).toString();
    return parts.join(QStringLiteral(" · "));
}

void Conversation::changed(int row)
{
    const QModelIndex i = index(row);
    Q_EMIT dataChanged(i, i);
}

bool Conversation::busy() const
{
    return m_answerer->running();
}

void Conversation::setArticleLibraryId(qint64 id)
{
    if (id == m_article.libraryId || m_chatId > 0)
        return;  // a saved chat keeps the article it is about
    m_article.libraryId = id;
    Q_EMIT articleChanged();
}

void Conversation::setArticlePath(const QString &path)
{
    if (path == m_article.path || m_chatId > 0)
        return;
    m_article.path = path;
    Q_EMIT articleChanged();
}

void Conversation::setArticleTitle(const QString &title)
{
    if (title == m_article.title || m_chatId > 0)
        return;
    m_article.title = title;
    Q_EMIT articleChanged();
}

void Conversation::setChatId(qint64 id)
{
    if (id == m_chatId)
        return;
    m_answerer->stop();
    m_chatId = id;
    load();
    Q_EMIT chatIdChanged();
}

void Conversation::load()
{
    beginResetModel();
    m_messages.clear();
    m_answering = -1;
    m_title.clear();
    if (m_chatId > 0) {
        if (const auto chat = Chats::byId(m_chatId)) {
            m_title = chat->title;
            if (chat->articleLibraryId > 0) {
                m_article = {chat->articleLibraryId, chat->articlePath, chat->articleTitle};
                Q_EMIT articleChanged();
            }
        }
        for (const MessageRecord &r : Chats::messages(m_chatId)) {
            Message m;
            m.id = r.id;
            m.role = r.role;
            m.content = r.content;
            m.status = r.status;
            m.sources = parse(r.sources).toList();
            m.citations = parse(r.citations).toList();
            m.meta = parse(r.meta).toMap();
            m.error = m.meta.value(QStringLiteral("error")).toString();
            m.note = m.meta.value(QStringLiteral("note")).toString();
            m_messages << m;
        }
    }
    endResetModel();
}

void Conversation::append(const Message &m)
{
    beginInsertRows({}, int(m_messages.size()), int(m_messages.size()));
    m_messages << m;
    endInsertRows();
}

void Conversation::ask(const QString &question)
{
    const QString q = question.trimmed();
    if (q.isEmpty() || busy())
        return;

    // what was asked before, for follow-ups ("and its density?")
    QStringList earlier;
    for (const Message &m : std::as_const(m_messages))
        if (m.role == QLatin1String("user"))
            earlier << m.content;

    if (m_chatId <= 0) {
        m_chatId = Chats::create(q, m_article.isSet() ? m_article.libraryId : 0, m_article.path, m_article.title);
        m_title = q.simplified().left(200);
        Q_EMIT chatIdChanged();
    }
    Message user;
    user.role = QStringLiteral("user");
    user.content = q;
    MessageRecord record;
    record.chatId = m_chatId;
    record.role = user.role;
    record.content = q;
    user.id = Chats::addMessage(record);
    append(user);
    Q_EMIT saved();

    Message answer;
    answer.role = QStringLiteral("assistant");
    answer.status = QStringLiteral("working");
    answer.phase = i18n("Finding articles…");
    append(answer);
    m_answering = int(m_messages.size()) - 1;

    QList<qint64> libraries;
    for (const LibraryRecord &r : Library::all())
        if (r.fileExists() && (!m_article.isSet() || r.id == m_article.libraryId))
            libraries << r.id;
    if (libraries.isEmpty() && m_article.isSet()) {
        AnswerResult gone;
        gone.status = QStringLiteral("error");
        gone.error = i18n("“%1” cannot be read: its library is no longer installed.", m_article.title);
        finished(gone);
        return;
    }
    if (libraries.isEmpty()) {
        AnswerResult none;
        none.status = QStringLiteral("error");
        none.error = i18n("No libraries are installed. Add one in the Library Manager, then ask again.");
        finished(none);
        return;
    }
    ModelSettings *models = ModelSettings::instance();
    m_answerer->start({q, earlier, libraries, models->answerModel(), models->embeddingModel(), m_article});
    Q_EMIT busyChanged();
}

void Conversation::stop()
{
    m_answerer->stop();
}

void Conversation::finished(const AnswerResult &result)
{
    if (m_answering < 0)
        return;
    Message &m = m_messages[m_answering];
    m.status = result.status;
    m.content = Rag::tidyAnswer(result.answer);
    m.phase.clear();
    m.error = result.error;
    m.citations = toVariant(result.citations);
    const AnswerStats &s = result.stats;
    m.meta = {{QStringLiteral("findMs"), s.findMs},
              {QStringLiteral("embedMs"), s.embedMs},
              {QStringLiteral("firstTokenMs"), s.firstTokenMs},
              {QStringLiteral("citeMs"), s.citeMs},
              {QStringLiteral("totalMs"), s.totalMs},
              {QStringLiteral("tokens"), s.tokens},
              {QStringLiteral("tokensPerSecond"), qRound(s.tokensPerSecond * 10) / 10.0},
              {QStringLiteral("truncated"), s.truncated},
              {QStringLiteral("answerModel"), ModelSettings::instance()->answerModel()},
              {QStringLiteral("embeddingModel"), ModelSettings::instance()->embeddingModel()}};
    if (!m.error.isEmpty())
        m.meta.insert(QStringLiteral("error"), m.error);
    if (!m.note.isEmpty())
        m.meta.insert(QStringLiteral("note"), m.note);

    MessageRecord record;
    record.chatId = m_chatId;
    record.role = m.role;
    record.content = m.content;
    record.status = m.status;
    record.sources = json(m.sources);
    record.citations = json(m.citations);
    record.meta = json(m.meta);
    m.id = Chats::addMessage(record);

    const int row = m_answering;
    m_answering = -1;
    changed(row);
    Q_EMIT busyChanged();
    Q_EMIT saved();
    if (result.status == QLatin1String("done") && !result.citations.isEmpty())
        ModelSettings::instance()->checkSwapping();
}

QVariantMap Conversation::source(int row, int n) const
{
    if (row < 0 || row >= m_messages.size())
        return {};
    for (const QVariant &v : m_messages.at(row).sources) {
        QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("n")).toInt() != n)
            continue;
        const auto library = Library::byId(s.value(QStringLiteral("libraryId")).toLongLong());
        s.insert(QStringLiteral("available"), library && library->fileExists());
        return s;
    }
    return {};
}

QString Conversation::flagReason(int row, int citation) const
{
    if (row < 0 || row >= m_messages.size())
        return {};
    const QVariantList &cites = m_messages.at(row).citations;
    if (citation < 0 || citation >= cites.size())
        return {};
    const QVariantMap c = cites.at(citation).toMap();
    if (c.value(QStringLiteral("reason")).toString() == QLatin1String("number"))
        return i18n("Check this: %1 appears in none of the sources.", c.value(QStringLiteral("numbers")).toStringList().join(QStringLiteral(", ")));
    return i18n("Check this: no source clearly says this.");
}
