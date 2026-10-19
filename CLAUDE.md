# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this project is

KAthenaeum: a native KDE / Qt desktop version of [Athenaeum](https://github.com/pacificnm/Athenaeum) (offline Kiwix Wikipedia library + local RAG research assistant with app-computed citations), **built for laptops running small local models**. Athenaeum is a FastAPI + React web app for a home server with a prebuilt vector index; this is a single-user desktop application with no index to build.

**Read Athenaeum's [CLAUDE.md](https://github.com/pacificnm/Athenaeum/blob/main/CLAUDE.md) before designing any subsystem** (locally `../Athenaeum/CLAUDE.md` when the two are checked out side by side). It records what was measured, what failed and why. Its "ZIM-search mode" section is the closest thing to this app's whole design. The lessons below are the ones that carry over.

## Status

Skeleton built and the **Library Manager** works (2026-10-19): Kiwix catalog (filter, search, sort, works offline from a cache), downloads with progress, cancel and resume, verification, installed libraries with details, update detection, add an existing ZIM file, remove (optionally deleting the file), a configurable download folder. The Libraries page lists installed libraries; opening one (library home, A-Z) is the next feature. Repository: https://github.com/pacificnm/KAthenaeum.

## Decisions made

- **Target: laptops with small AI models.** Everything must work on an iGPU or CPU with models of a few GB. No step may need hours or days of batch work before the app is useful.
- **No vector index: no PostgreSQL, no pgvector, no intro or full index.** Building one takes too much disk space and time (Athenaeum measured days to months for English Wikipedia, hundreds of GB for a full index). Every library is searched and read through the ZIM itself, exactly as Athenaeum's ZIM-search mode does. Don't add a persistent passage index or a database server without asking.
- **SQLite for user data**: chats and messages, research projects, saved excerpts and notes, documents, settings, the installed-library list and download state. One file in the user's data directory (`QStandardPaths::AppDataLocation`). Embeddings needed for user data (saved excerpts) are small and stored as blobs next to the row they belong to, together with the name of the model that made them.
- **ZIM access: the libzim C++ library, directly** (`zim::Archive` for entries and items, `zim::SuggestionSearcher` for title suggestions, `zim::Searcher` for Xapian full-text). Athenaeum's Python code used python-libzim, a wrapper around the same library, so its behaviour notes apply unchanged. No kiwix-serve, and no going through Athenaeum's FastAPI backend: this is a native C++ application.
- **Ollama is installed locally on this machine** and is the model server, reached over its HTTP API at `http://localhost:11434` (streaming `/api/chat`, `/api/embed`, plus `/api/tags`, `/api/show`, `/api/pull` for the Models page). The URL is a setting, never hard-coded.
- **No separate admin UI, and no login.** Everything that was Athenaeum's admin area (catalog, downloads, library manager, models, log viewer) is an ordinary part of the application, in its main window or in the Settings dialog. There are no admin accounts, passwords, sessions or login throttling. Don't add them back. Destructive actions (delete a library) still ask for confirmation.
- **Browsing a library needs nothing built: everything comes from the ZIM itself.** No topic clustering, no stored article list.
  - **Library home:** title, description, creator, date, language, article count and illustration from the ZIM's metadata (`zim::Archive::getMetadata`, `getIllustrationItem`, `getArticleCount`), plus a search box, Random article, recently read articles from this library, and its hub articles. Never the ZIM's own main page (on selection ZIMs it is just the first ~100 titles alphabetically).
  - **A-Z:** paged straight from the ZIM's title order (`iterByTitle`, and `findByTitle(prefix)` to jump to a letter), so it costs nothing up front and scales to English Wikipedia. **Verified on real ZIMs (libzim 9.2.3, 2026-10-19):** `iterByTitle` yields exactly the front articles (`getArticleCount()`; no images, CSS or JS) **but includes redirects, which were 78% of them** in `chemistry_mini` (33,926 of 43,239; 9,313 real articles), so skip `isRedirect()` entries. Mwoffliner ZIMs have no Category:/Template: pages; titles with a colon ("Chemistry: A European Journal") are real articles, so do not filter on colons. Skip "(disambiguation)" pages. Title order is case-sensitive byte order (UTF-8): `findByTitle` is a case-sensitive prefix match (`"a"` found nothing, `"A"` found 3,672 entries), and quotes, digits and punctuation sort before `A` while lowercase, accented (`É…`) and emoji titles sort after `Z`. Buckets: A-Z from `findByTitle("<letter>")`, and `#` for everything else (before `A` and after `Z`). Page with a cursor (the last title shown, then `findByTitle(cursor)` and skip past it): resuming a page took 0.01 ms. Never compute page *N* by counting from the start. A full title scan took 21 ms for 43,239 entries, so per-letter counts for a big ZIM (~10 s for English Wikipedia's ~20M title entries, estimated) should be computed in the background and cached in SQLite.
  - **Random article:** `getRandomEntry()` returns redirects as often as the title list holds them (4 of 4 in a first try; 4.6 draws per real article on average), so draw again until `!isRedirect()` (cap the attempts). Do not follow the redirect instead: that favours articles with many alternative names.
  - **Hub articles:** Wikipedia's own curated overview pages, already in the ZIM as ordinary articles, found by title prefix: "Outline of …", "List of …", "Index of …", "Glossary of …", "Timeline of …" (skip redirects). `chemistry_mini` has 4 Outline, 148 List, 1 Index, 2 Glossary and 6 Timeline articles; the 340-article `ray-charles` ZIM has 3 List and nothing else, so the home page must read well with few or none, and a big ZIM will have thousands of "List of" pages (page them). The prefix match is literal: "Index of Hydrogen Deficiency" is a redirect, not an index.
- **Default models: `qwen3.5:2b` for answers, `nomic-embed-text` for embeddings and citations**, both changeable in Settings. Chosen on measurements on this laptop; see "Models".
- **Packaging: a `.deb` for now** (Debian 13 first; Flatpak maybe later). Build it from the CMake project with CPack's DEB generator unless that proves too limited. Runtime dependencies come from Debian's own packages (Qt 6, KF6, Kirigami, libzim, SQLite) and `aria2` is a hard dependency. **Ollama is not in Debian**, so it cannot be a package dependency: the app must detect a missing or unreachable Ollama and explain how to install it, and still read and search libraries without it. Install a `.desktop` file, an AppStream metainfo file and icons in the standard places.
- **Its own git repository**: https://github.com/pacificnm/KAthenaeum (public, branch `main`), separate from Athenaeum's. Refer to Athenaeum by its URL, not a relative path.
- **Fully offline**, as in Athenaeum: no hosted LLM or embedding APIs, ever, as a default or an option.

## Stack (decided)

- C++20, Qt 6 (6.8 on Debian 13), KDE Frameworks 6, Kirigami + Kirigami Addons (QML) for the UI, CMake + extra-cmake-modules, Ninja.
- libzim 9 (C++) for ZIM files, found with `pkg-config` (`libzim`).
- Qt SQL with the `QSQLITE` driver for the database.
- Ollama via `QNetworkAccessManager`.
- Article rendering: Qt WebEngine (`WebEngineQuick`), with the sanitising rules of Athenaeum's `render.py`.
- Packaging: CPack DEB generator.

## How a question is answered (carried over from Athenaeum's ZIM-search mode)

- **Find articles** per library, rank-fused (RRF, k=60): the exact title as typed and case variants (ZIM titles are case-sensitive), runs of 1 to 4 words of the question as exact titles ("boiling point"), the title suggestion index (skipped for question-form queries), and Xapian full-text on the content words with widening: every word, then every word but one, then each word alone. **libzim's query parser has no `OR`** (the word is dropped silently) and ANDs every word, so a natural question matches nothing without this. Strip filler words ("why", "does", "work"); skip Category:/Template: and "(disambiguation)" pages.
- **Passages**: read the best ~6 articles from the ZIM, cut them into passages, keep each article's best 6 by word overlap (always the introduction), embed at most 40 with the document prefix, and rank by similarity fused with overlap (RRF). With no embedding model, rank by words alone and mark the answer degraded. About 1 to 2.6 s on Athenaeum's P1000; measure it here.
- **Passage vectors live in memory only**, for the length of the answer: they are what the citations are matched against.
- **Known weakness**: keyword recall only. "Why is the sky blue" finds articles titled "Sky blue", not Rayleigh scattering. Any improvement must be cheap at question time, not a prebuilt index.

## Models (configurable in Settings)

**Defaults (decided): answer `qwen3.5:2b` (send `think: false`), embedding `nomic-embed-text`.** Both are changeable in Settings. Alternative answer model `llama3.2:3b` (tied on quality, terser answers); "better but slower" option `qwen3.5:4b`. Never `ministral-3:3b` (answers from memory).

Measured on this laptop, 2026-10-19 (scripts and logs were in the session scratchpad; rerun them before changing defaults):

**Answer models**, 17 questions on real Wikipedia text (13 articles' first 2,600 characters, three numbered sources per question, ~2,000 tokens), Athenaeum's own system prompt and refusal detector, `num_ctx` 6144, `temperature` 0.2, one run each. 12 questions the sources answer, 5 they do not (the answer is common knowledge, so a model answering from memory shows up):

| Model | Size | Answered right | Said "not in the sources" | Answered from memory | Reading the prompt | Writing | Average answer |
|---|---|---|---|---|---|---|---|
| `qwen3.5:2b` | 2.7 GB | 12/12 | 5/5 | 0 | 2.4 s | 12.3 tok/s | 8.4 s |
| `llama3.2:3b` | 2.0 GB | 12/12 | 5/5 | 0 | 4.0 s | 12.9 tok/s | 5.3 s |
| `qwen3.5:4b` | 3.3 GB | 12/12 | 5/5 * | 0 | 10.7 s | 7.9 tok/s | 19.4 s |
| `ministral-3:3b` | 3.0 GB | 12/12 | 2/5 | **3** | 4.7 s | 12.4 tok/s | 8.9 s |
| `qwen3.5:9b` | 6.6 GB | 11/12 | 5/5 | 0 | 16.7 s | 4.9 tok/s | 31.4 s |

\* The detector scored it 4/5, but its answer ("The provided sources do not state the boiling point…") was a correct refusal: **Athenaeum's refusal regex misses "do not state" (and "give", "list", "specify")**. Widen it in KAthenaeum. `qwen3.5:9b`'s miss: it said the sources had no date for the Berlin Wall's fall, then quoted 9 November 1989 from them. `ministral-3:3b` answered ethanol's boiling point, the Titanic and Marie Curie from memory: **never a default.**

- **Reading the prompt is what the user waits for**, before the first word appears. `qwen3.5:2b` reads ~2,000 tokens in 2.4 s against 4.0 s for `llama3.2:3b` (Qwen3.5's hybrid attention reads faster) but writes longer answers. Keep the source budget at about 8,000 characters and show progress ("Reading 5 sources…").
- 9B models are 3 to 4 times slower: never the default.
- The test is easy (facts in introductions, one run each). Before shipping, add harder questions (facts deep in long articles, two-article questions, follow-ups) once real ZIM passages are available.
- Model load from disk: 3 to 4 s for a 3B on first use.

**Embedding (`nomic-embed-text`, 274 MB, 768 dimensions):**

- Speed: 39 passages of ~800 characters in 1.2 s (the ~40 passages per answer), one question in 0.04 s.
- **Citation thresholds hold:** 13 true sentences each matched their own article first (13/13); best similarity to their own article was 0.78 to 0.91, to any other article 0.61 to 0.70 (median 0.64). Athenaeum's `SUPPORT_MIN` 0.68 and citation threshold 0.70 sit in that gap, so keep them for nomic. Any other embedding model needs them re-measured.
- **Model swapping:** this machine's Ollama runs with `OLLAMA_MAX_LOADED_MODELS=1`, so each question swaps embedding model -> answer model -> embedding model (for citations). Measured cost: ~1.5 s to bring back the answer model (2.8 s the first time), ~0.4 s for the embedding model, so **about 2 s added to every question**. **Plan: run Ollama with 2 loaded models** (both fit easily), which removes this. The answer-model timings above were taken with the model already loaded (no embedding calls in between), so they are what a question costs once both models stay loaded; today each question adds the ~2 s of swapping on top. Tuning Ollama is deferred until the app works; re-measure then. The app cannot change the service: the Models page should detect the swap (via `/api/ps`) and explain how to raise it.
- Do not use the `hf.co/...Uncensored...` models as defaults, and HF GGUF pulls of Qwen3.5 need a renderer/parser set or they run on without stopping.

## Other lessons that must carry over

- **Citations are computed by the app, never by the model.** Embed each answer sentence and match it to the passages that were in the prompt; the best source (and a near tie) becomes the chip. Asking a 3B model to cite made answers worse. Refusals ("I could not find...") get no chips.
- **Unsupported-sentence flags:** best similarity below `SUPPORT_MIN` (0.68 with nomic; re-measure for another embedding model) = topic flag; a figure (>1 digit) found in no source = number flag. It is a "verify this" hint, not a fact-checker.
- **Merge results from different retrievers by rank (RRF), never by raw score.** Raw similarities from different embedding models are not comparable either.
- **Embedding prefixes per model family** (nomic: `search_document: ` / `search_query: `; embeddinggemma, e5, bge, mxbai, arctic have their own; see Athenaeum's `models.py`). Store the model name with every saved vector.
- **Thinking models** need `think: false`, or they spend the token budget on hidden reasoning and return nothing.
- **A download is complete only after verification:** no `.aria2` sidecar, size within the catalog figure (the catalog rounds up to the KiB), opens as a ZIM, internal checksum passes. aria2c must die with its parent (`PR_SET_PDEATHSIG`); a partial download resumes with `-c`.
- **Never render ZIM HTML without sanitising it** (scripts, styles, forms, navboxes, event handlers, risky inline styles dropped; internal links rewritten).
- **Only Wikipedia-style (mwoffliner) ZIMs can be searched and read for answers**; say so plainly for others.
- **Don't send a ZIM's own main page to users as a library home**: on Wikipedia selection ZIMs it is just the first ~100 titles alphabetically.
- Leaving a chat mid-answer must save the partial answer as stopped.

## Environment (verified on this machine, 2026-10-19)

- Dell 16 Plus DB16255 laptop, Debian 13, KDE Plasma 6.3.6, 30 GB RAM.
- GPU: AMD Radeon iGPU (amdgpu, gfx1152), **not** the Quadro P1000 Athenaeum was built on. Ollama 0.32.15 needs `OLLAMA_IGPU_ENABLE=1` to use it (Vulkan); effective bandwidth ~31 GB/s, so tok/s is roughly 31 / model size in GB.
- Installed: cmake 3.31.6, g++ 14.2, Kirigami 6.13 and Kirigami Addons 1.7 QML modules (runtime only), Python 3.13.5.
- **Build dependencies installed and verified 2026-10-19** (see "Build dependencies"): a CMake probe found Qt 6.8.2, Kirigami 6.13.0, Kirigami Addons 1.7.0 and libzim 9.2.3 (Xapian 1.4.25, zstd, lzma, ICU 76); the `QSQLITE` driver opens SQLite 3.46.1; `aria2c` and `sqlite3` are on the PATH.
- **libzim header gotcha:** `zim/version.h` uses `std::cout` as a default argument without including `<iostream>`; include `<iostream>` before it.
- ZIM metadata seen on mwoffliner ZIMs: Title, Description, Creator, Publisher, Date, Language, Name, Flavour, Source, Scraper, Tags, Counter, and a 48x48 PNG illustration (`Illustration_48x48@1`); the main entry is `mainPage`.
- Ollama models present include `qwen3.5:2b`, `qwen3.5:4b`, `qwen3.5:9b`, `llama3.2:3b`, `ministral-3:3b`, `qwen3.8:27b`, and the embedding model `nomic-embed-text` (pulled 2026-10-19).

## Build dependencies (Debian 13)

```bash
sudo apt install extra-cmake-modules ninja-build \
  qt6-base-dev qt6-declarative-dev qt6-webengine-dev qt6-svg-dev qt6-tools-dev \
  libkirigami-dev kirigami-addons-dev libkf6coreaddons-dev libkf6i18n-dev libkf6config-dev \
  libkf6iconthemes-dev libkf6crash-dev libkf6dbusaddons-dev libkf6qqc2desktopstyle-dev qml6-module-org-kde-desktop \
  libzim-dev aria2 sqlite3
```

Already present on this machine: cmake, g++ 14, pkg-config, `libsqlite3-dev`, `libqt6sql6-sqlite` (the `QSQLITE` driver), `qml6-module-qtwebengine`, dpkg-dev (for CPack DEB), appstream and desktop-file-utils (to validate the metainfo and `.desktop` files). Kirigami's dev package is `libkirigami-dev` (6.13), not `libkf6kirigami-dev`. Installed on this machine 2026-10-19 (101 new packages).

## Layout and running

- App id **`io.github.pacificnm.kathenaeum`** (desktop file, AppStream id, D-Bus name). Icon name `kathenaeum` (placeholder SVG in `data/icons/`). QML module URI `KAthenaeum`.
- `CMakeLists.txt` — ECM + Qt 6 + KF6, libzim via pkg-config, install rules for the desktop/metainfo/icon, CPack DEB settings (runtime QML modules are listed in `CPACK_DEBIAN_PACKAGE_DEPENDS` by hand: dpkg-shlibdeps cannot see them).
- `src/core/` — static library `kathenaeum_core`, everything that is not UI, so tests link it without QML: `database.*` (SQLite open + migrations, `Database::text`), `settings.*` (key/value settings, library folder), `zimarchive.*` (libzim wrapper: metadata, `verify`), `catalog.*` (OPDS parser, URL/filename rules, `CatalogFeed` cache), `library.*` (installed libraries: add, scan, remove), `downloads.*` (aria2c runner, `DownloadManager`).
- `src/librarymanager.*` (QML singleton `LibraryManager`, the one object the Library Manager UI talks to; created in `main()` as a child of the app so running aria2c processes are stopped before the app goes) and `src/librarymodels.*` (`InstalledModel`, `CatalogModel`, `DownloadsModel`).
- `src/main.cpp` (startup: WebEngine init, desktop style, KAboutData, KCrash, single instance via KDBusService, database, library manager, QML engine with a disk-cached network factory for catalog images), `src/appstate.*` (QML singleton `AppState`), `src/qml/` (Main.qml, one file per page, Library Manager tabs `InstalledView`/`CatalogView`/`DownloadsView`, `LibraryDetailsDialog`, `LibraryIcon`, singleton `Util.qml` for shared wording).
- `autotests/` — QtTest via `ecm_add_tests`: aria2 output parsing, catalog parsing and URL rules (fixture `autotests/data/catalog_sample.xml`, real entries), database, downloads, library store, ZIM reader. ZIM files are never committed: tests that need one read `KATHENAEUM_TEST_ZIM` and skip without it; real downloads from kiwix.org (a 2 MB library, and cancel/resume of a 22 MB one) run only with `KATHENAEUM_NETWORK_TESTS=1`. Full run: `KATHENAEUM_TEST_ZIM=/path/wikipedia_en_ray-charles_nopic_2026-08.zim KATHENAEUM_NETWORK_TESTS=1 ctest --test-dir build`. ECM also adds an AppStream validation test.
- Build and run: `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build && ./build/bin/kathenaeum`. `.deb`: `cmake --build build --target package`.
- User data: `~/.local/share/kathenaeum/kathenaeum.sqlite` (WAL mode).
- **Database migrations** (`database.cpp`, `migrations`): migration N takes the schema from version N to N+1, tracked in `PRAGMA user_version`, each in one transaction. Never edit a migration that has shipped; append one. QSQLITE runs one statement per `exec()`, so a migration is a list of statements. A database from a newer build is refused rather than touched. Schema v1: `settings`, `libraries`; v2: library metadata columns and `downloads`. Add tables when the feature that needs them is built.
- **libzim throws exceptions, and KDE compiles with `-fno-exceptions`.** `kathenaeum_core` enables them (`kde_target_enable_exceptions`); every libzim call must be inside it and caught there, never let a libzim exception reach Qt or QML code.
- **A null `QString` binds as SQL NULL.** Our text columns are `NOT NULL DEFAULT ''`, so binding `QString()` makes the statement fail, silently unless its result is checked (a download stayed "queued" forever because of it). Wrap strings that may be empty in `Database::text()` and check `exec()`.
- **`i18np`'s plural argument must be a number.** Passing a formatted string ("11,901") made every count over 999 read "0 articles". KI18n adds the thousands separator itself.
- **`ComboBox.indexOfValue()` in a `currentIndex` binding is not re-run when the model changes**, so the box showed "All kinds" while filtering on Wikipedia. Compute the index from the model array (`model.findIndex(...)`).
- **Screenshots for UI checks:** `QT_QPA_PLATFORM=offscreen`, separate `XDG_DATA_HOME`/`XDG_CACHE_HOME`/`XDG_CONFIG_HOME`, and a scratch copy of `Main.qml` with a `Timer` that drives the pages and calls `pageStack.grabToImage(...)` (the window's own content item has no QML engine and cannot be grabbed). The app is single-instance: make sure it is not already running, or the test copy just raises it and exits.
- The desktop style's `TabButton.qml:22: TypeError: Cannot read property 'y' of null` comes from KDE's style (a `baselineOffset` binding before `contentItem` exists): harmless.
- **Debian ships Qt's private headers separately**, but Qt's QML targets still list their folders, so `-Wno-missing-include-dirs` is set globally.
- `qmllint` (`cmake --build build --target kathenaeum_qmllint`) reports `i18n` and Kirigami grouped properties (`icon.name`) as unqualified/unresolved: false positives in KDE apps. Check pages by opening them instead. Known harmless runtime warnings: "Created graphical object was not placed in the graphics scene" when the page stack swaps pages, and `MessageDialog.qml: Cannot read property 'Success'` from Kirigami Addons' own About page.


## Library Manager (`core/catalog.*`, `core/downloads.*`, `core/library.*`, `librarymanager.*`, `qml/*View.qml`)

- **Catalog:** Kiwix OPDS v2, `https://opds.library.kiwix.org/catalog/v2/entries?lang=eng&count=5000`, one ~1.5 MB response, filtered and sorted locally. Cached in `<CacheLocation>/catalog-eng.xml` (written with `QSaveFile`), used for an hour, and kept when a fetch fails so the catalog works offline (the page says it is showing an older copy). **It is fetched only when the Library Manager page opens**, never at startup. Parsed with `QXmlStreamReader` (no external entities, entity expansion limited: tested with an entity bomb).
- **What the catalog holds** (2026-10-19): 1,312 English entries, only 65 of them Wikipedia; 574 have no category (DevDocs, maps, ...), 357 TED, 181 Stack Exchange. Each library appears once per flavour (mini, nopic, maxi). The Kind filter starts on Wikipedia; other kinds get a notice that they may not be searchable. The `_ftindex:no` tag means the ZIM has no Xapian full-text index, which ZIM search needs: such entries get a warning icon, and installed ones a warning.
- **Downloads:** `aria2c -x8 -s8 -c --file-allocation=none --auto-file-renaming=false --follow-metalink=mem --summary-interval=1 --show-console-readout=false --console-log-level=warn --dir <folder> <url.meta4>`. Verified behaviour with aria2 1.37: `--follow-metalink=mem` leaves no `.meta4` beside the ZIM; progress lines look like `[#85a9a7 1.1MiB/22MiB(5%) CN:1 DL:592KiB ETA:36s]` (the first has no ETA; Kiwix metalinks give `CN:1` whatever `-x` says); **SIGTERM makes aria2c exit 7 and keep its `.aria2` control file, and `-c` resumes from it** (cancel = SIGTERM, so a cancelled download can be resumed); a finished file was 178 bytes smaller than the catalog's figure (rounded up to the KiB). The child gets `PR_SET_PDEATHSIG` via `QProcess::setChildProcessModifier`. Statuses `queued | downloading | verifying | complete | failed | cancelled`; a unique partial index allows one active download per file; rows left active by a previous run become `failed` with "Press Resume" at startup (`reconcile`). Before starting: URL rules, file name rules, not already installed, and free space for what is still to come plus 64 MB.
- **Verification** (`ZimArchive::verify`, in a `QtConcurrent` thread, status `verifying`): no `.aria2` sidecar, size within the catalog figure (up to 1023 bytes smaller), opens as a ZIM, `Archive::check()` passes (reads the whole file: minutes for a big one). A file that fails is deleted and the row says why; it is never registered. Removing a download row never deletes a file that an installed library uses.
- **Installed libraries** (`libraries` table, schema v2): registered by canonical path, so a ZIM can live anywhere (Add File keeps it where it is). Metadata, size and the 48x48 illustration are stored when it is added. A missing file keeps its row (projects may refer to it) and is shown as missing. Remove asks, with "also delete the file" checked for files in the download folder and unchecked for files added from elsewhere. **Updates:** a catalog entry with the same base name (`wikipedia_en_chemistry_mini`) and a newer `YYYY-MM` version than an installed file; the update is an ordinary download beside the old file, which the user then removes.
- **Download folder:** setting `library.folder`, default `<AppDataLocation>/zim` (`~/.local/share/kathenaeum/zim`), shown with free space in the Installed tab and in Settings. Changing it does not move files; libraries keep their paths and the new folder is scanned.
- **Catalog illustrations** are loaded by QML straight from the catalog server; the QML engine's network manager has a 50 MB disk cache (`<CacheLocation>/qml-network`) so they are not fetched again. Installed libraries' illustrations come from the database as `data:` URLs.
- **Checked end to end** in a real (offscreen) run with isolated XDG folders: catalog loaded (1,312 entries, 65 Wikipedia), searched, a real download of the 2 MB Ray Charles library verified and registered, screenshots of every tab reviewed.

## Open decisions (ask before assuming)

None at the moment.

## Conventions

- When creating the git repo, add a `.gitignore` for build directories (`build/`, `build-*/`), `*.deb` and CPack output, editor files, and any local ZIM files or test databases.
- Prefer the simplest thing that fits. Don't add services (a database server, a vector DB, kiwix-serve, a task queue) without asking.
- Nothing may assume a discrete GPU or a big model.
- Keep this file current when decisions change; move items from "Open decisions" to "Decisions made".
