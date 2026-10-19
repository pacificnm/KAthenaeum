# SPDX-License-Identifier: GPL-3.0-or-later
ecm_add_tests(
    aria2test.cpp
    articlerenderertest.cpp
    catalogtest.cpp
    databasetest.cpp
    downloadstest.cpp
    librarytest.cpp
    zimarchivetest.cpp
    zimreadertest.cpp
    LINK_LIBRARIES kathenaeum_core Qt6::Test
)
