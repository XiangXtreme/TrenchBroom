"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
require("./bake.js");
require("./multi.js");

const { width, height, core, worldBlockSize, makeMap } = globalThis.TerrainBake;
const { initialWeights, paint, bake } = globalThis.TerrainMulti;
const output = process.argv[2];
if (!output) {
  console.error("Usage: node clip-example.cjs <output-directory>");
  process.exit(1);
}

const divide = width / 2;
const cap = divide * worldBlockSize / core / 16;
const ramp = (x) => x / 16;
const clippedTop = (x) => Math.min(ramp(x), cap);
const source = {
  points: [[0, 0], [width, 0], [width, height], [0, height]],
  u: [1, 0, 0], v: [0, 1, 0], material: 0,
};

function splitAtCap(face) {
  const xs = face.points.map(([x]) => x);
  if (Math.min(...xs) >= divide || Math.max(...xs) <= divide) return [face];

  // In this fixture the cut is on a paint-grid line. Only rectangles can cross
  // it, so each clipped part remains a quad supported by the demo exporter.
  assert.equal(face.points.length, 4);
  assert.equal(new Set(xs).size, 2);
  const [left, right] = [Math.min(...xs), Math.max(...xs)];
  const ys = face.points.map(([, y]) => y);
  assert.equal(new Set(ys).size, 2);
  const [top, bottom] = [Math.min(...ys), Math.max(...ys)];
  return [[left, divide], [divide, right]].map(([a, b]) => ({
    ...face, points: [[a, top], [b, top], [b, bottom], [a, bottom]],
  }));
}

function footprint(faces) {
  return faces.reduce((sum, { points }) => sum + points.reduce((area, [x, y], i) => {
    const [nextX, nextY] = points[(i + 1) % points.length];
    return area + x * nextY - y * nextX;
  }, 0) / 2, 0);
}

const weights = initialWeights();
// Paint a short fork across the future break between ramp and platform.
for (let x = 390; x <= 624; x += 6) {
  paint(weights, x, 266 + (x - 390) / 4, 45, 1);
}
const { shared } = bake(weights);
const maps = [
  ["terrain_source_before", [source], ramp],
  ["terrain_source_after", splitAtCap(source), clippedTop],
  ["terrain_painted_before", shared.faces, ramp],
  ["terrain_painted_after", shared.faces.flatMap(splitAtCap), clippedTop],
];
fs.mkdirSync(output, { recursive: true });
const id = "terrain_clip";
fs.writeFileSync(path.join(output, id + ".wad"), shared.wad);
for (const [name, faces, terrainTop] of maps) {
  assert.equal(footprint(faces), width * height);
  if (terrainTop === clippedTop) {
    assert(faces.every(({ points }) => points.every(([x]) => x <= divide)
      || points.every(([x]) => x >= divide)));
  }
  const map = makeMap({ ...shared, id, faces }, { terrainTop });
  fs.writeFileSync(path.join(output, name + ".map"), map);
  console.log(name + " · " + faces.length + " terrain tops");
}
