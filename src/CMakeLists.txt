# SPDX-License-Identifier: GPL-3.0-or-later

# Everything that is not UI, so tests can link it without QML.
add_library(kathenaeum_core STATIC
    core/answerer.cpp core/answerer.h
    core/articlerenderer.cpp core/articlerenderer.h
    core/catalog.cpp core/catalog.h
    core/chats.cpp core/chats.h
    core/database.cpp core/database.h
    core/downloads.cpp core/downloads.h
    core/drafts.cpp core/drafts.h
    core/embedding.cpp core/embedding.h
    core/extract.cpp core/extract.h
    core/history.cpp core/history.h
    core/library.cpp core/library.h
    core/ollama.cpp core/ollama.h
    core/projects.cpp core/projects.h
    core/rag.cpp core/rag.h
    core/settings.cpp core/settings.h
    core/zimarchive.cpp core/zimarchive.h
    core/zimarchives_p.h
    core/zimreader.cpp core/zimreader.h
    core/zimsearch.cpp core/zimsearch.h
)
target_include_directories(kathenaeum_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kathenaeum_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network PRIVATE Qt6::Concurrent PkgConfig::ZIM PkgConfig::LIBXML2)
kde_target_enable_exceptions(kathenaeum_core PRIVATE)  # libzim reports errors by throwing; KDE builds with -fno-exceptions

add_executable(kathenaeum
    main.cpp
    appstate.cpp appstate.h
    articlestore.cpp articlestore.h
    chatlist.cpp chatlist.h
    conversation.cpp conversation.h
    documentmodel.cpp documentmodel.h
    draftcontroller.cpp draftcontroller.h
    librarybrowser.cpp librarybrowser.h
    librarymanager.cpp librarymanager.h
    librarymodels.cpp librarymodels.h
    modelsettings.cpp modelsettings.h
    projectlist.cpp projectlist.h
    projectmodel.cpp projectmodel.h
    searchcontroller.cpp searchcontroller.h
    zimschemehandler.cpp zimschemehandler.h
)
target_include_directories(kathenaeum PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(kathenaeum PRIVATE APP_ID="${APP_ID}")

set_source_files_properties(qml/Util.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kathenaeum
    URI KAthenaeum
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LibrariesPage.qml
        qml/AskPage.qml
        qml/ChatView.qml
        qml/ChatsPage.qml
        qml/ProjectsPage.qml
        qml/LibraryManagerPage.qml
        qml/SettingsPage.qml
        qml/InstalledView.qml
        qml/CatalogView.qml
        qml/DownloadsView.qml
        qml/LibraryDetailsDialog.qml
        qml/LibraryIcon.qml
        qml/Util.qml
        qml/LibraryPage.qml
        qml/TitlesPage.qml
        qml/ArticlePage.qml
        qml/SearchPage.qml
        qml/ProjectPicker.qml
        qml/ProjectEditDialog.qml
        qml/ProjectPage.qml
        qml/SourceRow.qml
        qml/DocumentPage.qml
)

target_link_libraries(kathenaeum PRIVATE
    kathenaeum_core
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::WebEngineCore Qt6::WebEngineQuick
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::Crash KF6::DBusAddons KF6::IconThemes
)

install(TARGETS kathenaeum ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
