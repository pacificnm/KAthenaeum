# KAthenaeum

**A private, offline Wikipedia library with an AI research assistant, as a native KDE application for laptops.**

KAthenaeum is the KDE / Qt version of [Athenaeum](https://github.com/pacificnm/Athenaeum). It turns [Kiwix](https://www.kiwix.org) ZIM files into a searchable library on your own desktop. Ask a question and get an answer written by a small local language model, with every sentence linked back to the Wikipedia article it came from. Nothing leaves your machine: no cloud API, no account, no tracking.

There is nothing to build before you can use a library: it is searchable and ready to answer questions the moment its download finishes.

> **Status: early development.** The Library Manager works: browse the Kiwix catalog, download, verify and manage libraries. Reading, searching and asking are not built yet. See [CLAUDE.md](CLAUDE.md) for the design.

## Features

- **Read** articles from any Wikipedia ZIM, rendered cleanly, following the Plasma colour scheme (light and dark).
- **Search** through the ZIM's own title and word indexes. An exact article title always ranks first.
- **Ask** questions in plain language. Answers stream in, each sentence carries a citation computed by the app (not trusted to the model), and sentences the sources do not support are flagged.
- **Browse** each library from its home page: A to Z, Random article, recently read, and Wikipedia's own overview articles ("Outline of …", "List of …", "Index of …"). All read straight from the ZIM, nothing to build.
- **Chat history**: saved, searchable and renameable.
- **Research projects**: collect articles, selected passages and chats, chat about just that material, and write Markdown documents with AI-drafted summaries. Export to `.md`.
- **Ask about this article**: a chat panel beside every article.
- **Highlights and notes**: saved passages are highlighted in their article with your notes.
- **Library manager**: browse the Kiwix catalog (also offline, from a saved copy), download with progress, cancel and resume, verify every download, spot newer versions, add a ZIM file you already have, see each library's details, choose the download folder, remove safely.
- **Models**: see what Ollama has, test a model, download one, and switch the answer or embedding model without restarting.

The library manager and model settings are ordinary parts of the application, not a separate admin interface, and there is no login: it is your desktop.

## How it works

```
KDE / Qt UI  -->  ZIM files via libzim (C++)   (article text, images, title and word search)
     |
     +--------->  Ollama on this machine       (embeddings and the answer model)
     +--------->  SQLite                       (chats, projects, excerpts, notes, settings)
```

An answer is made in four steps: **find** the best articles through the ZIM's own indexes, **read** their best passages with a small local model, **cite** by matching each answer sentence to the passages by embedding similarity, and **check** for figures or topics the sources do not contain.

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

Some tests read a real library when one is given, and real downloads from kiwix.org run only when asked: `KATHENAEUM_TEST_ZIM=/path/to/wikipedia_en_….zim KATHENAEUM_NETWORK_TESTS=1 ctest --test-dir build`.

## Relationship to Athenaeum

Athenaeum is the web version: a FastAPI server with a React UI and a PostgreSQL + pgvector index, meant to run on a home server with a GPU. KAthenaeum is the desktop version of the same idea without the index, built on Athenaeum's "ZIM search" mode and its citation design, both measured and tested there.

## License

KAthenaeum is free software, released under the [GNU General Public License v3.0 or later](LICENSE). It links against libzim (GPL-3.0-or-later). Wikipedia text and images in ZIM files have their own licenses (mostly CC BY-SA 4.0).
