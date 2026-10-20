# SPDX-License-Identifier: GPL-3.0-or-later
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
    zimarchivetest.cpp
    zimreadertest.cpp
    LINK_LIBRARIES kathenaeum_core Qt6::Test
)

# These build their own tiny ZIMs with libzim's writer, which reports errors by throwing
ecm_add_test(zimsearchtest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test Qt6::Concurrent PkgConfig::ZIM)
kde_target_enable_exceptions(zimsearchtest PRIVATE)
ecm_add_test(answerertest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test Qt6::Network PkgConfig::ZIM)
kde_target_enable_exceptions(answerertest PRIVATE)

# Not a test: measures Ask on real libraries and the local Ollama (see the comment at the top of the file)
add_executable(askbench askbench.cpp)
target_link_libraries(askbench kathenaeum_core)
ecm_add_test(libraryidstest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test PkgConfig::ZIM)
kde_target_enable_exceptions(libraryidstest PRIVATE)

# The document window is app code (widgets): built from the app's sources it needs
ecm_add_test(documentwindowtest.cpp ../src/documentwindow.cpp ../src/documentmodel.cpp ../src/projectlist.cpp ../src/projectmodel.cpp
             TEST_NAME documentwindowtest
             LINK_LIBRARIES kathenaeum_core Qt6::Test Qt6::Widgets Qt6::Qml KF6::TextEditor KF6::I18n)
target_include_directories(documentwindowtest PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_tests_properties(documentwindowtest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Maps; writes a small OpenStreetMap file with libosmium, which reports errors by throwing
ecm_add_test(maptest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test Qt6::Sql ZLIB::ZLIB Threads::Threads)
target_include_directories(maptest SYSTEM PRIVATE ${OSMIUM_INCLUDE_DIR} ${PROTOZERO_INCLUDE_DIR})
kde_target_enable_exceptions(maptest PRIVATE)

# Not a test: cuts a region out of Protomaps' planet build (see the comment at the top of the file)
add_executable(mapbench mapbench.cpp)
target_link_libraries(mapbench kathenaeum_core Qt6::Network)
