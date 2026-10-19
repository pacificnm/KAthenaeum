// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>
#include <QStandardPaths>
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
#include "librarymanager.h"
#include "core/database.h"
#include "kathenaeum-version.h"

namespace
{
// Images QML loads from the network (catalog illustrations) are kept on disk, so scrolling the catalog again or opening it
// offline does not fetch them again.
class CachingNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new QNetworkAccessManager(parent);
        auto *cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("qml-network")));
        cache->setMaximumCacheSize(50 << 20);
        nam->setCache(cache);
        return nam;
    }
};
}

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
    LibraryManager *libraries = LibraryManager::createInstance(&app);
    if (dbError.isEmpty())
        libraries->initialize();

    CachingNetworkFactory networkFactory;  // declared first: the engine uses it until the engine is destroyed
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("KAthenaeum", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
