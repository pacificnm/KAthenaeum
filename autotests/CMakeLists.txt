# SPDX-License-Identifier: GPL-3.0-or-later
ecm_add_tests(
    aria2test.cpp
    articlerenderertest.cpp
    catalogtest.cpp
    chatstest.cpp
    databasetest.cpp
    extracttest.cpp
    downloadstest.cpp
    librarytest.cpp
    ollamatest.cpp
    ragtest.cpp
    zimarchivetest.cpp
    zimreadertest.cpp
    LINK_LIBRARIES kathenaeum_core Qt6::Test
)

# These build their own tiny ZIMs with libzim's writer, which reports errors by throwing
ecm_add_test(zimsearchtest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test PkgConfig::ZIM)
kde_target_enable_exceptions(zimsearchtest PRIVATE)
ecm_add_test(answerertest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test Qt6::Network PkgConfig::ZIM)
kde_target_enable_exceptions(answerertest PRIVATE)

# Not a test: measures Ask on real libraries and the local Ollama (see the comment at the top of the file)
add_executable(askbench askbench.cpp)
target_link_libraries(askbench kathenaeum_core)
