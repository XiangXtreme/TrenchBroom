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
          palette[nextColor * 3 + channel] = Math.round(base + (shade - 3) * 7);
        }
        nextColor++;
      }
    }
  }

  function detail(x, y) {
    return 1 + 3 * valueNoise(x, y, 16, 62) + 2 * noise(x, y, 97);
  }

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
        samples[offset + 2] = detail((worldX + period) % period, (worldY + period) % period);
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
    let junctionFaces = 0, fitFaces = 0, patchCount = 0;
    function add(points, u, v, material, kind) {
      const face = { points, u, v, material, kind };
      faces.push(face);
      return face;
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
          const face = add([[x, y], [right, y], [right, bottom], [x, bottom]],
            worldU, worldV, pure[cell], "base");
          for (let dy = 0; dy < h; dy++) grid.fill([face],
            cell + dy * across, cell + dy * across + w);
          continue;
        }
        const points = [[x, y], [x + step, y], [x + step, y + step], [x, y + step]];
        grid[cell] = [[0, 1, 2], [0, 2, 3]].map((triangle, half) => {
          const corners = triangle.map((index) => quads[cell][index]);
          const polygon = triangle.map((index) => points[index]);
          const active = activeMaterials(corners);
          if (active.length === 1) return add(polygon, worldU, worldV, active[0], "base");
          if (active.length === 2) {
            const [a, b] = active;
            const [v0, v1, v2] = corners.map((corner) => lowV + corner[b] * (highV - lowV));
            const vx = (half ? v1 - v2 : v1 - v0) / step;
            const vy = (half ? v2 - v0 : v2 - v1) / step;
            const determinant = sharedU[0] * vy - sharedU[1] * vx;
            if (Math.abs(determinant) >= 0.02) {
              const key = a * names.length + b;
              if (!textureIndices.has(key)) {
                textureIndices.set(key, textures.length);
                textures.push(pairs.get(key));
              }
              return add(polygon, sharedU, [vx, vy, v0 - vx * x - vy * y],
                textureIndices.get(key), "pair");
            }
          }
          const patchX = Math.floor(x / patchCore) * patchCore;
          const patchY = Math.floor(y / patchCore) * patchCore;
          const key = `patch:${patchX}:${patchY}`;
          if (!textureIndices.has(key)) {
            textureIndices.set(key, textures.length);
            textures.push(texture("tm_p" + patchCount++, sample,
              patchX - patchGutter, patchY - patchGutter, patchCore + 2 * patchGutter));
          }
          const kind = active.length === 3 ? "junction" : "fit";
          if (kind === "junction") junctionFaces++;
          else fitFaces++;
          return add(polygon, [1, 0, patchGutter - patchX], [0, 1, patchGutter - patchY],
            textureIndices.get(key), kind);
        });
      }
    }
    const original = wadVariant("terrain_multi_baked", bakedTextures, bakedCells);
    const shared = wadVariant("terrain_multi_uv", textures, faces.map((face) => face.material));
    return { original: { ...original, baseCount: bases.length },
      shared: { ...shared, baseCount: bases.length, faces, junctionFaces, fitFaces, patchCount,
        faceAt(x, y) {
          const [cell, s, t] = locate(x, y);
          const candidates = grid[cell];
          return candidates.length === 1 || s >= t ? candidates[0] : candidates[1];
        } } };
  }

  return { initialWeights, paint, bake };
})();
