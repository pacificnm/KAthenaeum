# SPDX-License-Identifier: GPL-3.0-or-later

# Everything that is not UI, so tests can link it without QML.
add_library(kathenaeum_core STATIC
    core/database.cpp core/database.h
    core/zimarchive.cpp core/zimarchive.h
)
target_include_directories(kathenaeum_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kathenaeum_core PUBLIC Qt6::Core Qt6::Sql PRIVATE PkgConfig::ZIM)
kde_target_enable_exceptions(kathenaeum_core PRIVATE)  # libzim reports errors by throwing; KDE builds with -fno-exceptions

add_executable(kathenaeum main.cpp appstate.cpp appstate.h)
target_include_directories(kathenaeum PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(kathenaeum PRIVATE APP_ID="${APP_ID}")

qt_add_qml_module(kathenaeum
    URI KAthenaeum
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LibrariesPage.qml
        qml/AskPage.qml
        qml/ChatsPage.qml
        qml/ProjectsPage.qml
        qml/LibraryManagerPage.qml
        qml/SettingsPage.qml
)

target_link_libraries(kathenaeum PRIVATE
    kathenaeum_core
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::WebEngineQuick
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::Crash KF6::DBusAddons KF6::IconThemes
)

install(TARGETS kathenaeum ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
