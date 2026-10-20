// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mvt.h"

#include <QLineF>
#include <cmath>

namespace Mvt
{
namespace
{
// A protobuf message read field by field
class Reader
{
public:
    explicit Reader(QByteArrayView data)
        : m_data(data)
    {
    }

    bool next()  // reads the next field's key; false at the end or on bad data
    {
        if (m_pos >= m_data.size())
            return false;
        quint64 key = 0;
        if (!varint(key))
            return false;
        m_field = int(key >> 3);
        m_wire = int(key & 7);
        return true;
    }
    bool atEnd() const { return m_pos >= m_data.size(); }
    int field() const { return m_field; }
    int wire() const { return m_wire; }
    bool ok() const { return m_ok; }

    bool varint(quint64 &v)
    {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_pos >= m_data.size())
                return m_ok = false;
            const auto b = quint8(m_data[m_pos++]);
            v |= quint64(b & 0x7f) << shift;
            if (!(b & 0x80))
                return true;
        }
        return m_ok = false;
    }
    QByteArrayView bytes()
    {
        quint64 n = 0;
        if (!varint(n) || n > quint64(m_data.size() - m_pos)) {
            m_ok = false;
            return {};
        }
        const QByteArrayView v = m_data.sliced(m_pos, qsizetype(n));
        m_pos += qsizetype(n);
        return v;
    }
    bool fixed(int size, QByteArrayView &v)
    {
        if (m_pos + size > m_data.size())
            return m_ok = false;
        v = m_data.sliced(m_pos, size);
        m_pos += size;
        return true;
    }
    void skip()
    {
        quint64 v;
        QByteArrayView b;
        switch (m_wire) {
        case 0:
            varint(v);
            break;
        case 1:
            fixed(8, b);
            break;
        case 2:
            bytes();
            break;
        case 5:
            fixed(4, b);
            break;
        default:
            m_ok = false;
            m_pos = m_data.size();
        }
    }

private:
    QByteArrayView m_data;
    qsizetype m_pos = 0;
    int m_field = 0;
    int m_wire = 0;
    bool m_ok = true;
};

QList<quint32> packed(QByteArrayView data, bool &ok)
{
    QList<quint32> out;
    Reader r(data);
    quint64 v = 0;
    while (!r.atEnd()) {
        if (!r.varint(v) || v > UINT32_MAX) {
            ok = false;
            break;
        }
        out.append(quint32(v));
    }
    return out;
}

QVariant value(QByteArrayView data)
{
    Reader r(data);
    while (r.next()) {
        quint64 v = 0;
        QByteArrayView b;
        switch (r.field()) {
        case 1:
            return QString::fromUtf8(r.bytes());
        case 2:
            if (r.fixed(4, b)) {
                float f;
                memcpy(&f, b.data(), 4);
                return double(f);
            }
            break;
        case 3:
            if (r.fixed(8, b)) {
                double d;
                memcpy(&d, b.data(), 8);
                return d;
            }
            break;
        case 4:
            r.varint(v);
            return qint64(v);
        case 5:
            r.varint(v);
            return quint64(v);
        case 6:
            r.varint(v);
            return qint64((v >> 1) ^ (~(v & 1) + 1));
        case 7:
            r.varint(v);
            return v != 0;
        default:
            r.skip();
        }
    }
    return {};
}

qint32 zigzag(quint32 v)
{
    return qint32((v >> 1) ^ (~(v & 1) + 1));
}

QList<QList<QPointF>> geometry(const QList<quint32> &cmds)
{
    QList<QList<QPointF>> parts;
    qint32 x = 0, y = 0;
    for (qsizetype i = 0; i < cmds.size();) {
        const quint32 id = cmds[i] & 7, count = cmds[i] >> 3;
        ++i;
        if (id == 1 || id == 2) {
            for (quint32 k = 0; k < count && i + 1 < cmds.size(); ++k, i += 2) {
                x += zigzag(cmds[i]);
                y += zigzag(cmds[i + 1]);
                if (id == 1 && (parts.isEmpty() || !parts.last().isEmpty()))
                    parts.append(QList<QPointF>());
                if (parts.isEmpty())
                    parts.append(QList<QPointF>());
                parts.last().append(QPointF(x, y));
            }
        } else if (id == 7) {
            if (!parts.isEmpty() && !parts.last().isEmpty())
                parts.last().append(parts.last().first());
        } else {
            break;
        }
    }
    return parts;
}
}

std::optional<QList<Feature>> decode(const QByteArray &tile, const QStringList &layers)
{
    QList<Feature> out;
    Reader t(tile);
    while (t.next()) {
        if (t.field() != 3 || t.wire() != 2) {
            t.skip();
            continue;
        }
        Reader l(t.bytes());
        QString name;
        QStringList keys;
        QVariantList values;
        QList<QByteArrayView> features;
        int extent = 4096;
        while (l.next()) {
            quint64 v = 0;
            switch (l.field()) {
            case 1:
                name = QString::fromUtf8(l.bytes());
                break;
            case 2:
                features.append(l.bytes());
                break;
            case 3:
                keys.append(QString::fromUtf8(l.bytes()));
                break;
            case 4:
                values.append(value(l.bytes()));
                break;
            case 5:
                l.varint(v);
                extent = int(std::clamp<quint64>(v, 1, 1 << 16));
                break;
            default:
                l.skip();
            }
        }
        if (!l.ok())
            return std::nullopt;
        if (!layers.isEmpty() && !layers.contains(name))
            continue;
        for (QByteArrayView fb : std::as_const(features)) {
            Feature f;
            f.layer = name;
            f.extent = extent;
            Reader r(fb);
            bool ok = true;
            while (r.next()) {
                quint64 v = 0;
                switch (r.field()) {
                case 1:
                    r.varint(f.id);
                    break;
                case 2: {
                    const QList<quint32> tags = packed(r.bytes(), ok);
                    for (qsizetype i = 0; i + 1 < tags.size(); i += 2)
                        if (tags[i] < quint32(keys.size()) && tags[i + 1] < quint32(values.size()))
                            f.properties.insert(keys[tags[i]], values[tags[i + 1]]);
                    break;
                }
                case 3:
                    r.varint(v);
                    f.type = v <= 3 ? GeomType(v) : Unknown;
                    break;
                case 4:
                    f.parts = geometry(packed(r.bytes(), ok));
                    break;
                default:
                    r.skip();
                }
            }
            if (!r.ok() || !ok)
                return std::nullopt;
            out.append(f);
        }
    }
    if (!t.ok())
        return std::nullopt;
    return out;
}

bool contains(const Feature &polygon, QPointF p)
{
    if (polygon.type != Polygon)
        return false;
    bool inside = false;
    for (const QList<QPointF> &ring : polygon.parts)
        for (qsizetype i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
            if ((ring[i].y() > p.y()) != (ring[j].y() > p.y())
                && p.x() < (ring[j].x() - ring[i].x()) * (p.y() - ring[i].y()) / (ring[j].y() - ring[i].y()) + ring[i].x())
                inside = !inside;
    return inside;
}

double distance(const Feature &feature, QPointF p)
{
    double best = INFINITY;
    for (const QList<QPointF> &part : feature.parts) {
        if (feature.type == Point || part.size() == 1) {
            for (const QPointF &q : part)
                best = std::min(best, QLineF(p, q).length());
            continue;
        }
        for (qsizetype i = 0; i + 1 < part.size(); ++i) {
            const QPointF a = part[i], b = part[i + 1];
            const QPointF ab = b - a;
            const double len2 = QPointF::dotProduct(ab, ab);
            const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            best = std::min(best, QLineF(p, a + t * ab).length());
        }
    }
    return best;
}
}
