// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

using Vector = QList<float>;

// What embedding models need around the text, and comparing their vectors.
namespace Embedding
{
// Embedding models are trained with their own task prefixes and match noticeably worse without them (Athenaeum's
// models.py). A model not known here gets none.
QString formatQuery(const QString &model, const QString &text);
QString formatDocument(const QString &model, const QString &text);

// "nomic-embed-text:latest" and "nomic-embed-text" are the same model
QString normalizeName(const QString &model);

// Cosine similarity; 0 for empty or mismatched vectors
double cosine(const Vector &a, const Vector &b);
}
