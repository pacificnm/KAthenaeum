# SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# every test runs with QStandardPaths in test mode (see testmode.cpp)
add_library(kathenaeum_testmode OBJECT testmode.cpp)
target_link_libraries(kathenaeum_testmode PRIVATE Qt6::Core)

ecm_add_tests(
    aria2test.cpp
    articlerenderertest.cpp
    catalogtest.cpp
    chatstest.cpp
    citationstest.cpp
    classroomtest.cpp
    databasetest.cpp
    dictionarytest.cpp
    extracttest.cpp
    downloadstest.cpp
    draftstest.cpp
    highlightstest.cpp
    librarytest.cpp
    ollamatest.cpp
    projectstest.cpp
    ragtest.cpp
    timelinetest.cpp
    zimarchivetest.cpp
    zimreadertest.cpp
    LINK_LIBRARIES kathenaeum_core kathenaeum_testmode Qt6::Test
)

# These build their own tiny ZIMs with libzim's writer, which reports errors by throwing
ecm_add_test(zimsearchtest.cpp LINK_LIBRARIES kathenaeum_core kathenaeum_testmode Qt6::Test Qt6::Concurrent PkgConfig::ZIM)
kde_target_enable_exceptions(zimsearchtest PRIVATE)
ecm_add_test(answerertest.cpp LINK_LIBRARIES kathenaeum_core kathenaeum_testmode Qt6::Test Qt6::Network PkgConfig::ZIM)
kde_target_enable_exceptions(answerertest PRIVATE)

ecm_add_test(libraryidstest.cpp LINK_LIBRARIES kathenaeum_core kathenaeum_testmode Qt6::Test PkgConfig::ZIM)
kde_target_enable_exceptions(libraryidstest PRIVATE)

# The document window is app code (widgets): built from the app's sources it needs
ecm_add_test(documentwindowtest.cpp ../src/documentwindow.cpp ../src/documentmodel.cpp ../src/projectlist.cpp ../src/projectmodel.cpp
             TEST_NAME documentwindowtest
             LINK_LIBRARIES kathenaeum_core kathenaeum_testmode Qt6::Test Qt6::Widgets Qt6::Qml KF6::TextEditor KF6::I18n)
target_include_directories(documentwindowtest PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_tests_properties(documentwindowtest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Maps; writes a small OpenStreetMap file with libosmium, which reports errors by throwing
ecm_add_test(maptest.cpp LINK_LIBRARIES kathenaeum_core kathenaeum_testmode Qt6::Test Qt6::Sql ZLIB::ZLIB Threads::Threads)
target_include_directories(maptest SYSTEM PRIVATE ${OSMIUM_INCLUDE_DIR} ${PROTOZERO_INCLUDE_DIR})
kde_target_enable_exceptions(maptest PRIVATE)

# Not tests: askbench measures Ask on real libraries and the local Ollama, mapbench cuts a region out of Protomaps'
# planet build (see the comment at the top of each file)
if(BUILD_BENCHMARKS)
    add_executable(askbench askbench.cpp)
    target_link_libraries(askbench kathenaeum_core)
    add_executable(mapbench mapbench.cpp)
    target_link_libraries(mapbench kathenaeum_core Qt6::Network)
endif()

# a test that hangs (a fake server that never answers) fails instead of stopping the run
get_property(tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${tests} PROPERTIES TIMEOUT 300)
