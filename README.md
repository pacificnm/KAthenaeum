# KAthenaeum

**A private, offline Wikipedia library with an AI research assistant, as a native KDE application for laptops.**

KAthenaeum is the KDE / Qt version of [Athenaeum](https://github.com/pacificnm/Athenaeum). It turns [Kiwix](https://www.kiwix.org) ZIM files into a searchable library on your own desktop. Ask a question and get an answer written by a small local language model, with every sentence linked back to the Wikipedia article it came from. Nothing leaves your machine: no cloud API, no account, no tracking.

There is nothing to build before you can use a library: it is searchable and ready to answer questions the moment its download finishes.

> **Status: early development.** Working: the Library Manager (catalog, downloads, installed libraries), reading (library home, A to Z, articles with pictures), search, Ask (answers with citations, follow-up questions, saved chats), Ask about this article, and research projects (sources, notes, a project chat, documents, AI drafts, highlights). See [CLAUDE.md](CLAUDE.md) for the design.

## Features

- **Read** articles from any Wikipedia ZIM, rendered cleanly with their pictures, following the Plasma colour scheme (light and dark), with a contents menu and back/forward. Links to the web open in your browser.
- **Search** every library at once, or one: titles as you type, then the text of every article through the ZIMs' own word indexes, with the matching passage shown. An article the search names ranks first.
- **Ask** questions in plain language. Answers stream in, each sentence carries a citation computed by the app (not trusted to the model) that opens the article at the cited section, and sentences the sources do not support, or figures none of them contain, are flagged for checking. Facts from articles' infoboxes (boiling points, dates of birth) are read too. Ask follow-up questions ("and its density?"), or stop an answer at any time. A question the libraries cannot answer gets "I could not find it", not an answer from the model's memory.
- **Browse** each library from its home page: find an article by title, A to Z, Random article, recently read, and Wikipedia's own overview articles ("Outline of …", "List of …", "Timeline of …"). All read straight from the ZIM, nothing to build.
- **Chat history**: every chat is saved with its sources and citations, and can be reopened, renamed or deleted.
- **Research projects**: collect articles, passages you select in them, answers and chats, each with your notes. A project's own chat answers from just that material (or the whole library too, kept apart), with the project's goal in mind. Write Markdown documents that cite the project's sources, and export a document or the whole project to `.md`. Let the AI draft a summary of the project's articles, or notes from a chat, as a document to edit.
- **Ask about this article**: a chat panel beside every article that answers from that article alone, citing its sections (a click scrolls there). Select text and right-click to ask about it. These chats are saved with their article.
- **Highlights and notes**: passages you save are highlighted in their article, with your notes on hover.
- **Library manager**: browse the Kiwix catalog (also offline, from a saved copy), download with progress, cancel and resume, verify every download, spot newer versions, add a ZIM file you already have, see each library's details, choose the download folder, remove safely.
- **Models**: see whether Ollama is running and has the models, switch the answer or embedding model without restarting, and get told (with the fix) when Ollama is set to unload one model to load the other.

The library manager and model settings are ordinary parts of the application, not a separate admin interface, and there is no login: it is your desktop.

## How it works

```
KDE / Qt UI  -->  ZIM files via libzim (C++)   (article text, images, title and word search)
     |
     +--------->  Ollama on this machine       (embeddings and the answer model)
     +--------->  SQLite                       (chats, projects, excerpts, notes, settings)
```

An answer is made in four steps: **find** the best articles through the ZIM's own indexes, **read** their best passages (ranked by meaning with a small embedding model) with a small local answer model, **cite** by matching each answer sentence to the passages by embedding similarity, and **check** for figures or topics the sources do not contain. With the default models on a laptop iGPU, the first words appear after about 5 seconds and a whole answer takes 9 to 19.

## Requirements (intended)

| | |
|---|---|
| Desktop | KDE Plasma 6 (other Linux desktops should work with the Qt libraries installed) |
| Libraries | Qt 6, KDE Frameworks 6, Kirigami, libzim (C++, 9.x), SQLite |
| [Ollama](https://ollama.com) | Installed on the same machine, with a small answer model and an embedding model |
| aria2 | `aria2c` on the PATH, for downloading ZIM files |
| GPU | Not required. Built for laptop iGPUs and small models. |

KAthenaeum will be distributed as a `.deb` package for Debian. Ollama is not packaged by Debian, so install it separately.

## Building

On Debian 13, install the build dependencies listed in [CLAUDE.md](CLAUDE.md#build-dependencies-debian-13), then:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build                 # unit tests
./build/bin/kathenaeum                 # run without installing
cmake --build build --target package   # make a .deb in build/
```

Some tests read a real library when one is given, real downloads from kiwix.org run only when asked, and so does a check against your local Ollama: `KATHENAEUM_TEST_ZIM=/path/to/wikipedia_en_….zim KATHENAEUM_NETWORK_TESTS=1 KATHENAEUM_OLLAMA_TESTS=1 ctest --test-dir build`.

To measure Ask on your own libraries and models: `./build/bin/askbench [--answer MODEL] [--embed MODEL] [--article PATH] library.zim… < questions.txt` (one question per line; a line starting with `+` is a follow-up). It prints the time of each step, the sources, and each answer with its citations and flags.

## Relationship to Athenaeum

Athenaeum is the web version: a FastAPI server with a React UI and a PostgreSQL + pgvector index, meant to run on a home server with a GPU. KAthenaeum is the desktop version of the same idea without the index, built on Athenaeum's "ZIM search" mode and its citation design, both measured and tested there.

## License

KAthenaeum is free software, released under the [GNU General Public License v3.0 or later](LICENSE). It links against libzim (GPL-3.0-or-later). Wikipedia text and images in ZIM files have their own licenses (mostly CC BY-SA 4.0).
