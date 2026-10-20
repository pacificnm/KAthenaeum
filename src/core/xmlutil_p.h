// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <libxml/tree.h>

// Reading libxml2 nodes as Qt strings (core-internal: the renderer, the extractor and the dictionary)
namespace XmlUtil
{
// An attribute's value, or an empty string when the node has none
inline QString attr(xmlNodePtr n, const char *name)
{
    xmlChar *v = xmlGetProp(n, reinterpret_cast<const xmlChar *>(name));
    if (!v)
        return {};
    QString s = QString::fromUtf8(reinterpret_cast<const char *>(v));
    xmlFree(v);
    return s;
}

// All the text inside a node, whitespace collapsed
inline QString text(xmlNodePtr n)
{
    xmlChar *t = xmlNodeGetContent(n);
    if (!t)
        return {};
    QString s = QString::fromUtf8(reinterpret_cast<const char *>(t)).simplified();
    xmlFree(t);
    return s;
}
}
