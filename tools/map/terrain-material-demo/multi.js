"use strict";

globalThis.TerrainMulti = (() => {
  const { width, height, columns, rows, core, gutter, period, clamp, smooth,
    noise, valueNoise, wadVariant } = globalThis.TerrainBake;
  const names = ["grass", "dirt", "rock"];
  const colors = [[102, 115, 62], [158, 116, 78], [129, 128, 130]];
  const step = 24, across = width / step, down = height / step, stride = across + 1;
  const patchCore = 2 * step, patchGutter = 8;
  const lowV = 24, highV = 104;
  const worldU = [1, 0, 0], worldV = [0, 1, 0], sharedU = [0.875, 0.5, 0];
  const weightSteps = 7, shades = 7;
  const palette = new Uint8Array(768);
  const ramp = [];
  let nextColor = 0;
  for (let dirt = 0; dirt <= weightSteps; dirt++) {
    ramp[dirt] = nextColor;
    for (let rock = 0; rock <= weightSteps - dirt; rock++) {
      const grass = weightSteps - dirt - rock;
      for (let shade = 0; shade < shades; shade++) {
        for (let channel = 0; channel < 3; channel++) {
          const base = (colors[0][channel] * grass + colors[1][channel] * dirt
            + colors[2][channel] * rock) / weightSteps;
          palette[nextColor * 3 + channel] = Math.round(base + (shade - 3) * 9);
        }
        nextColor++;
      }
    }
  }

  const detail = names.map((_, material) => Float32Array.from({ length: period ** 2 }, (_, i) => {
    const x = i % period, y = Math.floor(i / period);
    const grain = noise(x, y, 97 + material * 17) - 0.5;
    if (material === 0)
      return clamp(3 + 3 * (valueNoise(x, y, 2, 62) - 0.5) + 3 * grain, 0, 6);
    if (material === 1)
      return clamp(3 + 4 * (valueNoise(x, y, 4, 71) - 0.5) + 2 * grain, 0, 6);
    return clamp(3 + 5 * (valueNoise(x, y, 8, 83) - 0.5) + 3 * grain, 0, 6);
  }));

  function texture(name, sample, originX = 0, originY = 0, size = period) {
    let side = size;
    let samples = new Float32Array(side * side * 3);
    for (let y = 0; y < side; y++) {
      for (let x = 0; x < side; x++) {
        const offset = (y * side + x) * 3;
        const worldX = originX + x, worldY = originY + y;
        const weights = sample(worldX + 0.5, worldY + 0.5);
        samples[offset] = weights[1];
        samples[offset + 1] = weights[2];
        const texel = ((worldY + period) % period) * period + (worldX + period) % period;
        samples[offset + 2] = weights[0] * detail[0][texel] + weights[1] * detail[1][texel]
          + weights[2] * detail[2][texel];
      }
    }
    const levels = [];
    for (let mip = 0; mip < 4; mip++) {
      const pixels = new Uint8Array(side * side);
      for (let i = 0; i < pixels.length; i++) {
        const offset = i * 3;
        const dirt = samples[offset] * weightSteps, rock = samples[offset + 1] * weightSteps;
        // Quantize on the simplex so the three rounded weights still sum to one.
        const r = Math.min(weightSteps - Math.round(dirt),
          Math.round(dirt + rock) - Math.round(dirt));
        pixels[i] = ramp[Math.round(dirt)] + r * shades + Math.round(samples[offset + 2]);
      }
      levels.push(pixels);
      if (mip === 3) break;
      const next = new Float32Array((side / 2) ** 2 * 3);
      for (let y = 0; y < side; y += 2) {
        for (let x = 0; x < side; x += 2) {
          const src = (y * side + x) * 3, dst = (y / 2 * (side / 2) + x / 2) * 3;
          for (let k = 0; k < 3; k++)
            next[dst + k] = (samples[src + k] + samples[src + k + 3]
              + samples[src + k + side * 3] + samples[src + k + side * 3 + 3]) / 4;
        }
      }
      samples = next;
      side /= 2;
    }
    return { name, size, levels, colors: palette };
  }

  const bases = names.map((name, index) =>
    texture("tm_" + name, () => names.map((_, k) => Number(k === index))));
  const pairs = new Map();
  for (let a = 0; a < names.length; a++) {
    for (let b = a + 1; b < names.length; b++) {
      pairs.set(a * names.length + b, texture("tm_" + names[a] + "_" + names[b], (_, y) => {
        const weight = clamp((y - lowV) / (highV - lowV), 0, 1);
        return names.map((_, k) => k === a ? 1 - weight : k === b ? weight : 0);
      }));
    }
  }

  function initialWeights() {
    return Array.from({ length: stride * (down + 1) }, (_, i) => {
      const x = i % stride * step, y = Math.floor(i / stride) * step;
      const road = 244 + 42 * Math.sin(x / width * 2 * Math.PI);
      const dirt = smooth(clamp((63 - Math.abs(y - road)) / 45, 0, 1));
      const rockLine = 108 + 166 * smooth(clamp((x - 315) / 400, 0, 1));
      const rock = smooth(clamp((rockLine + 24 - y) / 48, 0, 1))
        * smooth(clamp((x - 270) / 80, 0, 1));
      return [(1 - dirt) * (1 - rock), dirt * (1 - rock), rock];
    });
  }

  function paint(weights, x, y, radius, target) {
    const left = Math.max(0, Math.ceil((x - radius) / step));
    const right = Math.min(across, Math.floor((x + radius) / step));
    const top = Math.max(0, Math.ceil((y - radius) / step));
    const bottom = Math.min(down, Math.floor((y + radius) / step));
    for (let row = top; row <= bottom; row++) {
      for (let col = left; col <= right; col++) {
        const distance = Math.hypot(col * step - x, row * step - y);
        const strength = smooth(clamp((radius - distance) / (radius * 0.65), 0, 1));
        const vertex = weights[row * stride + col];
        for (let k = 0; k < names.length; k++)
          vertex[k] = vertex[k] * (1 - strength) + (k === target ? strength : 0);
      }
    }
  }

  function bake(weights) {
    const vertices = weights.map((vertex) => {
      const active = vertex.map((value) => value >= 0.05 ? value : 0);
      const sum = active.reduce((a, b) => a + b);
      return active.map((value) => value / sum);
    });
    const quads = Array.from({ length: across * down }, (_, i) => {
      const row = Math.floor(i / across), col = i % across;
      const vertex = row * stride + col;
      return [vertices[vertex], vertices[vertex + 1],
        vertices[vertex + stride + 1], vertices[vertex + stride]];
    });
    const locate = (x, y) => {
      const col = Math.min(Math.floor(x / step), across - 1);
      const row = Math.min(Math.floor(y / step), down - 1);
      return [row * across + col, x / step - col, y / step - row];
    };
    const sample = (x, y) => {
      const [cell, s, t] = locate(clamp(x, 0, width), clamp(y, 0, height));
      const [a, b, c, d] = quads[cell];
      return a.map((value, k) => s >= t ? value * (1 - s) + b[k] * (s - t) + c[k] * t
        : value * (1 - t) + c[k] * s + d[k] * (t - s));
    };
    const tile = (cell, prefix) => texture(prefix + cell, sample,
      cell % columns * core - gutter, Math.floor(cell / columns) * core - gutter);
    const activeMaterials = (corners) => names.map((_, k) => k)
      .filter((k) => corners.some((vertex) => vertex[k] > 0));
    const bakedTextures = [...bases], bakedCells = [];
    for (let row = 0; row < rows; row++) {
      for (let col = 0; col < columns; col++) {
        const cellsPerTile = core / step;
        const corners = [];
        for (let dy = 0; dy <= cellsPerTile; dy++) {
          for (let dx = 0; dx <= cellsPerTile; dx++)
            corners.push(vertices[(row * cellsPerTile + dy) * stride + col * cellsPerTile + dx]);
        }
        const active = activeMaterials(corners);
        bakedCells.push(active.length === 1 ? active[0] : bakedTextures.length);
        if (active.length > 1) bakedTextures.push(tile(row * columns + col, "tm_b"));
      }
    }

    const textures = [...bases], textureIndices = new Map();
    const faces = [], grid = new Array(quads.length);
    const pure = quads.map((quad) => {
      const active = activeMaterials(quad);
      return active.length === 1 ? active[0] : -1;
    });
    const triangles = [[0, 1, 2], [0, 2, 3]];
    const mixes = quads.map((quad, cell) => {
      if (pure[cell] >= 0) return null;
      return triangles.map((triangle, half) => {
        const corners = triangle.map((index) => quad[index]);
        const active = activeMaterials(corners);
        if (active.length === 1) return { kind: "base", material: active[0] };
        if (active.length === 3) return { kind: "junction" };
        const [a, b] = active;
        const [v0, v1, v2] = corners.map((corner) => lowV + corner[b] * (highV - lowV));
        const vx = (half ? v1 - v2 : v1 - v0) / step;
        const vy = (half ? v2 - v0 : v2 - v1) / step;
        if (Math.abs(sharedU[0] * vy - sharedU[1] * vx) < 0.02) return { kind: "fit" };
        const x = cell % across * step, y = Math.floor(cell / across) * step;
        return { kind: "pair", key: a * names.length + b, v: [vx, vy, v0 - vx * x - vy * y] };
      });
    });
    let patchCount = 0, mergedPatchFaces = 0;
    function add(points, u, v, material, kind) {
      const face = { points, u, v, material, kind };
      faces.push(face);
      return face;
    }
    function patch(points, x, y, kind) {
      const key = `patch:${x}:${y}`;
      if (!textureIndices.has(key)) {
        textureIndices.set(key, textures.length);
        textures.push(texture("tm_p" + patchCount++, sample,
          x - patchGutter, y - patchGutter, patchCore + 2 * patchGutter));
      }
      return add(points, [1, 0, patchGutter - x], [0, 1, patchGutter - y],
        textureIndices.get(key), kind);
    }
    const rect = (x, y, right, bottom) => [[x, y], [right, y], [right, bottom], [x, bottom]];

    // Merge busy blocks already paying for a patch first. Spend a limited number
    // of extra 64px textures on two-material blocks with the most cuts.
    const candidates = [];
    for (let row = 0; row < down; row += 2) {
      for (let col = 0; col < across; col += 2) {
        const cell = row * across + col;
        const cells = [cell, cell + 1, cell + across, cell + across + 1];
        const mixed = cells.filter((index) => mixes[index]);
        if (mixed.length < 2) continue;
        const kinds = mixed.flatMap((index) => mixes[index].map(({ kind }) => kind));
        const kind = kinds.includes("junction") ? "junction" : kinds.includes("fit") ? "fit" : "patch";
        if (kind !== "patch" || mixed.length >= 3)
          candidates.push({ row, col, cells, count: mixed.length, kind });
      }
    }
    const extraPatches = Math.min(16, Math.floor((bakedTextures.length - bases.length) / 2));
    let addedPatches = 0;
    candidates.sort((a, b) => b.count - a.count);
    for (const { row, col, cells, kind } of candidates) {
      if (kind === "patch" && addedPatches >= extraPatches) continue;
      if (kind === "patch") addedPatches++;
      const x = col * step, y = row * step;
      const surface = patch(rect(x, y, x + patchCore, y + patchCore), x, y, kind);
      for (const cell of cells) grid[cell] = [surface];
      mergedPatchFaces++;
    }

    for (let row = 0; row < down; row++) {
      for (let col = 0; col < across; col++) {
        const cell = row * across + col, x = col * step, y = row * step;
        if (grid[cell]) continue;
        if (pure[cell] >= 0) {
          let w = 1, h = 1;
          while (col + w < across && pure[cell + w] === pure[cell] && !grid[cell + w]) w++;
          while (row + h < down) {
            const start = cell + h * across;
            if (!pure.slice(start, start + w).every((value, i) =>
              value === pure[cell] && !grid[start + i])) break;
            h++;
          }
          const right = x + w * step, bottom = y + h * step;
          const face = add(rect(x, y, right, bottom),
            worldU, worldV, pure[cell], "base");
          for (let dy = 0; dy < h; dy++) grid.fill([face],
            cell + dy * across, cell + dy * across + w);
          continue;
        }
        const points = rect(x, y, x + step, y + step);
        const [first, second] = mixes[cell];
        function surface(polygon, { kind, material, key, v }) {
          if (kind === "base") return add(polygon, worldU, worldV, material, kind);
          if (kind !== "pair") return patch(polygon,
            Math.floor(x / patchCore) * patchCore, Math.floor(y / patchCore) * patchCore, kind);
          if (!textureIndices.has(key)) {
            textureIndices.set(key, textures.length);
            textures.push(pairs.get(key));
          }
          return add(polygon, sharedU, v, textureIndices.get(key), kind);
        }
        const samePair = first.kind === "pair" && second.kind === "pair"
          && first.key === second.key && first.v.every((value, i) => Math.abs(value - second.v[i]) < 1e-8);
        const samePatch = first.kind === second.kind && (first.kind === "junction" || first.kind === "fit");
        grid[cell] = samePair || samePatch ? [surface(points, first)]
          : triangles.map((triangle, half) => surface(triangle.map((index) => points[index]), mixes[cell][half]));
      }
    }
    const original = wadVariant("terrain_multi_baked", bakedTextures, bakedCells);
    const shared = wadVariant("terrain_multi_uv", textures, faces.map((face) => face.material));
    return { original: { ...original, baseCount: bases.length },
      shared: { ...shared, baseCount: bases.length, faces, patchCount, mergedPatchFaces,
        junctionFaces: faces.filter(({ kind }) => kind === "junction").length,
        fitFaces: faces.filter(({ kind }) => kind === "fit").length,
        grid, gridStep: step } };
  }

  return { initialWeights, paint, bake };
})();
