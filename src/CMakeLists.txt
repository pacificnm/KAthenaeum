# SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# Everything that is not UI, so tests can link it without QML.
add_library(kathenaeum_core STATIC
    core/answerer.cpp core/answerer.h
    core/articlerenderer.cpp core/articlerenderer.h
    core/catalog.cpp core/catalog.h
    core/chats.cpp core/chats.h
    core/citations.cpp core/citations.h
    core/classroom.cpp core/classroom.h
    core/database.cpp core/database.h
    core/dictionary.cpp core/dictionary.h
    core/downloads.cpp core/downloads.h
    core/drafts.cpp core/drafts.h
    core/embedding.cpp core/embedding.h
    core/extract.cpp core/extract.h
    core/highlights.cpp core/highlights.h
    core/history.cpp core/history.h
    core/library.cpp core/library.h
    core/mapextract.cpp core/mapextract.h
    core/maplookup.cpp core/maplookup.h
    core/mapplaces.cpp core/mapplaces.h
    core/mapregions.cpp core/mapregions.h
    core/maptiling.cpp core/maptiling.h
    core/mvt.cpp core/mvt.h
    core/network.cpp core/network.h
    core/ollama.cpp core/ollama.h
    core/pmtiles.cpp core/pmtiles.h
    core/projects.cpp core/projects.h
    core/rag.cpp core/rag.h
    core/settings.cpp core/settings.h
    core/teacher.cpp core/teacher.h
    core/timeline.cpp core/timeline.h
    core/zimarchive.cpp core/zimarchive.h
    core/zimarchives_p.h
    core/zimreader.cpp core/zimreader.h
    core/zimsearch.cpp core/zimsearch.h
)
target_include_directories(kathenaeum_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(kathenaeum_core SYSTEM PRIVATE ${OSMIUM_INCLUDE_DIR} ${PROTOZERO_INCLUDE_DIR})
target_link_libraries(kathenaeum_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network PRIVATE Qt6::Concurrent PkgConfig::ZIM PkgConfig::LIBXML2 ZLIB::ZLIB Threads::Threads)
kde_target_enable_exceptions(kathenaeum_core PRIVATE)  # libzim reports errors by throwing; KDE builds with -fno-exceptions

add_executable(kathenaeum
    main.cpp
    appstate.cpp appstate.h
    articlestore.cpp articlestore.h
    chatlist.cpp chatlist.h
    classroomcontroller.cpp classroomcontroller.h
    conversation.cpp conversation.h
    documentmodel.cpp documentmodel.h
    documentwindow.cpp documentwindow.h
    draftcontroller.cpp draftcontroller.h
    librarybrowser.cpp librarybrowser.h
    librarymanager.cpp librarymanager.h
    librarymodels.cpp librarymodels.h
    mapmanager.cpp mapmanager.h
    modelsettings.cpp modelsettings.h
    notes.cpp notes.h
    projectlist.cpp projectlist.h
    projectmodel.cpp projectmodel.h
    searchcontroller.cpp searchcontroller.h
    timelinemodel.cpp timelinemodel.h
    zimschemehandler.cpp zimschemehandler.h
)
target_include_directories(kathenaeum PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# install paths relative to the prefix (bin/..), the same in the build directory (see the top-level CMakeLists.txt)
target_compile_definitions(kathenaeum PRIVATE APP_ID="${APP_ID}"
    KATHENAEUM_MAP_ASSETS="${KDE_INSTALL_DATADIR}/kathenaeum/map"
    KATHENAEUM_MAPLIBRE_QML="${KATHENAEUM_PRIVATE_DIR}/qml")

set_source_files_properties(qml/Util.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kathenaeum
    URI KAthenaeum
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/CitationDialog.qml
        qml/ClassroomPage.qml
        qml/CoursePage.qml
        qml/CourseProjectDialog.qml
        qml/LessonPage.qml
        qml/NewCourseDialog.qml
        qml/ProfileDialog.qml
        qml/QuizView.qml
        qml/TimelinePage.qml
        qml/UnitTestPage.qml
        qml/DefinitionPopup.qml
        qml/HighlightCard.qml
        qml/NotesPage.qml
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
        qml/MapPage.qml
        qml/MapRegionsPage.qml
        qml/RegionSilhouette.qml
        qml/Sidebar.qml
        qml/ArticleChip.qml
        qml/AppButton.qml
        qml/SegmentedTabs.qml
)

target_link_libraries(kathenaeum PRIVATE
    kathenaeum_core
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::WebEngineCore Qt6::WebEngineQuick
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::Crash KF6::DBusAddons KF6::IconThemes KF6::TextEditor KF6::WindowSystem
)

install(TARGETS kathenaeum ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
