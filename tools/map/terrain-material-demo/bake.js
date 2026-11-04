"use strict";

globalThis.TerrainBake = (() => {
  const columns = 10, rows = 5, core = 96, gutter = 16, period = 32;
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
      grass[index] = 3 + 8 * valueNoise(x, y, 8, 10) + 5 * noise(x, y, 9)
        - 3 * Number(noise(x, Math.floor(y / 2), 32) < 0.15);
      dirt[index] = 2 + 7 * valueNoise(x, y, 4, 21) + 5 * noise(x, y, 37)
        + 3 * Number(noise(x, y, 44) > 0.94);
    }
  }

  function raster(size, sample) {
    const alpha = new Float32Array(size * size), shade = new Float32Array(size * size);
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const index = y * size + x;
        const weight = sample(x, y);
        alpha[index] = weight * (15 / 255);
        shade[index] = mix(grass[wrap(y, period) * period + wrap(x, period)],
          dirt[wrap(y, period) * period + wrap(x, period)], weight / 255);
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
    textures.forEach(({ name, size, levels }, index) => {
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
      bytes.set(palette, offset + mipOffset + 2);
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

  function bake(weights) {
    const size = core + 2 * gutter;
    const sample = (x, y) =>
      weights[clamp(y, 0, height - 1) * width + clamp(x, 0, width - 1)];
    const original = [...bases], shared = [...bases];
    const uniqueTiles = new Map();
    const originalCells = [], sharedCells = [];

    for (let row = 0; row < rows; row++) {
      for (let col = 0; col < columns; col++) {
        const at = (x, y) => sample(col * core - gutter + x, row * core - gutter + y);
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
        const matches = uniqueTiles.get(hash) ?? [];
        // Cell size and gutter are multiples of the base texture period or share
        // a fixed phase; matching the full padded mask preserves every mip.
        let match = matches.find((candidate) =>
          candidate.signature.every((value, index) => signature[index] === value));
        if (!match) {
          match = { signature, texture: shared.length };
          matches.push(match);
          uniqueTiles.set(hash, matches);
          shared.push({ name: "tm_s" + shared.length, size, levels: raster(size, at) });
        }
        originalCells.push(original.length);
        original.push({ ...shared[match.texture], name: "tm_u" + original.length });
        sharedCells.push(match.texture);
      }
    }
    const variant = (id, textures, cells) => {
      const wad = encodeWad(textures);
      return { id, wad, textures: decodeWad(wad, textures), cells };
    };
    return {
      original: variant("terrain_unique", original, originalCells),
      shared: variant("terrain_shared", shared, sharedCells),
    };
  }

  function makeMap(variant) {
    const span = 128, scale = span / core;
    const mapWidth = span * columns, mapHeight = span * rows;
    const point = (xyz) => "( " + xyz.join(" ") + " )";
    const face = (a, b, c, texture, s, t, factor = 1) =>
      [point(a), point(b), point(c), texture, "[ " + s.join(" ") + " ]",
        "[ " + t.join(" ") + " ]", 0, factor, factor].join(" ");
    const brush = (x0, y0, x1, y1, bottom, top, texture, shift = [0, 0]) => {
      const a = [x0, y0, top(x0)], b = [x1, y0, top(x1)];
      const c = [x1, y1, top(x1)], d = [x0, y1, top(x0)];
      const lower = ([x, y]) => [x, y, bottom];
      const side = (p, q, axis) => face(p, q, lower(q), "tm_grass", axis, [0, 0, -1, 0]);
      return ["{", face(d, c, b, texture, [1, 0, 0, shift[0]], [0, -1, 0, shift[1]], scale),
        face(lower(b), lower(c), lower(d), "tm_grass", [1, 0, 0, 0], [0, -1, 0, 0]),
        side(d, a, [0, 1, 0, 0]), side(a, b, [1, 0, 0, 0]),
        side(b, c, [0, 1, 0, 0]), side(c, d, [1, 0, 0, 0]), "}"].join("\n");
    };
    const lines = ["// Game: Half-Life", "// Format: Valve", "{", '"classname" "worldspawn"',
      '"mapversion" "220"', '"wad" "' + variant.id + '.wad"'];
    variant.cells.forEach((index, cell) => {
      const col = cell % columns, row = Math.floor(cell / columns);
      const shift = index < 2 ? [0, 0] : [gutter - col * core, gutter - row * core];
      lines.push(brush(col * span, -(row + 1) * span, (col + 1) * span, -row * span,
        -64, (x) => x / 16, variant.textures[index].name, shift));
    });
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

  return { columns, rows, core, gutter, width, height, initialWeights, paint, bake, makeMap };
})();
