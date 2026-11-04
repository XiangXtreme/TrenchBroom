"use strict";

(() => {
  const { columns, rows, core, gutter, width, height, worldBlockSize,
    initialWeights, paint, bake, bakeBend, makeMap } = globalThis.TerrainBake;
  const multi = globalThis.TerrainMulti;
  const preview = globalThis.TerrainPreview;
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
    const backdrop = document.createElement("canvas");
    backdrop.width = width;
    backdrop.height = height;
    return { id, root, canvas, detail, backdrop, image: document.createElement("canvas") };
  });
  let weights = initialWeights();
  let multiWeights = multi.initialWeights();
  let results, bendResults, displayedScene;
  let scene = "multi";
  let tool = "inspect", level = 0;
  let position = { x: 4 * core, y: 191 };
  let stroke, queuedBake, frame, activeBake, worker;
  let revision = 0, published = 0;
  const status = find("status");
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

  function renderWad(panel) {
    const variant = results[panel.id];
    const step = 1 << level;
    const pixels = variant.previews?.[level] ?? preview.render(variant, level);
    panel.image.width = width / step;
    panel.image.height = height / step;
    panel.image.getContext("2d").putImageData(new ImageData(pixels, width / step, height / step), 0, 0);
    renderBackdrop(panel);
  }

  function drawEdges(ctx, variant, projectX, projectY, kind, bounds) {
    ctx.beginPath();
    if (variant.faces) {
      for (const face of variant.faces) {
        if (kind && face.kind !== kind) continue;
        const { points } = face;
        if (bounds && (points.every(([x]) => x < bounds.left)
          || points.every(([x]) => x > bounds.right)
          || points.every(([, y]) => y < bounds.top)
          || points.every(([, y]) => y > bounds.bottom))) continue;
        ctx.moveTo(projectX(points[0][0]), projectY(points[0][1]));
        for (let i = 1; i < points.length; i++)
          ctx.lineTo(projectX(points[i][0]), projectY(points[i][1]));
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

  function renderBackdrop(panel) {
    const { backdrop, image } = panel;
    const ctx = backdrop.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(image, 0, 0, width, height);
    const variant = results[panel.id];
    if (find("grid").checked) {
      ctx.strokeStyle = "#fff6d648";
      ctx.lineWidth = 1;
      drawEdges(ctx, variant, (x) => x + 0.5, (y) => y + 0.5);
      if (variant.junctionFaces) {
        ctx.strokeStyle = "#ffbd73";
        drawEdges(ctx, variant, (x) => x + 0.5, (y) => y + 0.5, "junction");
      }
      ctx.fillStyle = "#c8ed8a";
      if (displayedScene === "paint") variant.cells.forEach((index, cell) => {
        if (index >= 2 && panel.counts[index] > 1)
          ctx.fillRect((cell % columns) * core + 7, Math.floor(cell / columns) * core + 7, 10, 10);
      });
    }
  }

  function renderOverlay(panel) {
    const { canvas, detail, image, backdrop } = panel;
    const ctx = canvas.getContext("2d");
    ctx.drawImage(backdrop, 0, 0);
    const variant = results[panel.id];
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
    const cropWidth = 64, cropHeight = 16;
    const cropX = clamp(Math.round((x - cropWidth / 2) / step) * step, 0, width - cropWidth);
    const cropY = clamp(Math.round((y - cropHeight / 2) / step) * step, 0, height - cropHeight);
    zoom.imageSmoothingEnabled = false;
    zoom.drawImage(image, cropX / step, cropY / step, cropWidth / step, cropHeight / step,
      0, 0, detail.width, detail.height);
    if (find("grid").checked) {
      zoom.strokeStyle = "#fff6d67a";
      const projectX = (x) => (x - cropX) * detail.width / cropWidth + 0.5;
      const projectY = (y) => (y - cropY) * detail.height / cropHeight + 0.5;
      const bounds = { left: cropX, top: cropY, right: cropX + cropWidth, bottom: cropY + cropHeight };
      drawEdges(zoom, variant, projectX, projectY, undefined, bounds);
      if (variant.junctionFaces) {
        zoom.strokeStyle = "#ffbd73";
        drawEdges(zoom, variant, projectX, projectY, "junction", bounds);
      }
    }
    const cell = Math.floor(y / core) * columns + Math.floor(x / core);
    const surface = variant.faces && preview.surfaceAt(variant, x, y);
    const index = surface ? surface.material : variant.cells[cell];
    const tex = variant.textures[index];
    const kind = surface?.kind === "junction" ? "三材质交汇"
      : surface?.kind === "fit" ? "UV 退化处烘焙"
      : surface?.kind === "patch" ? "合并面烘焙"
      : surface?.kind === "pair" ? "可复用过渡"
      : index < (variant.baseCount ?? 2) ? "平铺底纹"
      : panel.counts[index] > 1 ? "共享过渡" : "独有过渡";
    const info = panel.root.querySelector(".tile-info");
    const text = tex.name + " · " + kind + " · " + panel.counts[index] + " 个面引用 · "
      + (tex.size >> level) + " × " + (tex.size >> level) + " px";
    if (info.textContent !== text) info.textContent = text;
  }

  function scheduleOverlay() {
    if (!frame) frame = requestAnimationFrame(() => {
      frame = undefined;
      if (results) panels.forEach(renderOverlay);
    });
  }

  function showResults(nextResults, nextRevision) {
    results = nextResults;
    displayedScene = scene;
    published = nextRevision;
    const base = results.original.wad.length, shared = results.shared.wad.length;
    find("savings").textContent = formatMB(base - shared) + "（" +
      ((1 - shared / base) * 100).toFixed(1) + "%）";
    status.textContent = scene === "multi"
      ? "右侧复用两材质过渡贴图，在切割密集处合并面。开启面边界可查看橙色交汇区。"
      : scene === "bend"
      ? "同一条 S 弯。左侧按网格烘焙，右侧按草泥边界切面并沿路铺过渡纹理。"
      : "两版共用同一笔迹，权重和底纹相位一致的过渡块可以复用。";
    for (const panel of panels) {
      const variant = results[panel.id];
      panel.counts = new Uint16Array(variant.textures.length);
      for (const index of variant.cells) panel.counts[index]++;
      panel.root.querySelector(".bytes").textContent = formatMB(variant.wad.length);
      const baseCount = variant.baseCount ?? 2;
      panel.root.querySelector(".stats").textContent = variant.textures.length + " 张 WAD 纹理 · "
        + (variant.textures.length - baseCount - (variant.patchCount ?? 0)) + " 张过渡纹理"
        + (variant.grid ? " · " + variant.patchCount + " 张局部烘焙" : "");
      renderWad(panel);
    }
    renderGeometry();
    showPending();
    scheduleOverlay();
  }

  function showPending() {
    const pending = revision !== published;
    find("bake-progress").hidden = !pending;
    for (const { root } of panels)
      for (const button of root.querySelectorAll("[data-file]")) button.disabled = pending;
  }

  function runBake() {
    clearTimeout(queuedBake);
    queuedBake = undefined;
    if (published === revision) return;
    if (scene === "bend") {
      showResults(bendResults ??= bakeBend(), revision);
      return;
    }
    if (activeBake) return;
    if (worker) {
      activeBake = { scene, revision };
      worker.postMessage({ ...activeBake, weights: scene === "multi" ? multiWeights : weights });
    } else if (stroke === undefined) {
      showResults(scene === "multi" ? multi.bake(multiWeights) : bake(weights), revision);
    }
  }

  function queueBake(delay = 0) {
    const wasCurrent = published === revision;
    revision++;
    if (wasCurrent) showPending();
    if (delay === 0) runBake();
    else if (!activeBake && !queuedBake) queuedBake = setTimeout(runBake, delay);
  }

  if (location.protocol !== "file:" && typeof Worker === "function") {
    try {
      worker = new Worker("bake-worker.js");
      worker.onmessage = ({ data }) => {
        activeBake = undefined;
        if (data.scene === scene && data.revision > published)
          showResults(data.results, data.revision);
        runBake();
      };
      worker.onerror = () => {
        worker.terminate();
        worker = undefined;
        activeBake = undefined;
        runBake();
      };
    } catch {
      // Browsers that disallow workers can still bake after the stroke.
    }
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
      const target = ["grass", "dirt", "rock"].indexOf(tool);
      const dx = position.x - previous.x, dy = position.y - previous.y;
      const steps = Math.max(1, Math.ceil(Math.hypot(dx, dy) / (brushRadius / 4)));
      for (let i = 1; i <= steps; i++) {
        const x = previous.x + dx * i / steps, y = previous.y + dy * i / steps;
        if (scene === "multi") multi.paint(multiWeights, x, y, brushRadius, target);
        else paint(weights, x, y, brushRadius, target === 1 ? 255 : 0);
      }
      queueBake(90);
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
      runBake();
    };
    canvas.addEventListener("pointerup", finish);
    canvas.addEventListener("pointercancel", finish);
    canvas.addEventListener("lostpointercapture", finish);
    for (const button of panel.root.querySelectorAll("[data-file]")) {
      button.addEventListener("click", () => {
        if (published !== revision) return;
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
      if (event.target.tagName !== "BUTTON" || (id !== "mips" && stroke !== undefined)) return;
      group.querySelector('[aria-pressed="true"]').setAttribute("aria-pressed", "false");
      event.target.setAttribute("aria-pressed", "true");
      onChange(event.target.dataset);
      scheduleOverlay();
    });
  }

  segment("tools", (data) => { if (scene !== "bend") tool = data.tool; });
  segment("mips", (data) => {
    level = Number(data.mip);
    if (results) panels.forEach(renderWad);
  });
  function changeScene(nextScene) {
    scene = nextScene;
    const bend = scene === "bend";
    const multiple = scene === "multi";
    if (!multiple && tool === "rock") tool = "inspect";
    for (const button of find("tools").querySelectorAll("[data-tool]")) {
      button.disabled = bend && button.dataset.tool !== "inspect";
      button.hidden = button.dataset.tool === "rock" && !multiple;
      button.setAttribute("aria-pressed", String(button.dataset.tool === (bend ? "inspect" : tool)));
    }
    radius.disabled = bend;
    radius.min = multiple ? 28 : 8;
    find("radius-value").value = radius.value;
    find("reset").disabled = bend;
    const legend = document.querySelector(".legend");
    legend.hidden = bend;
    legend.querySelector("i").classList.toggle("junction", multiple);
    legend.querySelector("span").textContent = multiple ? "三材质交汇" : "复用块";
    find("shared").querySelector("h2").textContent = multiple ? "自动 UV + 局部烘焙"
      : bend ? "三材质过渡带" : "共享过渡纹理";
    position = multiple ? { x: 515, y: 186 } : { x: 4 * core, y: bend ? 228 : 191 };
    queueBake();
  }
  segment("scenes", (data) => changeScene(data.scene));
  radius.addEventListener("input", () => {
    find("radius-value").value = radius.value;
    scheduleOverlay();
  });
  find("grid").addEventListener("change", () => {
    if (results) panels.forEach(renderBackdrop);
    scheduleOverlay();
  });
  find("reset").addEventListener("click", () => {
    if (stroke !== undefined) return;
    if (scene === "multi") multiWeights = multi.initialWeights();
    else weights = initialWeights();
    queueBake();
  });

  function renderGeometry() {
    for (const { id, root } of panels) {
      const variant = results[id];
      const faceCount = 30 + (variant.faces
        ? variant.faces.reduce((count, face) => count + face.points.length + 2, 0)
        : variant.cells.length * 6);
      const tileSize = core + 2 * gutter;
      const bsp = root.querySelector(".bsp");
      const measurement = bendBsp[id];
      bsp.hidden = scene !== "bend";
      bsp.textContent = `BSP 实测 ${formatMB(measurement.bytes)} · ${measurement.faces} 个面`;
      root.querySelector(".geometry").textContent =
        `地形 ${columns * worldBlockSize} × ${rows * worldBlockSize} 单位 · ${variant.cells.length} 个顶面 · MAP 共 ${faceCount} 个 Brush 面`;
      root.querySelector(".tile-size").textContent = variant.grid
        ? `合并块 ${variant.mergedPatchFaces} 个 · 三材质交汇 ${variant.junctionFaces} 面 · UV 退化处烘焙 ${variant.fitFaces} 面`
        : variant.faces
        ? `过渡带沿 Y 轴宽 ${(variant.transitionWidth * worldBlockSize / core).toFixed(1)} 单位 · 过渡贴图 ${variant.textures[2].size} × ${variant.textures[2].size} px`
        : `单块 ${worldBlockSize} × ${worldBlockSize} 单位 · 过渡贴图 ${tileSize} × ${tileSize} px（有效区 ${core} × ${core}）`;
    }
  }
  changeScene(scene);
})();
