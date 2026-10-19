// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtWebEngineQuick>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include "appstate.h"
#include "core/database.h"
#include "kathenaeum-version.h"

int main(int argc, char *argv[])
{
    KIconTheme::initTheme();
    QtWebEngineQuick::initialize();  // must run before the application object exists
    QApplication app(argc, argv);    // QApplication, not QGuiApplication: the desktop style needs widgets

    // Follow Plasma's look when the user has not chosen a Qt Quick style themselves
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    KLocalizedString::setApplicationDomain("kathenaeum");

    KAboutData about(QStringLiteral("kathenaeum"),
                     i18n("KAthenaeum"),
                     QStringLiteral(KATHENAEUM_VERSION_STRING),
                     i18n("A private, offline Wikipedia library with an AI research assistant"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 Jaimie Garner"));
    about.addAuthor(i18n("Jaimie Garner"));
    about.setHomepage(QStringLiteral("https://github.com/pacificnm/KAthenaeum"));
    about.setDesktopFileName(QStringLiteral(APP_ID));
    about.setOrganizationDomain("pacificnm.github.io");
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kathenaeum")));

    KCrash::initialize();
    KDBusService service(KDBusService::Unique);  // a second launch raises the running window

    QString dbError;
    const QString dbPath = Database::defaultPath();
    Database::open(dbPath, &dbError);
    AppState::instance()->setDatabase(dbPath, dbError);

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("KAthenaeum", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
