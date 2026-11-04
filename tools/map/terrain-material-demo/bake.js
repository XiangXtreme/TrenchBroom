"use strict";

globalThis.TerrainBake = (() => {
  const columns = 10, rows = 5, core = 96, gutter = 16, period = 128;
  const worldBlockSize = 128;
  const width = columns * core, height = rows * core;
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
  const wrap = (value, size) => ((value % size) + size) % size;
  const mix = (a, b, t) => a + (b - a) * t;
  const smooth = (t) => t * t * (3 - 2 * t);

  function distance(x, y, [ax, ay, bx, by]) {
    const dx = bx - ax, dy = by - ay;
    const t = clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0, 1);
    return Math.hypot(x - ax - t * dx, y - ay - t * dy);
  }

  function initialWeights() {
    const road = [
      [-48, 224, 600, 224], [600, 224, 708, 305], [708, 305, 1008, 305],
      [567, 224, 728, 110], [728, 110, 870, 106], [870, 106, 1008, 150],
    ];
    const weights = new Uint8Array(width * height);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const d = Math.min(...road.map((segment) => distance(x + 0.5, y + 0.5, segment)));
        weights[y * width + x] = Math.round(255 * smooth(clamp((49 - d) / 23, 0, 1)));
      }
    }
    return weights;
  }

  function paint(weights, x, y, radius, target) {
    for (let py = Math.max(0, Math.floor(y - radius)); py < Math.min(height, y + radius); py++) {
      for (let px = Math.max(0, Math.floor(x - radius)); px < Math.min(width, x + radius); px++) {
        const strength = smooth(clamp(1 - Math.hypot(px - x, py - y) / radius, 0, 1));
        const index = py * width + px;
        weights[index] = Math.round(mix(weights[index], target, strength));
      }
    }
  }

  function noise(x, y, seed) {
    let bits = Math.imul(x + 91, 374761393) ^ Math.imul(y + seed, 668265263);
    bits = Math.imul(bits ^ (bits >>> 13), 1274126177);
    return ((bits ^ (bits >>> 16)) >>> 0) / 4294967295;
  }

  function valueNoise(x, y, size, seed) {
    const ix = Math.floor(x / size), iy = Math.floor(y / size);
    const tx = smooth(x / size - ix), ty = smooth(y / size - iy);
    const at = (u, v) => noise(wrap(u, period / size), wrap(v, period / size), seed);
    return mix(mix(at(ix, iy), at(ix + 1, iy), tx),
      mix(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
  }

  // Reserve the same ramp entries for pure and blended surfaces.
  const palette = new Uint8Array(768);
  for (let alpha = 0; alpha < 16; alpha++) {
    for (let shade = 0; shade < 16; shade++) {
      const base = [102, 115, 62].map((value, channel) =>
        mix(value, [158, 116, 78][channel], alpha / 15));
      for (let channel = 0; channel < 3; channel++) {
        palette[(alpha * 16 + shade) * 3 + channel] =
          Math.round(clamp(base[channel] + (shade - 7.5) * 7, 0, 255));
      }
    }
  }

  const grass = new Float32Array(period * period), dirt = new Float32Array(period * period);
  for (let y = 0; y < period; y++) {
    for (let x = 0; x < period; x++) {
      const index = y * period + x;
      grass[index] = 2 + 3 * valueNoise(x, y, 32, 10) + 4 * valueNoise(x, y, 4, 32)
        + 5 * noise(x, y, 9);
      dirt[index] = 3 + 2 * valueNoise(x, y, 16, 21) + 4 * valueNoise(x, y, 4, 44)
        + 4 * noise(x, y, 37);
    }
  }

  function raster(size, sample, originX = 0, originY = 0) {
    const alpha = new Float32Array(size * size), shade = new Float32Array(size * size);
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const index = y * size + x;
        const weight = sample(x, y);
        alpha[index] = weight * (15 / 255);
        const materialIndex = wrap(originY + y, period) * period + wrap(originX + x, period);
        shade[index] = mix(grass[materialIndex], dirt[materialIndex], weight / 255);
      }
    }
    return Array.from({ length: 4 }, (_, level) => {
      const step = 1 << level, side = size >> level;
      const pixels = new Uint8Array(side * side);
      for (let y = 0; y < side; y++) {
        for (let x = 0; x < side; x++) {
          let a = 0, s = 0;
          for (let dy = 0; dy < step; dy++) {
            for (let dx = 0; dx < step; dx++) {
              const index = (y * step + dy) * size + x * step + dx;
              a += alpha[index];
              s += shade[index];
            }
          }
          pixels[y * side + x] = Math.round(a / (step * step)) * 16
            + clamp(Math.round(s / (step * step)), 0, 15);
        }
      }
      return pixels;
    });
  }

  function encodeWad(textures) {
    const lumpSizes = textures.map(({ levels }) =>
      (40 + levels.reduce((sum, level) => sum + level.length, 0) + 770 + 3) & ~3);
    const directory = lumpSizes.reduce((sum, size) => sum + size, 12);
    const bytes = new Uint8Array(directory + textures.length * 32);
    const view = new DataView(bytes.buffer);
    const u32 = (at, value) => view.setUint32(at, value, true);
    const ascii = (at, value) => bytes.set(new TextEncoder().encode(value), at);
    ascii(0, "WAD3");
    u32(4, textures.length);
    u32(8, directory);
    let offset = 12;
    textures.forEach(({ name, size, levels, colors = palette }, index) => {
      ascii(offset, name);
      u32(offset + 16, size);
      u32(offset + 20, size);
      let mipOffset = 40;
      levels.forEach((level, mip) => {
        u32(offset + 24 + mip * 4, mipOffset);
        bytes.set(level, offset + mipOffset);
        mipOffset += level.length;
      });
      view.setUint16(offset + mipOffset, 256, true);
      bytes.set(colors, offset + mipOffset + 2);
      const entry = directory + index * 32;
      u32(entry, offset);
      u32(entry + 4, lumpSizes[index]);
      u32(entry + 8, lumpSizes[index]);
      bytes[entry + 12] = 0x43;
      ascii(entry + 16, name);
      offset += lumpSizes[index];
    });
    return bytes;
  }

  function decodeWad(bytes, textures) {
    const view = new DataView(bytes.buffer);
    const directory = view.getUint32(8, true);
    return textures.map((texture, index) => {
      const offset = view.getUint32(directory + index * 32, true);
      const size = view.getUint32(offset + 16, true);
      const levels = Array.from({ length: 4 }, (_, level) => {
        const start = offset + view.getUint32(offset + 24 + level * 4, true);
        return bytes.subarray(start, start + (size >> level) ** 2);
      });
      const colors = levels[3].byteOffset + levels[3].byteLength + 2;
      return { ...texture, size, levels, colors: bytes.subarray(colors, colors + 768) };
    });
  }

  const bases = [0, 255].map((weight, index) => ({
    name: index ? "tm_dirt" : "tm_grass", size: period, levels: raster(period, () => weight),
  }));

  function wadVariant(id, textures, cells) {
    const wad = encodeWad(textures);
    return { id, wad, textures: decodeWad(wad, textures), cells };
  }

  function bake(weights) {
    const size = core + 2 * gutter;
    const sample = (x, y) =>
      weights[clamp(y, 0, height - 1) * width + clamp(x, 0, width - 1)];
    const original = [...bases], shared = [...bases];
    const uniqueTiles = new Map();
    const originalCells = [], sharedCells = [];

    for (let row = 0; row < rows; row++) {
      for (let col = 0; col < columns; col++) {
        const originX = col * core - gutter, originY = row * core - gutter;
        const at = (x, y) => sample(originX + x, originY + y);
        let lo = 255, hi = 0, hash = 2166136261;
        const signature = new Uint8Array(size * size);
        for (let y = 0; y < size; y++) {
          for (let x = 0; x < size; x++) {
            const weight = at(x, y);
            lo = Math.min(lo, weight);
            hi = Math.max(hi, weight);
            hash = Math.imul(hash ^ weight, 16777619);
            signature[y * size + x] = weight;
          }
        }
        if (hi === 0 || lo === 255) {
          const index = hi === 0 ? 0 : 1;
          originalCells.push(index);
          sharedCells.push(index);
          continue;
        }
        // A matching mask can only share pixels at the same base texture phase.
        const key = [wrap(originX, period), wrap(originY, period), hash].join(":");
        const matches = uniqueTiles.get(key) ?? [];
        let match = matches.find((candidate) =>
          candidate.signature.every((value, index) => signature[index] === value));
        if (!match) {
          match = { signature, texture: shared.length };
          matches.push(match);
          uniqueTiles.set(key, matches);
          shared.push({ name: "tm_s" + shared.length, size,
            levels: raster(size, at, originX, originY) });
        }
        originalCells.push(original.length);
        original.push({ ...shared[match.texture], name: "tm_u" + original.length });
        sharedCells.push(match.texture);
      }
    }
    return {
      original: wadVariant("terrain_unique", original, originalCells),
      shared: wadVariant("terrain_shared", shared, sharedCells),
    };
  }

  function bakeBend() {
    const segmentWidth = 48;
    const grassEdge = 48, dirtEdge = 80;
    const transitionWidth = dirtEdge - grassEdge;
    const inner = period / 2, outer = inner + transitionWidth;
    const materials = [0, 2, 1, 2, 0];
    const boundaries = ({ center }) => [0, center - outer, center - inner,
      center + inner, center + outer, height];
    const weightAtDistance = (distance) =>
      Math.round(255 * smooth(clamp((outer - distance) / (outer - inner), 0, 1)));
    const sections = Array.from({ length: width / segmentWidth + 1 }, (_, index) => {
      const x = index * segmentWidth;
      const center = height / 2 + 3 * Math.round(31 * Math.sin(x / width * 2 * Math.PI));
      return { x, center };
    });
    const weights = new Uint8Array(width * height);
    const faces = [];
    let distance = 0;
    for (let i = 0; i < sections.length - 1; i++) {
      const left = sections[i], right = sections[i + 1];
      const slope = (right.center - left.center) / segmentWidth;
      const uScale = Math.hypot(1, slope);
      const centerOffset = left.center - slope * left.x;
      const a = boundaries(left), b = boundaries(right);
      for (let band = 0; band < materials.length; band++) {
        const sign = band < 3 ? 1 : -1;
        // The dirt spans one repeat, so both strips meet it at the same V phase.
        const vCenter = band < 2 ? dirtEdge - inner : dirtEdge + inner;
        faces.push({
          points: [[left.x, a[band]], [right.x, b[band]],
            [right.x, b[band + 1]], [left.x, a[band + 1]]],
          u: [uScale, 0, distance - left.x * uScale],
          v: [-slope * sign, sign, vCenter - centerOffset * sign],
          material: materials[band],
        });
      }
      for (let x = left.x; x < right.x; x++) {
        const center = slope * (x + 0.5) + centerOffset;
        for (let y = 0; y < height; y++)
          weights[y * width + x] = weightAtDistance(Math.abs(y + 0.5 - center));
      }
      distance += segmentWidth * uScale;
    }
    const transition = { name: "tm_grass_dirt", size: period,
      levels: raster(period, (_, y) =>
        Math.round(255 * smooth(clamp((y + 0.5 - grassEdge) / transitionWidth, 0, 1)))) };
    const original = { ...bake(weights).original, id: "terrain_bend_baked" };
    const shared = wadVariant("terrain_bend_strip", [...bases, transition],
      faces.map((face) => face.material));
    return { original, shared: { ...shared, faces, segmentWidth, transitionWidth,
      bandCount: materials.length } };
  }

  function makeMap(variant, { terrainTop = (x) => x / 16 } = {}) {
    const span = worldBlockSize, scale = span / core;
    const mapWidth = span * columns, mapHeight = span * rows;
    const point = (xyz) => "( " + xyz.join(" ") + " )";
    const face = (a, b, c, texture, s, t, factor = 1) =>
      [point(a), point(b), point(c), texture, "[ " + s.join(" ") + " ]",
        "[ " + t.join(" ") + " ]", 0, factor, factor].join(" ");
    const prism = (corners, bottom, top, texture, s, t, factor = 1) => {
      const vertices = corners.map(([x, y]) => [x, y, top(x)]);
      const [a, b, c] = vertices;
      const lower = ([x, y]) => [x, y, bottom];
      const side = (p, q) => {
        const axis = p[0] === q[0] ? [0, 1, 0, 0] : [1, 0, 0, 0];
        return face(p, q, lower(q), "tm_grass", axis, [0, 0, -1, 0]);
      };
      const d = vertices.at(-1);
      // Keep the existing quad winding and use the corresponding triangle winding.
      const topFace = vertices.length === 4 ? [d, c, b] : [c, b, a];
      const bottomFace = vertices.length === 4 ? [b, c, d] : [a, b, c];
      return ["{", face(...topFace, texture, s, t, factor),
        face(...bottomFace.map(lower), "tm_grass", [1, 0, 0, 0], [0, -1, 0, 0]),
        ...vertices.map((q, i) => side(vertices[(i + vertices.length - 1) % vertices.length], q)),
        "}"].join("\n");
    };
    const brush = (x0, y0, x1, y1, bottom, top, texture, shift = [0, 0]) =>
      prism([[x0, y0], [x1, y0], [x1, y1], [x0, y1]], bottom, top, texture,
        [1, 0, 0, shift[0]], [0, -1, 0, shift[1]], scale);
    const lines = ["// Game: Half-Life", "// Format: Valve", "{", '"classname" "worldspawn"',
      '"mapversion" "220"', '"wad" "' + variant.id + '.wad"'];
    if (variant.faces) {
      for (const surface of variant.faces) {
        const corners = surface.points.map(([x, y]) => [x * scale, -y * scale]).reverse();
        const axis = ([x, y, offset]) => [x / scale, -y / scale, 0, offset];
        lines.push(prism(corners, -64, terrainTop, variant.textures[surface.material].name,
          axis(surface.u), axis(surface.v)));
      }
    } else {
      variant.cells.forEach((index, cell) => {
        const col = cell % columns, row = Math.floor(cell / columns);
        const shift = index < (variant.baseCount ?? 2) ? [0, 0]
          : [gutter - col * core, gutter - row * core];
        lines.push(brush(col * span, -(row + 1) * span, (col + 1) * span, -row * span,
          -64, terrainTop, variant.textures[index].name, shift));
      });
    }
    const box = (x0, y0, x1, y1, bottom, top) =>
      brush(x0, y0, x1, y1, bottom, () => top, "tm_grass");
    lines.push(box(-32, -mapHeight - 32, 0, 32, -64, 224));
    lines.push(box(mapWidth, -mapHeight - 32, mapWidth + 32, 32, -64, 224));
    lines.push(box(0, -mapHeight - 32, mapWidth, -mapHeight, -64, 224));
    lines.push(box(0, 0, mapWidth, 32, -64, 224));
    lines.push(box(-32, -mapHeight - 32, mapWidth + 32, 32, 224, 256), "}");
    lines.push('{\n"classname" "info_player_start"\n"origin" "64 -300 48"\n"angle" "0"\n}');
    lines.push('{\n"classname" "info_player_deathmatch"\n"origin" "1160 -420 112"\n"angle" "180"\n}');
    for (let x = 96; x < mapWidth; x += 224) {
      for (const y of [-160, -480]) {
        lines.push('{\n"classname" "light"\n"origin" "' + x + " " + y
          + ' 190"\n"_light" "255 240 215 360"\n}');
      }
    }
    return lines.join("\n") + "\n";
  }

  return { columns, rows, core, gutter, width, height, worldBlockSize,
    period, clamp, smooth, noise, valueNoise, wadVariant, initialWeights, paint, bake, bakeBend, makeMap };
})();
