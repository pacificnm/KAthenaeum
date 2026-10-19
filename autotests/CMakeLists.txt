# SPDX-License-Identifier: GPL-3.0-or-later
ecm_add_tests(
    aria2test.cpp
    articlerenderertest.cpp
    catalogtest.cpp
    databasetest.cpp
    extracttest.cpp
    downloadstest.cpp
    librarytest.cpp
    zimarchivetest.cpp
    zimreadertest.cpp
    LINK_LIBRARIES kathenaeum_core Qt6::Test
)

# Builds its own tiny ZIM with libzim's writer, which reports errors by throwing
ecm_add_test(zimsearchtest.cpp LINK_LIBRARIES kathenaeum_core Qt6::Test PkgConfig::ZIM)
kde_target_enable_exceptions(zimsearchtest PRIVATE)
