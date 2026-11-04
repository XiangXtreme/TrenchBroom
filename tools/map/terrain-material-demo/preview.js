"use strict";

globalThis.TerrainPreview = (() => {
  const { columns, core, gutter, width, height } = globalThis.TerrainBake;

  function surfaceAt(variant, x, y) {
    if (variant.grid) {
      const step = variant.gridStep;
      const col = Math.floor(x / step), row = Math.floor(y / step);
      const candidates = variant.grid[row * (width / step) + col];
      return candidates.length === 1 || x - col * step >= y - row * step
        ? candidates[0] : candidates[1];
    }
    const segment = Math.floor(x / variant.segmentWidth);
    const fraction = x / variant.segmentWidth - segment;
    for (let band = 0; band < variant.bandCount; band++) {
      const surface = variant.faces[segment * variant.bandCount + band];
      const lower = surface.points[3][1], upper = surface.points[2][1];
      if (y < lower + (upper - lower) * fraction) return surface;
    }
  }

  function render(variant, level) {
    const step = 1 << level, imageWidth = width / step, imageHeight = height / step;
    const pixels = new Uint8ClampedArray(imageWidth * imageHeight * 4);
    function write(output, texture, index) {
      const color = texture.levels[level][index] * 3;
      pixels[output] = texture.colors[color];
      pixels[output + 1] = texture.colors[color + 1];
      pixels[output + 2] = texture.colors[color + 2];
      pixels[output + 3] = 255;
    }
    if (variant.faces) {
      const coordinate = ([a, b, offset], x, y, side) => {
        const texel = Math.floor((a * x + b * y + offset) / step);
        return texel & (side - 1);
      };
      for (let y = 0; y < imageHeight; y++) {
        for (let x = 0; x < imageWidth; x++) {
          const px = (x + 0.5) * step, py = (y + 0.5) * step;
          const surface = surfaceAt(variant, px, py);
          const texture = variant.textures[surface.material];
          const cols = texture.size / step, rows = texture.height / step;
          const index = coordinate(surface.v, px, py, rows) * cols
            + coordinate(surface.u, px, py, cols);
          write((y * imageWidth + x) * 4, texture, index);
        }
      }
    } else {
      const side = core / step;
      variant.cells.forEach((index, cell) => {
        const texture = variant.textures[index], tileSide = texture.size / step;
        const base = index < (variant.baseCount ?? 2);
        const startX = (cell % columns) * side, startY = Math.floor(cell / columns) * side;
        for (let y = 0; y < side; y++) {
          for (let x = 0; x < side; x++) {
            const source = base
              ? ((startY + y) % tileSide) * tileSide + (startX + x) % tileSide
              : (y + gutter / step) * tileSide + x + gutter / step;
            write(((startY + y) * imageWidth + startX + x) * 4, texture, source);
          }
        }
      });
    }
    return pixels;
  }

  return { render, surfaceAt };
})();
