# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

project(kathenaeum VERSION 0.6.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.8.0")
set(KF_MIN_VERSION "6.13.0")
set(APP_ID "io.github.pacificnm.kathenaeum")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMAddTests)
include(FeatureSummary)

ecm_setup_version(${PROJECT_VERSION} VARIABLE_PREFIX KATHENAEUM VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/src/kathenaeum-version.h)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Sql Network Concurrent WebEngineQuick Test)
qt_policy(SET QTP0001 NEW)  # QML modules live under qrc:/qt/qml/
qt_policy(SET QTP0004 NEW)  # a qmldir for each QML subfolder
# Debian ships Qt's private headers in separate -private-dev packages, but Qt's QML targets still list their folders
add_compile_options(-Wno-missing-include-dirs)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Kirigami CoreAddons I18n Config Crash DBusAddons IconThemes)
find_package(KF6KirigamiAddons 1.7.0 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZIM REQUIRED IMPORTED_TARGET libzim>=9.0)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

install(FILES data/${APP_ID}.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/${APP_ID}.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES data/icons/kathenaeum.svg DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

# Packaging: `cmake --build build --target package` makes a .deb (see CLAUDE.md, "Decisions made")
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "kathenaeum")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline Wikipedia library with a local AI research assistant")
set(CPACK_PACKAGE_CONTACT "Jaimie Garner <pacificnm@gmail.com>")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "education")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Loaded at run time, so dpkg-shlibdeps cannot see them. Ollama is not in Debian: the app explains how to get it.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "aria2, libqt6sql6-sqlite, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtwebengine, qml6-module-org-kde-kirigami, qml6-module-org-kde-kirigamiaddons-formcard, qml6-module-org-kde-coreaddons, qml6-module-org-kde-desktop")
include(CPack)
