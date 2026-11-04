"use strict";

importScripts("bake.js", "multi.js", "preview.js");

self.onmessage = ({ data: { scene, weights, revision } }) => {
  const results = scene === "multi" ? TerrainMulti.bake(weights) : TerrainBake.bake(weights);
  const transfer = [];
  for (const variant of Object.values(results)) {
    variant.previews = Array.from({ length: 4 }, (_, mip) => TerrainPreview.render(variant, mip));
    transfer.push(variant.wad.buffer, ...variant.previews.map(({ buffer }) => buffer));
  }
  self.postMessage({ scene, revision, results }, transfer);
};
