// SPDX-License-Identifier: GPL-3.0-or-later
#include "embedding.h"

#include <cmath>

namespace
{
struct Family {
    const char *name;  // a prefix of the model name
    const char *query;
    const char *document;  // "{head}" is the passage's title, "none" when it has none
};

const char *Instruct = "Represent this sentence for searching relevant passages: ";
const Family Families[] = {
    {"nomic-embed-text", "search_query: ", "search_document: "},
    {"embeddinggemma", "task: search result | query: ", "title: {head} | text: "},
    {"mxbai-embed", Instruct, ""},
    {"bge-", Instruct, ""},
    {"snowflake-arctic-embed", Instruct, ""},
    {"multilingual-e5", "query: ", "passage: "},
    {"e5", "query: ", "passage: "},
};

const Family *family(const QString &model)
{
    QString name = Embedding::normalizeName(model).toLower();
    name = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);  // hf.co/owner/name
    for (const Family &f : Families)
        if (name.startsWith(QLatin1String(f.name)))
            return &f;
    return nullptr;
}
}

QString Embedding::normalizeName(const QString &model)
{
    return model.endsWith(QLatin1String(":latest")) ? model.chopped(7) : model;
}

QString Embedding::formatQuery(const QString &model, const QString &text)
{
    const Family *f = family(model);
    return f ? QLatin1String(f->query) + text : text;
}

QString Embedding::formatDocument(const QString &model, const QString &text)
{
    const Family *f = family(model);
    if (!f)
        return text;
    QString prefix = QLatin1String(f->document);
    prefix.replace(QLatin1String("{head}"), QLatin1String("none"));
    return prefix + text;
}

double Embedding::cosine(const Vector &a, const Vector &b)
{
    if (a.isEmpty() || a.size() != b.size())
        return 0;
    double dot = 0, na = 0, nb = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        dot += double(a[i]) * b[i];
        na += double(a[i]) * a[i];
        nb += double(b[i]) * b[i];
    }
    return na > 0 && nb > 0 ? dot / (std::sqrt(na) * std::sqrt(nb)) : 0;
}
