<!--
SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
SPDX-License-Identifier: CC0-1.0
-->

# Hacking on KAthenaeum

This file is for contributors: how to build and test KAthenaeum, how the code is organised, and the rules that keep it
safe and working. What the application does is in [README.md](README.md) and in the handbook (`doc/index.docbook`).

## Building

On Debian 13 (Qt 6.8, KDE Frameworks 6.13):

```bash
sudo apt install extra-cmake-modules ninja-build \
  qt6-base-dev qt6-declarative-dev qt6-webengine-dev qt6-svg-dev qt6-tools-dev \
  libkirigami-dev kirigami-addons-dev libkf6coreaddons-dev libkf6i18n-dev libkf6crash-dev \
  libkf6dbusaddons-dev libkf6iconthemes-dev libkf6texteditor-dev libkf6windowsystem-dev \
  libkf6qqc2desktopstyle-dev qml6-module-org-kde-desktop libkf6doctools-dev \
  libzim-dev libxml2-dev libosmium2-dev zlib1g-dev aria2 sqlite3
```

`libkf6doctools-dev` is optional: without it the handbook is not built.

MapLibre Native for Qt draws the maps. It is not packaged by Debian, so build it once and install it to a prefix
(about 2.5 minutes on 16 cores):

```bash
git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/maplibre/maplibre-native-qt.git
cmake -S maplibre-native-qt -B maplibre-native-qt/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMLN_WITH_OPENGL=ON \
      -DMLN_QT_WITH_LOCATION=OFF -DMLN_QT_WITH_WIDGETS=OFF -DCMAKE_INSTALL_PREFIX=$HOME/maplibre-qt
cmake --build maplibre-native-qt/build && cmake --install maplibre-native-qt/build
```

Then:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/maplibre-qt
cmake --build build
ctest --test-dir build                 # the tests
./build/bin/kathenaeum                 # run from the build directory
cmake --build build --target package   # a .deb in build/
```

The build directory is laid out like an installed prefix (`share/kathenaeum/map` and MapLibre's QML plugin are linked
into it), so the program finds its files the same way in both places. No source paths are compiled in.

KAthenaeum is single-instance: if an installed copy is already running, starting the build only raises that window.
Quit it first, or run the build inside `dbus-run-session`.

**Never run a development build on your own data.** A newer build migrates the database, and an older installed build
then refuses to open it. Point the XDG folders somewhere else, with a copy of your database if you want real data:

```bash
mkdir -p /tmp/kath/data/kathenaeum
sqlite3 ~/.local/share/kathenaeum/kathenaeum.sqlite ".backup /tmp/kath/data/kathenaeum/kathenaeum.sqlite"
XDG_DATA_HOME=/tmp/kath/data XDG_CONFIG_HOME=/tmp/kath/config XDG_CACHE_HOME=/tmp/kath/cache ./build/bin/kathenaeum
```

### Flatpak

`.flatpak-manifest.json` builds KAthenaeum with libzim, Xapian, aria2, libosmium, KTextEditor, Kirigami Addons and
MapLibre Native for Qt on KDE's runtime, for KDE's nightly Flatpak builds. **It has not been test-built yet**: run
`flatpak-builder --user --install build-flatpak .flatpak-manifest.json` and fix what fails before relying on it.
Ollama cannot run inside the sandbox; the Flatpak reaches the one installed on the computer over `localhost`.

## Tests

`ctest --test-dir build` runs the unit tests. They are hermetic: they build their own tiny ZIM files with libzim's
writer, talk to fake Ollama servers on local ports, and run with `QStandardPaths` in test mode. Some tests do more when
asked:

| Variable | What it enables |
|---|---|
| `KATHENAEUM_TEST_ZIM=/path/file.zim` | tests that read a real Wikipedia ZIM |
| `KATHENAEUM_NETWORK_TESTS=1` | real downloads from kiwix.org (a 2 MB library; cancel and resume of a 22 MB one) |
| `KATHENAEUM_OLLAMA_TESTS=1` | a check against the Ollama running on this computer |

Two measuring tools are built with `-DBUILD_BENCHMARKS=ON`. They are not tests:

- `askbench [--answer MODEL] [--embed MODEL] [--article PATH] library.zim… < questions.txt` runs Ask on real libraries
  and the local Ollama, one question per line (`+` starts a follow-up), and prints each step's time, the sources and
  the cited answer. Run it before changing a model, a threshold or a prompt, and compare.
- `mapbench URL west south east north maxzoom out.pmtiles` cuts a region out of Protomaps' planet build and reports the
  time and size (`DRY=1` only measures).

Code that parses untrusted bytes with libxml2 or reads PMTiles should also be run under AddressSanitizer, in a separate
build directory: `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`.

## How the code is organised

- `src/core/` is the static library `kathenaeum_core`: everything that is not interface, so tests link it without QML.
  ZIM access (`zimreader`, `zimsearch`, `zimarchive`, `articlerenderer`, `extract`), the database (`database`,
  `settings`, `library`, `chats`, `projects`, `highlights`, `classroom`), the catalog and downloads (`catalog`,
  `downloads`), the assistant (`ollama`, `embedding`, `rag`, `answerer`, `drafts`, `teacher`), study tools (`citations`,
  `dictionary`, `timeline`) and maps (`pmtiles`, `maptiling`, `mapextract`, `mvt`, `maplookup`, `mapplaces`,
  `mapregions`). `network` holds the redirect and size guards every download uses.
- `src/*.cpp` are the objects QML talks to: singletons such as `LibraryManager`, `LibraryBrowser`, `Search`, `Models`,
  `Notes`, `Classroom`, `Drafting`, and models such as `Conversation`, `ProjectModel`, `TimelineModel`.
  `zimschemehandler` serves `zim://` pages to the web view; `documentwindow` is the Markdown editor window (KTextEditor).
- `src/qml/` holds the pages, one file each, with `Main.qml` as the window and `Util.qml` for shared helpers.
- `autotests/` are the QtTest tests, `doc/` the handbook, `data/` the desktop file, AppStream metadata, icon and map
  assets, and `tools/map-style/` regenerates the map style from Protomaps' basemap (`npm ci && node generate.mjs`).

### How a question is answered

1. **Find** articles in each library through the ZIM's own indexes: the title as typed and its case variants, runs of
   words as titles, the title suggestion index, and Xapian full text widened from every word to fewer (libzim's query
   parser has no `OR`). The lists are fused by rank (RRF, k = 60) and re-ranked by how much each article is about the
   question.
2. **Read** the best articles' passages, embed at most 40 of them with the question, and rank by meaning and words.
3. **Answer** with the local model from at most five numbered sources (about 8,000 characters).
4. **Cite**: the app embeds each answer sentence and matches it to the passages. The model is never asked to cite.
   Sentences that match nothing well, or figures no source contains, are flagged for checking.

There is deliberately no prebuilt vector index: everything is read from the ZIM files at question time, so a library
can be used the moment it is downloaded.

## Rules

### Security and privacy

- **Text from outside the application is never shown as rich text.** ZIM metadata, catalog entries, model output,
  article titles, file paths and error messages can contain markup that makes Qt fetch images. Every `Label`, `Heading`
  and `Text` has `textFormat: Text.PlainText`; tooltips, menus, combo boxes and page titles get such text through
  `Util.plain()`; Markdown from a model goes through `Util.markdown()`, which drops images and raw HTML.
- **The interface never reaches the network on its own.** The QML engine's network access manager only allows the
  Kiwix catalog's illustrations (`Catalog::isIllustrationUrl`) and `data:` URLs. Article pages are sanitised
  (`ArticleRenderer`), run without JavaScript, carry a strict Content Security Policy and can only load `zim:` URLs.
- **Downloads go through `Network::guarded()`**: https only, redirects checked against the allowed hosts, sizes capped.
  Maps come only from Protomaps and Geofabrik (`MapCatalog::isAllowedUrl`), libraries only from Kiwix.
- **Everything runs locally.** No hosted AI service is used or offered, and there is no telemetry.

### Correctness

- **libzim reports errors by throwing**, and KDE builds with `-fno-exceptions`. Only `kathenaeum_core` enables
  exceptions; every libzim call is inside it and caught there.
- **Worker threads never touch the database.** Prepare on the main thread (`ZimSearch::prepare`), read archives in the
  worker through `ZimArchives::opened`, and write results back on the main thread.
- **Database migrations** live in `database.cpp`: migration N takes the schema from version N to N+1 (`PRAGMA
  user_version`), each in one transaction. Never edit a migration that has shipped; append one. A database from a newer
  build is refused, never touched.
- **A null `QString` binds as SQL `NULL`.** Text columns are `NOT NULL`: wrap strings that may be empty in
  `Database::text()`, and check the result of every `exec()` and `commit()`.
- **libxml2:** when dropping XPath matches, decide everything before freeing anything (freeing an outer match frees the
  inner ones), and a query libxml2 cannot finish must fail closed.
- **An invalid `QRegularExpression` silently matches nothing.** Every pattern needs a test that exercises it.

### Translations

- Every user-visible string goes through KI18n (`i18n`, `i18nc`, `i18np`). Whole sentences with `%1` placeholders, no
  concatenation; a plural's argument is a number, never a formatted string.
- **Prompts sent to the language model stay in English**, as does any text parsed from its replies: the small models
  the app is built for work best in English. Citations follow English style guides (MLA, APA, Chicago) and stay English
  too. Each such block has a comment saying so.
- `Messages.sh` extracts the strings for KDE's translation system.

### Style

- C++20, Qt 6 and KDE Frameworks conventions, formatted with KDE's clang-format settings: `cmake --build build
  --target clang-format` (with clang-format installed, configuring writes `.clang-format` and installs a pre-commit hook that checks
  formatting).
- The build must stay free of warnings with GCC and Clang, and of clazy findings at levels 0 and 1.
- Comments say why, not what. Match the comment density of the surrounding code.
- Every file carries `SPDX-FileCopyrightText` and `SPDX-License-Identifier` tags, or an entry in `REUSE.toml`; check
  with `reuse lint`.

## Known issues

### MapLibre is bundled

MapLibre Native for Qt is not packaged by Debian (or most distributions), so the build needs it installed to a prefix,
and the package ships its QML plugin and two libraries in a private folder (`<libdir>/kathenaeum`). That is a hurdle for
distributions and for KDE's continuous integration. Options being considered:

1. Make maps optional at build time (`-DWITH_MAPS=OFF`), so the rest of the application builds without MapLibre, and
   ship maps as an add-on.
2. Use a map library that distributions already package (for example Qt Location with a local tile plugin), at the
   cost of rewriting the map page and its style.
3. Keep MapLibre and build it as a dependency in KDE's CI and in the Flatpak manifest, as other KDE applications do
   with libraries distributions lack.

### The application id

The application id is `io.github.pacificnm.kathenaeum`. If the project moves to KDE it becomes `org.kde.kathenaeum`.
These are the places to change together:

- `CMakeLists.txt`: `APP_ID`, and `CPACK_PACKAGE_CONTACT`.
- `data/`: rename `io.github.pacificnm.kathenaeum.desktop` and `io.github.pacificnm.kathenaeum.metainfo.xml`; in the
  metainfo, `<id>`, `<launchable>`, `<developer id>`, the URLs, and add `<provides><id>` with the old id so software
  centres treat it as the same application.
- `src/main.cpp`: `setOrganizationDomain`, the homepage and bug address in `KAboutData` (the D-Bus name and desktop file
  name follow from these and `APP_ID`).
- `data/copyright`, `README.md` and the handbook's links.
- Users' data stays where it is (`~/.local/share/kathenaeum`), since it is named after the program, not the id.
