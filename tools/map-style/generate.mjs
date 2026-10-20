// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the map's layers, light and dark, from Protomaps' basemap style (BSD-3-Clause). The app puts them together with
// the downloaded regions' sources at run time (MapManager). Run: npm ci && node generate.mjs
import { layers, namedFlavor } from "@protomaps/basemaps";
import fs from "fs";
for (const flavor of ["light", "dark"]) {
  const out = new URL(`../../data/map/layers-${flavor}.json`, import.meta.url);
  fs.writeFileSync(out, JSON.stringify(layers("protomaps", namedFlavor(flavor), { lang: "en" }), null, 1) + "\n");
  console.log(`wrote ${out.pathname}`);
}
