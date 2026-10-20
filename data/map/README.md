<!--
SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
SPDX-License-Identifier: CC0-1.0
-->

# Map assets

Installed to `share/kathenaeum/map` and used by the map page (see HACKING.md).

- `layers-light.json`, `layers-dark.json`: the map's layers, generated from Protomaps' basemap style
  (https://github.com/protomaps/basemaps, BSD-3-Clause) by `tools/map-style/generate.mjs`.
- `fonts/`: Noto Sans glyphs as MapLibre reads them, from https://github.com/protomaps/basemaps-assets,
  SIL Open Font License (`fonts/OFL.txt`).
- `sprites/`: map icons from https://github.com/protomaps/basemaps-assets (v4), derived from
  MIT-licensed tangrams/icons (https://github.com/tangrams/icons, © 2017 Mapzen).

Licensing of every file here is recorded in the repository's `REUSE.toml`.

Map data shown with them is © OpenStreetMap contributors (ODbL), from Protomaps' planet build.
