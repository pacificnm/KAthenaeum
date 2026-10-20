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
    maptilestest.cpp
    networktest.cpp
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


# Not a test: askbench measures Ask on real libraries and the local Ollama (see the comment at the top of the file)
if(BUILD_BENCHMARKS)
    add_executable(askbench askbench.cpp)
    target_link_libraries(askbench kathenaeum_core)
endif()

# a test that hangs (a fake server that never answers) fails instead of stopping the run
get_property(tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${tests} PROPERTIES TIMEOUT 300)
