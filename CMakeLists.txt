# SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

project(kathenaeum VERSION 0.17.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.8.0")
set(KF_MIN_VERSION "6.13.0")
set(APP_ID "io.github.pacificnm.kathenaeum")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMAddTests)
include(ECMDeprecationSettings)
include(FeatureSummary)

option(BUILD_BENCHMARKS "Build askbench and mapbench, which measure Ask and map cutting on real data (not tests)" OFF)

ecm_setup_version(${PROJECT_VERSION} VARIABLE_PREFIX KATHENAEUM VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/src/kathenaeum-version.h)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Sql Network Concurrent WebEngineQuick)
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
endif()
qt_policy(SET QTP0001 NEW)  # QML modules live under qrc:/qt/qml/
qt_policy(SET QTP0004 NEW)  # a qmldir for each QML subfolder
# Debian ships Qt's private headers in separate -private-dev packages, but Qt's QML targets still list their folders
add_compile_options(-Wno-missing-include-dirs)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Kirigami CoreAddons I18n Crash DBusAddons IconThemes TextEditor WindowSystem)
find_package(KF6KirigamiAddons 1.7.0 REQUIRED)
find_package(KF6DocTools ${KF_MIN_VERSION})
set_package_properties(KF6DocTools PROPERTIES DESCRIPTION "Builds the KAthenaeum Handbook" TYPE OPTIONAL)

ecm_set_disabled_deprecation_versions(QT 6.8.0 KF 6.13.0)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZIM REQUIRED IMPORTED_TARGET libzim>=9.0)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)
find_package(ZLIB REQUIRED)  # PMTiles directories are gzipped
find_package(Threads REQUIRED)
# libosmium (header-only) reads OpenStreetMap extracts for the places a map can link to Wikipedia
find_path(OSMIUM_INCLUDE_DIR osmium/osm.hpp)
find_path(PROTOZERO_INCLUDE_DIR protozero/pbf_reader.hpp)
if(NOT OSMIUM_INCLUDE_DIR OR NOT PROTOZERO_INCLUDE_DIR)
    message(FATAL_ERROR "libosmium was not found: sudo apt install libosmium2-dev")
endif()
# MapLibre Native for Qt draws the maps. It is not in Debian: build it once (see HACKING.md) and
# pass its prefix in CMAKE_PREFIX_PATH. The app does not link it; its QML plugin and libraries are installed in the
# app's private folder, and the plugin finds the libraries next to it ($ORIGIN/../../lib).
find_package(QMapLibre 4.0 REQUIRED COMPONENTS Core Quick)
get_filename_component(MAPLIBRE_PREFIX "${QMapLibre_DIR}/../../.." ABSOLUTE)
if(NOT EXISTS "${MAPLIBRE_PREFIX}/qml/MapLibre/qmldir")
    message(FATAL_ERROR "MapLibre's QML plugin is not in ${MAPLIBRE_PREFIX}/qml/MapLibre")
endif()
set(KATHENAEUM_PRIVATE_DIR "${KDE_INSTALL_LIBDIR}/kathenaeum")

# The build directory is laid out like the installed tree, so bin/kathenaeum finds the map assets and MapLibre's QML
# plugin at the same paths relative to itself whether it runs from here or from its install prefix
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/${KDE_INSTALL_DATADIR}/kathenaeum ${CMAKE_BINARY_DIR}/${KATHENAEUM_PRIVATE_DIR}/qml
     ${CMAKE_BINARY_DIR}/${KATHENAEUM_PRIVATE_DIR}/lib)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/data/map ${CMAKE_BINARY_DIR}/${KDE_INSTALL_DATADIR}/kathenaeum/map SYMBOLIC)
file(CREATE_LINK ${MAPLIBRE_PREFIX}/qml/MapLibre ${CMAKE_BINARY_DIR}/${KATHENAEUM_PRIVATE_DIR}/qml/MapLibre SYMBOLIC)

add_subdirectory(src)
if(KF6DocTools_FOUND)
    add_subdirectory(doc)
    kdoctools_install(po)  # translated handbooks, once KDE's translation system adds them
endif()
ki18n_install(po)
if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

install(FILES data/${APP_ID}.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/${APP_ID}.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES data/icons/kathenaeum.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
install(DIRECTORY data/map/ DESTINATION ${KDE_INSTALL_DATADIR}/kathenaeum/map PATTERN README.md EXCLUDE)
# MapLibre: its QML plugin, and its two libraries under their sonames
install(DIRECTORY ${MAPLIBRE_PREFIX}/qml/MapLibre DESTINATION ${KATHENAEUM_PRIVATE_DIR}/qml)
foreach(lib QMapLibre QMapLibreQuickPrivate)
    file(REAL_PATH "${MAPLIBRE_PREFIX}/lib/lib${lib}.so.4" real)
    install(FILES ${real} DESTINATION ${KATHENAEUM_PRIVATE_DIR}/lib RENAME lib${lib}.so.4)
    file(CREATE_LINK ${real} ${CMAKE_BINARY_DIR}/${KATHENAEUM_PRIVATE_DIR}/lib/lib${lib}.so.4 SYMBOLIC)
endforeach()

# Licences: the app's and its bundled assets' (data/copyright says which applies to what), and MapLibre's
set(KATHENAEUM_DOC_DIR "${KDE_INSTALL_DATAROOTDIR}/doc/kathenaeum")
install(FILES data/copyright DESTINATION ${KATHENAEUM_DOC_DIR})
install(DIRECTORY LICENSES/ DESTINATION ${KATHENAEUM_DOC_DIR}/licenses)
# MapLibre Native's licence and its own third-party notices (data/thirdparty/maplibre, copied from its source)
install(DIRECTORY data/thirdparty/maplibre DESTINATION ${KATHENAEUM_DOC_DIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

# Packaging: `cmake --build build --target package` makes a .deb
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "kathenaeum")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline Wikipedia library with a local AI research assistant")
set(CPACK_PACKAGE_CONTACT "Jaimie Garner <pacificnm@gmail.com>")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
set(CPACK_SOURCE_IGNORE_FILES "/\\\\.git/" "/build[^/]*/" "\\\\.deb$" "\\\\.zim$" "/node_modules/" "/CLAUDE\\\\.md$" "/FEATURES\\\\.md$")
set(CPACK_DEBIAN_PACKAGE_SECTION "education")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "khelpcenter")  # shows the handbook (Help, F1)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CPACK_PACKAGING_INSTALL_PREFIX}/${KATHENAEUM_PRIVATE_DIR}/lib")
# Loaded at run time, so dpkg-shlibdeps cannot see them. Ollama is not in Debian: the app explains how to get it.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "aria2, libqt6sql6-sqlite, qml6-module-qtcore, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, qml6-module-qtquick-dialogs, qml6-module-qtwebengine, qml6-module-org-kde-kirigami, qml6-module-org-kde-kirigamiaddons-formcard, qml6-module-org-kde-coreaddons, qml6-module-org-kde-desktop")
include(CPack)
