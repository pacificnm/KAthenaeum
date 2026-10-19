# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this project is

KAthenaeum: a native KDE / Qt desktop version of [Athenaeum](https://github.com/pacificnm/Athenaeum) (offline Kiwix Wikipedia library + local RAG research assistant with app-computed citations), **built for laptops running small local models**. Athenaeum is a FastAPI + React web app for a home server with a prebuilt vector index; this is a single-user desktop application with no index to build.

**Read Athenaeum's [CLAUDE.md](https://github.com/pacificnm/Athenaeum/blob/main/CLAUDE.md) before designing any subsystem** (locally `../Athenaeum/CLAUDE.md` when the two are checked out side by side). It records what was measured, what failed and why. Its "ZIM-search mode" section is the closest thing to this app's whole design. The lessons below are the ones that carry over.

## Status

Skeleton built (2026-10-19): the app starts with a Kirigami window, a sidebar of empty pages, Settings showing the database, and an About page; the SQLite database is created and migrated; libzim metadata is read. Unit tests pass. Repository: https://github.com/pacificnm/KAthenaeum. No features yet.

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
- `src/core/` — static library `kathenaeum_core`, everything that is not UI, so tests link it without QML: `database.*` (SQLite open + migrations), `zimarchive.*` (libzim wrapper).
- `src/main.cpp` (startup: WebEngine init, desktop style, KAboutData, KCrash, single instance via KDBusService, database, QML), `src/appstate.*` (QML singleton `AppState`), `src/qml/` (Main.qml and one file per page).
- `autotests/` — QtTest via `ecm_add_tests`. ZIM files are never committed: tests that need one read `KATHENAEUM_TEST_ZIM` and skip without it. ECM also adds an AppStream validation test.
- Build and run: `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build && ./build/bin/kathenaeum`. `.deb`: `cmake --build build --target package`.
- User data: `~/.local/share/kathenaeum/kathenaeum.sqlite` (WAL mode).
- **Database migrations** (`database.cpp`, `migrations`): migration N takes the schema from version N to N+1, tracked in `PRAGMA user_version`, each in one transaction. Never edit a migration that has shipped; append one. QSQLITE runs one statement per `exec()`, so a migration is a list of statements. A database from a newer build is refused rather than touched. Schema v1 is only `settings` and `libraries`; add tables when the feature that needs them is built.
- **libzim throws exceptions, and KDE compiles with `-fno-exceptions`.** `kathenaeum_core` enables them (`kde_target_enable_exceptions`); every libzim call must be inside it and caught there, never let a libzim exception reach Qt or QML code.
- **Debian ships Qt's private headers separately**, but Qt's QML targets still list their folders, so `-Wno-missing-include-dirs` is set globally.
- `qmllint` (`cmake --build build --target kathenaeum_qmllint`) reports `i18n` and Kirigami grouped properties (`icon.name`) as unqualified/unresolved: false positives in KDE apps. Check pages by opening them instead. Known harmless runtime warnings: "Created graphical object was not placed in the graphics scene" when the page stack swaps pages, and `MessageDialog.qml: Cannot read property 'Success'` from Kirigami Addons' own About page.

## Open decisions (ask before assuming)

None at the moment.

## Conventions

- When creating the git repo, add a `.gitignore` for build directories (`build/`, `build-*/`), `*.deb` and CPack output, editor files, and any local ZIM files or test databases.
- Prefer the simplest thing that fits. Don't add services (a database server, a vector DB, kiwix-serve, a task queue) without asking.
- Nothing may assume a discrete GPU or a big model.
- Keep this file current when decisions change; move items from "Open decisions" to "Decisions made".
