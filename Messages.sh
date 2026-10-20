#! /usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
# SPDX-License-Identifier: CC0-1.0
# Extracts the translatable strings for KDE's translation system (scripty)
$XGETTEXT `find src -name '*.cpp' -o -name '*.h' -o -name '*.qml'` -o $podir/kathenaeum.pot
