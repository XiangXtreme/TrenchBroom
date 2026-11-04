"use strict";

(() => {
  const { columns, rows, core, gutter, width, height, initialWeights, paint, bake, makeMap } =
    globalThis.TerrainBake;
  const find = (id) => document.getElementById(id);
  const radius = find("radius");
  const number = new Intl.NumberFormat("zh-CN");
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
  let results;
  let tool = "inspect", level = 0;
  let position = { x: 4 * core, y: 191 };
  let stroke, queuedBake, frame;
  const status = find("status");
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

  function renderWad(panel) {
    const variant = results[panel.id];
    const step = 1 << level;
    const side = core / step;
    panel.image.width = width / step;
    panel.image.height = height / step;
    const ctx = panel.image.getContext("2d");
    const image = ctx.createImageData(panel.image.width, panel.image.height);

    variant.cells.forEach((index, cell) => {
      const { size, colors, levels } = variant.textures[index];
      const tileSide = size / step;
      const startX = (cell % columns) * side, startY = Math.floor(cell / columns) * side;
      for (let y = 0; y < side; y++) {
        for (let x = 0; x < side; x++) {
          const source = index < 2
            ? ((startY + y) % tileSide) * tileSide + (startX + x) % tileSide
            : (y + gutter / step) * tileSide + x + gutter / step;
          const color = levels[level][source] * 3;
          const output = ((startY + y) * image.width + startX + x) * 4;
          image.data[output] = colors[color];
          image.data[output + 1] = colors[color + 1];
          image.data[output + 2] = colors[color + 2];
          image.data[output + 3] = 255;
        }
      }
    });
    ctx.putImageData(image, 0, 0);
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
      ctx.beginPath();
      for (let x = core; x < width; x += core) {
        ctx.moveTo(x + 0.5, 0);
        ctx.lineTo(x + 0.5, height);
      }
      for (let y = core; y < height; y += core) {
        ctx.moveTo(0, y + 0.5);
        ctx.lineTo(width, y + 0.5);
      }
      ctx.stroke();
      ctx.fillStyle = "#c8ed8a";
      variant.cells.forEach((index, cell) => {
        if (index >= 2 && panel.counts[index] > 1)
          ctx.fillRect((cell % columns) * core + 7, Math.floor(cell / columns) * core + 7, 10, 10);
      });
    }

    const { x, y } = position;
    ctx.strokeStyle = "#f7f9dd";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    if (tool === "inspect") {
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
      zoom.beginPath();
      for (let boundary = core; boundary < width; boundary += core) {
        const offset = (boundary - cropX) * detail.width / cropWidth;
        zoom.moveTo(offset + 0.5, 0);
        zoom.lineTo(offset + 0.5, detail.height);
      }
      for (let boundary = core; boundary < height; boundary += core) {
        const offset = (boundary - cropY) * detail.height / cropHeight;
        zoom.moveTo(0, offset + 0.5);
        zoom.lineTo(detail.width, offset + 0.5);
      }
      zoom.stroke();
    }
    const cell = Math.floor(y / core) * columns + Math.floor(x / core);
    const index = variant.cells[cell];
    const tex = variant.textures[index];
    const kind = index < 2 ? "平铺底纹" : panel.counts[index] > 1 ? "共享过渡" : "独有过渡";
    panel.root.querySelector(".tile-info").textContent =
      tex.name + " · " + kind + " · " + panel.counts[index] + " 块引用 · "
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
    results = bake(weights);
    const base = results.original.wad.length, shared = results.shared.wad.length;
    find("savings").textContent = number.format(base - shared) + " B（" +
      ((1 - shared / base) * 100).toFixed(1) + "%）";
    status.textContent = "两版共用同一笔迹，权重和底纹相位一致的过渡块可以复用。";
    for (const panel of panels) {
      const variant = results[panel.id];
      panel.counts = new Uint16Array(variant.textures.length);
      for (const index of variant.cells) panel.counts[index]++;
      panel.root.querySelector(".bytes").textContent = number.format(variant.wad.length) + " B";
      panel.root.querySelector(".stats").textContent = variant.textures.length + " 张 WAD 纹理 · "
        + (variant.textures.length - 2) + " 张过渡纹理 · " + variant.cells.length + " 个地形块";
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
      if (tool === "inspect") return;
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

  segment("tools", (data) => { tool = data.tool; });
  segment("mips", (data) => { level = Number(data.mip); panels.forEach(renderWad); });
  radius.addEventListener("input", () => {
    find("radius-value").value = radius.value;
    scheduleOverlay();
  });
  find("grid").addEventListener("change", scheduleOverlay);
  find("reset").addEventListener("click", () => { weights = initialWeights(); rebake(); });
  rebake();
})();
