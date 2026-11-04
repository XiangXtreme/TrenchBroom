"use strict";

(() => {
  const { columns, rows, core, gutter, width, height, worldBlockSize,
    initialWeights, paint, bake, bakeBend, makeMap } = globalThis.TerrainBake;
  const find = (id) => document.getElementById(id);
  const radius = find("radius");
  const formatMB = (bytes) => (bytes / 1_000_000).toFixed(3) + " MB";
  // Measured with hltools on the fixed S-bend exports. See README.md.
  const bendBsp = { original: { bytes: 918648, faces: 82 }, shared: { bytes: 115600, faces: 131 } };
  const panels = ["original", "shared"].map((id) => {
    const root = find(id);
    const canvas = root.querySelector(".terrain");
    canvas.width = width;
    canvas.height = height;
    const detail = root.querySelector(".detail");
    detail.width = 400;
    detail.height = 100;
    return { id, root, canvas, detail, image: document.createElement("canvas") };
  });
  let weights = initialWeights();
  let results, bendResults;
  let scene = "paint";
  let tool = "inspect", level = 0;
  let position = { x: 4 * core, y: 191 };
  let stroke, queuedBake, frame;
  const status = find("status");
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

  function surfaceAt(variant, x, y) {
    const segment = Math.floor(x / variant.segmentWidth);
    const fraction = x / variant.segmentWidth - segment;
    for (let band = 0; band < variant.bandCount; band++) {
      const surface = variant.faces[segment * variant.bandCount + band];
      const lower = surface.points[3][1], upper = surface.points[2][1];
      if (y < lower + (upper - lower) * fraction) return surface;
    }
  }

  function writePixel(image, output, texture, level, index) {
    const color = texture.levels[level][index] * 3;
    image.data[output] = texture.colors[color];
    image.data[output + 1] = texture.colors[color + 1];
    image.data[output + 2] = texture.colors[color + 2];
    image.data[output + 3] = 255;
  }

  function renderStrip(image, variant, step) {
    const side = variant.textures[0].size / step;
    const coordinate = ([a, b, offset], x, y) => {
      const texel = Math.floor((a * x + b * y + offset) / step);
      return ((texel % side) + side) % side;
    };
    for (let y = 0; y < image.height; y++) {
      for (let x = 0; x < image.width; x++) {
        const px = (x + 0.5) * step, py = (y + 0.5) * step;
        const surface = surfaceAt(variant, px, py);
        const index = coordinate(surface.v, px, py) * side + coordinate(surface.u, px, py);
        writePixel(image, (y * image.width + x) * 4,
          variant.textures[surface.material], level, index);
      }
    }
  }

  function renderWad(panel) {
    const variant = results[panel.id];
    const step = 1 << level;
    const side = core / step;
    panel.image.width = width / step;
    panel.image.height = height / step;
    const ctx = panel.image.getContext("2d");
    const image = ctx.createImageData(panel.image.width, panel.image.height);

    if (variant.faces) renderStrip(image, variant, step);
    else variant.cells.forEach((index, cell) => {
      const texture = variant.textures[index];
      const { size } = texture;
      const tileSide = size / step;
      const startX = (cell % columns) * side, startY = Math.floor(cell / columns) * side;
      for (let y = 0; y < side; y++) {
        for (let x = 0; x < side; x++) {
          const source = index < 2
            ? ((startY + y) % tileSide) * tileSide + (startX + x) % tileSide
            : (y + gutter / step) * tileSide + x + gutter / step;
          const output = ((startY + y) * image.width + startX + x) * 4;
          writePixel(image, output, texture, level, source);
        }
      }
    });
    ctx.putImageData(image, 0, 0);
  }

  function drawEdges(ctx, variant, projectX, projectY) {
    ctx.beginPath();
    if (variant.faces) {
      for (const { points } of variant.faces) {
        ctx.moveTo(projectX(points[0][0]), projectY(points[0][1]));
        for (const [x, y] of points.slice(1)) ctx.lineTo(projectX(x), projectY(y));
        ctx.closePath();
      }
    } else {
      for (let x = core; x < width; x += core) {
        ctx.moveTo(projectX(x), projectY(0));
        ctx.lineTo(projectX(x), projectY(height));
      }
      for (let y = core; y < height; y += core) {
        ctx.moveTo(projectX(0), projectY(y));
        ctx.lineTo(projectX(width), projectY(y));
      }
    }
    ctx.stroke();
  }

  function renderOverlay(panel) {
    const { canvas, detail, image } = panel;
    const ctx = canvas.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(image, 0, 0, width, height);
    const variant = results[panel.id];
    if (find("grid").checked) {
      ctx.strokeStyle = "#fff6d648";
      ctx.lineWidth = 1;
      drawEdges(ctx, variant, (x) => x + 0.5, (y) => y + 0.5);
      ctx.fillStyle = "#c8ed8a";
      if (scene === "paint") variant.cells.forEach((index, cell) => {
        if (index >= 2 && panel.counts[index] > 1)
          ctx.fillRect((cell % columns) * core + 7, Math.floor(cell / columns) * core + 7, 10, 10);
      });
    }

    const { x, y } = position;
    ctx.strokeStyle = "#f7f9dd";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    if (tool === "inspect" || scene === "bend") {
      ctx.rect(x - 5, y - 5, 10, 10);
    } else {
      ctx.arc(x, y, Number(radius.value), 0, Math.PI * 2);
    }
    ctx.stroke();

    const zoom = detail.getContext("2d");
    const step = 1 << level;
    const cropWidth = 112, cropHeight = 28;
    const cropX = clamp(x - cropWidth / 2, 0, width - cropWidth);
    const cropY = clamp(y - cropHeight / 2, 0, height - cropHeight);
    zoom.imageSmoothingEnabled = false;
    zoom.drawImage(image, cropX / step, cropY / step, cropWidth / step, cropHeight / step,
      0, 0, detail.width, detail.height);
    if (find("grid").checked) {
      zoom.strokeStyle = "#fff6d67a";
      drawEdges(zoom, variant, (x) => (x - cropX) * detail.width / cropWidth + 0.5,
        (y) => (y - cropY) * detail.height / cropHeight + 0.5);
    }
    const cell = Math.floor(y / core) * columns + Math.floor(x / core);
    const index = variant.faces ? surfaceAt(variant, x, y).material : variant.cells[cell];
    const tex = variant.textures[index];
    const kind = index < 2 ? "平铺底纹" : panel.counts[index] > 1 ? "共享过渡" : "独有过渡";
    panel.root.querySelector(".tile-info").textContent =
      tex.name + " · " + kind + " · " + panel.counts[index] + " 个面引用 · "
      + (tex.size >> level) + " × " + (tex.size >> level) + " px";
  }

  function scheduleOverlay() {
    if (!frame) frame = requestAnimationFrame(() => {
      frame = undefined;
      panels.forEach(renderOverlay);
    });
  }

  function rebake() {
    clearTimeout(queuedBake);
    queuedBake = undefined;
    results = scene === "bend" ? (bendResults ??= bakeBend()) : bake(weights);
    const base = results.original.wad.length, shared = results.shared.wad.length;
    find("savings").textContent = formatMB(base - shared) + "（" +
      ((1 - shared / base) * 100).toFixed(1) + "%）";
    status.textContent = scene === "bend"
      ? "同一条 S 弯。左侧按网格烘焙，右侧按草泥边界切面并沿路铺过渡纹理。"
      : "两版共用同一笔迹，权重和底纹相位一致的过渡块可以复用。";
    for (const panel of panels) {
      const variant = results[panel.id];
      panel.counts = new Uint16Array(variant.textures.length);
      for (const index of variant.cells) panel.counts[index]++;
      panel.root.querySelector(".bytes").textContent = formatMB(variant.wad.length);
      panel.root.querySelector(".stats").textContent = variant.textures.length + " 张 WAD 纹理 · "
        + (variant.textures.length - 2) + " 张过渡纹理";
      renderWad(panel);
    }
    scheduleOverlay();
  }

  function move(event, canvas) {
    const rect = canvas.getBoundingClientRect();
    const previous = position;
    position = {
      x: clamp((event.clientX - rect.left) * width / rect.width, 0, width - 1),
      y: clamp((event.clientY - rect.top) * height / rect.height, 0, height - 1),
    };
    if (stroke === event.pointerId) {
      const brushRadius = Number(radius.value);
      const target = tool === "dirt" ? 255 : 0;
      const dx = position.x - previous.x, dy = position.y - previous.y;
      const steps = Math.max(1, Math.ceil(Math.hypot(dx, dy) / (brushRadius / 4)));
      for (let i = 1; i <= steps; i++)
        paint(weights, previous.x + dx * i / steps, previous.y + dy * i / steps, brushRadius, target);
      if (!queuedBake) queuedBake = setTimeout(rebake, 100);
    }
    scheduleOverlay();
  }

  for (const panel of panels) {
    const { canvas } = panel;
    canvas.addEventListener("pointerdown", (event) => {
      if (event.button !== 0 || stroke !== undefined) return;
      move(event, canvas);
      if (tool === "inspect" || scene === "bend") return;
      stroke = event.pointerId;
      canvas.setPointerCapture(stroke);
      move(event, canvas);
    });
    canvas.addEventListener("pointermove", (event) => {
      if (stroke === undefined || stroke === event.pointerId) move(event, canvas);
    });
    const finish = (event) => {
      if (stroke !== event.pointerId) return;
      stroke = undefined;
      if (queuedBake) rebake();
    };
    canvas.addEventListener("pointerup", finish);
    canvas.addEventListener("pointercancel", finish);
    canvas.addEventListener("lostpointercapture", finish);
    for (const button of panel.root.querySelectorAll("[data-file]")) {
      button.addEventListener("click", () => {
        if (queuedBake) rebake();
        const variant = results[panel.id], type = button.dataset.file;
        const data = type === "map" ? makeMap(variant) : variant.wad;
        const url = URL.createObjectURL(new Blob([data], { type: "application/octet-stream" }));
        const link = document.createElement("a");
        link.href = url;
        link.download = variant.id + "." + type;
        link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
      });
    }
  }

  function segment(id, onChange) {
    const group = find(id);
    group.addEventListener("click", (event) => {
      if (event.target.tagName !== "BUTTON") return;
      group.querySelector('[aria-pressed="true"]').setAttribute("aria-pressed", "false");
      event.target.setAttribute("aria-pressed", "true");
      onChange(event.target.dataset);
      scheduleOverlay();
    });
  }

  segment("tools", (data) => { if (scene === "paint") tool = data.tool; });
  segment("mips", (data) => { level = Number(data.mip); panels.forEach(renderWad); });
  segment("scenes", (data) => {
    scene = data.scene;
    const bend = scene === "bend";
    for (const button of find("tools").querySelectorAll("[data-tool]")) {
      button.disabled = bend && button.dataset.tool !== "inspect";
      button.setAttribute("aria-pressed", String(button.dataset.tool === (bend ? "inspect" : tool)));
    }
    radius.disabled = bend;
    find("reset").disabled = bend;
    document.querySelector(".legend").hidden = bend;
    find("shared").querySelector("h2").textContent = bend ? "三材质过渡带" : "共享过渡纹理";
    position = { x: 4 * core, y: bend ? 228 : 191 };
    rebake();
    renderGeometry();
  });
  radius.addEventListener("input", () => {
    find("radius-value").value = radius.value;
    scheduleOverlay();
  });
  find("grid").addEventListener("change", scheduleOverlay);
  find("reset").addEventListener("click", () => { weights = initialWeights(); rebake(); });

  function renderGeometry() {
    for (const { id, root } of panels) {
      const variant = results[id];
      const faceCount = Array.from(makeMap(variant).matchAll(/^\( /gm)).length;
      const tileSize = core + 2 * gutter;
      const bsp = root.querySelector(".bsp");
      const measurement = bendBsp[id];
      bsp.hidden = scene !== "bend";
      bsp.textContent = `BSP 实测 ${formatMB(measurement.bytes)} · ${measurement.faces} 个面`;
      root.querySelector(".geometry").textContent =
        `地形 ${columns * worldBlockSize} × ${rows * worldBlockSize} 单位 · ${variant.cells.length} 个顶面 · MAP 共 ${faceCount} 个 Brush 面`;
      root.querySelector(".tile-size").textContent = variant.faces
        ? `过渡带沿 Y 轴宽 ${(variant.transitionWidth * worldBlockSize / core).toFixed(1)} 单位 · 过渡贴图 ${variant.textures[2].size} × ${variant.textures[2].size} px`
        : `单块 ${worldBlockSize} × ${worldBlockSize} 单位 · 过渡贴图 ${tileSize} × ${tileSize} px（有效区 ${core} × ${core}）`;
    }
  }
  rebake();
  renderGeometry();
})();
