# SPDX-License-Identifier: GPL-3.0-or-later
ecm_add_tests(
    databasetest.cpp
    zimarchivetest.cpp
    LINK_LIBRARIES kathenaeum_core Qt6::Test
)
