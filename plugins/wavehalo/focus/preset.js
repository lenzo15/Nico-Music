"use strict";

document.currentScript.mountFocus = ({ root, background, renderCover, requestLevels, reduceMotion }) => {
  // 插件参数：修改后保存即可热重载。
  const tuning = {
    bass: 100, mid: 30, high: 60,         // 低、中、高频振幅
    ring: 65, width: 100, inward: 100,   // 峰间响应、尖峰宽度、向内延伸（%）
    attack: 10, release: 10,             // 光条上升 / 回落时间（ms）
    fadeInMs: 400, fadeOutMs: 400,      // 开始 / 暂停或结束时光条淡入淡出（ms）
    bassBrightness: 150,                 // 低音提亮强度（0–200%，100 为原效果，0 关闭）
    colorAt: 72, brightenMs: 30,        // 低音提亮阈值（%）、提亮时间（ms）
  };

  const wash = document.createElement("div");
  wash.className = "halo-wash";
  background.append(wash);
  root.innerHTML = `<div class="halo-stage" aria-hidden="true">
    <div class="halo-rim"></div><div class="halo-art"></div>
    <svg class="halo-ring" viewBox="-256 -256 512 512">
      <defs>
        <!-- Keep the mask boundary outside the paint surface at fractional scales. -->
        <mask id="halo-bars" maskUnits="userSpaceOnUse" x="-288" y="-288" width="576" height="576">
          <path stroke="white" fill="none"/>
        </mask>
        <filter id="halo-palette" color-interpolation-filters="sRGB">
          <feColorMatrix values="1 0 0 0 0  0 1 0 0 0  0 0 1 0 0  .213 .715 .072 0 0"/>
          <feComponentTransfer><feFuncA type="discrete" tableValues="1 .5"/></feComponentTransfer>
          <!-- Each output channel = .47 * overall luma - .72 * source channel
               + a light/dark offset. Complementary chroma, not a list of swatches. -->
          <feColorMatrix values="-.62 .336 .034 1.48 -.42  .100 -.384 .034 1.48 -.42  .100 .336 -.686 1.48 -.42  0 0 0 0 1"/>
        </filter>
      </defs>
      <g class="halo-glow"><g mask="url(#halo-bars)"><circle r="250" fill="white"/><foreignObject x="-256" y="-256" width="512" height="512"><canvas width="1" height="1"></canvas></foreignObject></g></g>
      <g mask="url(#halo-bars)"><circle r="250" fill="white"/><foreignObject x="-256" y="-256" width="512" height="512"><canvas width="1" height="1"></canvas></foreignObject></g>
    </svg>
  </div>
  <div class="halo-caption"><h2 id="haloTitle"></h2></div>`;
  const title = root.querySelector("#haloTitle");
  const art = root.querySelector(".halo-art"), path = root.querySelector(".halo-ring path");
  const ring = root.querySelector(".halo-ring");
  ring.style.setProperty("--halo-boost", String(Math.max(0, Math.min(200, tuning.bassBrightness)) / 100));
  ring.style.setProperty("--halo-brighten-time", `${tuning.brightenMs}ms`);
  ring.style.setProperty("--halo-fade-in", `${tuning.fadeInMs}ms`);
  ring.style.setProperty("--halo-fade-out", `${tuning.fadeOutMs}ms`);
  let observed = null, pending;
  function syncInk() {
    const img = art.querySelector("img");
    if (img === observed) return;
    pending?.abort(); observed = img;
    if (!img) { ring.removeAttribute("data-ink"); return; }
    pending = new AbortController();
    const failed = () => { if (observed === img) ring.removeAttribute("data-ink"); };
    const apply = () => {
      if (observed !== img) return;
      if (!img.naturalWidth) return failed();
      // Average the entire decoded cover by successively halving a 64px thumbnail.
      // Drawing cross-origin pixels is allowed; never read or export the canvas.
      let from = img;
      for (let size = 64; size >= 1; size /= 2) {
        const tile = document.createElement("canvas"); tile.width = tile.height = size;
        const ctx = tile.getContext("2d");
        if (size === 64) { ctx.fillStyle = "#625971"; ctx.fillRect(0, 0, size, size); }
        ctx.drawImage(from, 0, 0, size, size); from = tile;
      }
      for (const canvas of ring.querySelectorAll("canvas")) canvas.getContext("2d").drawImage(from, 0, 0);
      ring.setAttribute("data-ink", "");
    };
    if (img.complete) apply();
    else {
      const options = {once: true, signal: pending.signal};
      img.addEventListener("load", apply, options); img.addEventListener("error", failed, options);
    }
  }
  let accented = false;
  function updateColor(bass) {
    // Absolute meter level with a dead band, independent of bar gain and smoothing.
    const next = bass >= (tuning.colorAt - (accented ? 8 : 0)) / 100;
    if (next !== accented) { accented = next; ring.toggleAttribute("data-bass", next); }
  }
  // One fixed frequency axis, starting at the upper right. Different notes
  // occupy different spokes; no mirrored lobes, synthetic beats or random motion.
  const count = 128, radius = 160;
  const bins = Array.from({ length: count }, (_, i) => {
    const angle = i * 2 * Math.PI / count - Math.PI / 4;
    const hz = (3 + 40 * Math.expm1(Math.log1p(1536 / 40) * (i + .5) / count)) * 48000 / 4096;
    const mix = (lo, hi) => Math.max(0, Math.min(1, Math.log(hz / lo) / Math.log(hi / lo)));
    return { x: Math.cos(angle), y: Math.sin(angle), low: 1 - mix(140, 240), high: mix(2400, 4200) };
  });
  const raw = new Float32Array(count), target = new Float32Array(count), energy = new Float32Array(count);
  let bassLevel = 0, active = false, unsupported = false, frame = 0, last = 0, received = 0;
  function mapSpectrum() {
    const floor = Math.min(...raw), bed = tuning.ring / 100;
    const at = i => raw[Math.max(0, Math.min(count - 1, i))];
    for (let i = 0; i < count; i++) {
      const { low, high } = bins[i];
      const gain = low * tuning.bass + (1 - low - high) * tuning.mid + high * tuning.high;
      // Compare neighbours in THIS spectrum, not successive packets. Keep a
      // little texture between peaks without pumping the entire circumference.
      const surround = (at(i - 3) + at(i - 2) + at(i + 2) + at(i + 3)) / 4;
      const ridge = Math.max(0, raw[i] - surround);
      const body = raw[i] - floor;
      target[i] = Math.min(80, gain * (2.7 * ridge + 1.6 * bed * body) * body);
    }
  }
  function draw() {
    const shoulder = .32 * (tuning.width / 100) ** 2;
    const d = bins.map(({ x, y }, i) => {
      // Width affects only the short shoulders; a narrow note stays a needle.
      const pulse = Math.min(80, Math.max(energy[i], shoulder * (energy[i - 1] || 0), shoulder * (energy[i + 1] || 0)));
      const inner = radius - pulse * tuning.inward / 100, outer = radius + 5 + pulse;
      return `M${(x * inner).toFixed(2)},${(y * inner).toFixed(2)}L${(x * outer).toFixed(2)},${(y * outer).toFixed(2)}`;
    });
    path.setAttribute("d", d.join(""));
  }
  function tick(now) {
    frame = 0;
    const dt = Math.min(80, now - (last || now - 16)); last = now;
    if (now - received > 160) { raw.fill(0); target.fill(0); bassLevel = 0; updateColor(0); }
    let changed = false;
    for (let i = 0; i < count; i++) {
      const gap = target[i] - energy[i], time = gap > 0 ? tuning.attack : tuning.release;
      changed ||= gap !== 0;
      energy[i] = !time || Math.abs(gap) < .004 ? target[i] : energy[i] + gap * (1 - Math.exp(-dt / time));
    }
    if (changed) draw();
    if (changed || bassLevel || target.some(Boolean)) frame = requestAnimationFrame(tick);
    else last = 0;
  }
  function listen(want) {
    if (active === want) return;
    active = want;
    requestLevels(want, { spectrum: true });
    if (!want) {
      // Let the existing render loop release the last peaks during the fade.
      target.fill(0); bassLevel = 0; updateColor(0);
    }
  }
  draw();
  return {
    exitFadeMs: tuning.fadeOutMs,
    update(state) {
      title.textContent = title.title = state.title;
      renderCover(wash); renderCover(art); syncInk();
      const { playing } = state;
      listen(playing && !document.hidden && !reduceMotion.matches && !unsupported);
    },
    levels(values, spectrum) {
      if (!active) return;
      if (values === null) { unsupported = true; listen(false); return; }
      received = performance.now();
      const norm = v => Math.max(0, Math.min(1, (Number(v) || 0) / 100));
      bassLevel = norm(values[0]);
      for (let i = 0; i < count; i++) {
        const { low, high } = bins[i];
        // Older hosts still send three levels: interpolate those, never invent peaks.
        raw[i] = spectrum?.length === count ? norm(spectrum[i]) :
          low * bassLevel + (1 - low - high) * norm(values[1]) + high * norm(values[2]);
      }
      mapSpectrum(); updateColor(bassLevel);
      if (!frame && (bassLevel || target.some(Boolean))) frame = requestAnimationFrame(tick);
    },
    dispose() { pending?.abort(); observed = null; listen(false); cancelAnimationFrame(frame); }
  };
};
