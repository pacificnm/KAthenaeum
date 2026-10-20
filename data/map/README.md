# Map assets

Installed to `share/kathenaeum/map` and used by the map page (see CLAUDE.md, "Maps").

- `layers-light.json`, `layers-dark.json`: the map's layers, generated from Protomaps' basemap style
  (https://github.com/protomaps/basemaps, BSD-3-Clause) by `tools/map-style/generate.mjs`.
- `fonts/`: Noto Sans glyphs as MapLibre reads them, from https://github.com/protomaps/basemaps-assets,
  SIL Open Font License (`fonts/OFL.txt`).
- `sprites/`: map icons from https://github.com/protomaps/basemaps-assets (v4), derived from
  MIT-licensed tangrams/icons (https://github.com/tangrams/icons).

Map data shown with them is © OpenStreetMap contributors (ODbL), from Protomaps' planet build.
