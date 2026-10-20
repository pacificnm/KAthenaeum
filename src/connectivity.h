// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Whether KAthenaeum may use the internet now: allowed in Settings (on by default) and a connection the system reports.
// Reading, search, Ask and everything else work without it; the library catalog, downloads and the parts of the map not
// kept on this computer need it.
class Connectivity : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool allowed READ allowed WRITE setAllowed NOTIFY changed)
    Q_PROPERTY(bool reachable READ reachable NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)

public:
    static Connectivity *createInstance(QObject *parent); // once, from main()
    static Connectivity *instance();
    static Connectivity *create(QQmlEngine *, QJSEngine *);

    bool allowed() const;
    void setAllowed(bool on);
    bool reachable() const;
    bool online() const
    {
        return allowed() && reachable();
    }

Q_SIGNALS:
    void changed();

private:
    explicit Connectivity(QObject *parent);
    bool m_allowed = true;
};
