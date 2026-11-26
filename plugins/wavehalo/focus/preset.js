"use strict";

document.currentScript.mountFocus = ({root, background, renderCover, requestLevels, reduceMotion, signal}) => {
  // [label, default, min, max, unit]. Settings belong only to this preset.
  const fields = {
    bass: ["低频振幅", 34, 0, 80, ""], mid: ["中频振幅", 27, 0, 80, ""], high: ["高频振幅", 46, 0, 80, ""],
    colorAt: ["低音提亮阈值", 72, 10, 100, "%"], brightenMs: ["提亮时间", 150, 0, 1000, " ms"],
    ring: ["整圈响应", 65, 0, 100, "%"], width: ["尖峰宽度", 100, 10, 180, "%"], inward: ["向内延伸", 100, 0, 150, "%"],
    attack: ["上升时间", 12, 0, 250, " ms"], release: ["回落时间", 85, 0, 500, " ms"],
  };
  const storageKey = "wavehalo.tuning.v1";
  const defaults = Object.fromEntries(Object.entries(fields).map(([k, [, value]]) => [k, value]));
  const tuning = {...defaults};
  try {
    const saved = JSON.parse(localStorage.getItem(storageKey));
    for (const [k, [,,min,max]] of Object.entries(fields)) {
      if (Number.isInteger(saved?.[k])) tuning[k] = Math.max(min, Math.min(max, saved[k]));
    }
  } catch {} // Storage can be unavailable; live tuning still works.

  const wash = document.createElement("div");
  wash.className = "halo-wash";
  background.append(wash);
  root.innerHTML = `<div class="halo-stage" aria-hidden="true">
    <div class="halo-rim"></div><div class="halo-art"></div>
    <svg class="halo-ring" viewBox="-256 -256 512 512"><path/></svg>
  </div><details class="halo-tuner"><summary role="button" title="调节声谱参数" aria-label="调节声谱参数">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" aria-hidden="true"><path d="M3 7h8m5 0h5M3 17h3m5 0h10"/><circle cx="13.5" cy="7" r="2.5"/><circle cx="8.5" cy="17" r="2.5"/></svg>
  </summary><div class="halo-controls"><strong>声谱参数</strong>${Object.entries(fields).map(([k,[label,,min,max,unit]]) =>
    `<label>${label}<span aria-hidden="true"><b data-value="${k}"></b>${unit}</span><input type="range" data-key="${k}" min="${min}" max="${max}"></label>`).join("")}
    <button type="button">恢复默认</button></div></details>`;
  const art = root.querySelector(".halo-art"), path = root.querySelector(".halo-ring path");
  const ring = root.querySelector(".halo-ring");
  let accented = false;
  function updateColor(bass) {
    // Absolute meter level with a dead band, independent of bar gain and smoothing.
    const next = bass >= (tuning.colorAt - (accented ? 8 : 0)) / 100;
    if (next !== accented) { accented = next; ring.toggleAttribute("data-bass", next); }
  }
  const panel = root.querySelector(".halo-tuner");
  const sliders = [...panel.querySelectorAll("input")];
  function syncControls() {
    ring.style.setProperty("--halo-brighten-time", `${tuning.brightenMs}ms`);
    for (const input of sliders) {
      const k = input.dataset.key;
      input.value = tuning[k];
      input.setAttribute("aria-valuetext", tuning[k] + fields[k][4]);
      panel.querySelector(`[data-value="${k}"]`).textContent = tuning[k];
    }
  }
  syncControls();
  function apply() {
    mapSpectrum(); draw(); updateColor(bassLevel); syncControls();
    try { localStorage.setItem(storageKey, JSON.stringify(tuning)); } catch {}
  }
  panel.oninput = ({target}) => { tuning[target.dataset.key] = target.valueAsNumber; apply(); };
  panel.querySelector("button").onclick = () => { Object.assign(tuning, defaults); apply(); };
  document.addEventListener("pointerdown", e => { if (panel.open && !panel.contains(e.target)) panel.open = false; }, {signal});
  document.addEventListener("keydown", e => {
    if (panel.open && e.key === "Escape") {
      panel.open = false; panel.querySelector("summary").focus(); e.preventDefault(); e.stopPropagation();
    }
  }, {capture: true, signal});

  // One fixed frequency axis, starting at the upper right. Different notes
  // occupy different spokes; no mirrored lobes, synthetic beats or random motion.
  const count = 128, radius = 160;
  const bins = Array.from({length: count}, (_, i) => {
    const angle = i * 2 * Math.PI / count - Math.PI / 4;
    const hz = (3 + 40 * Math.expm1(Math.log1p(1536 / 40) * (i + .5) / count)) * 48000 / 4096;
    const mix = (lo, hi) => Math.max(0, Math.min(1, Math.log(hz / lo) / Math.log(hi / lo)));
    return {x: Math.cos(angle), y: Math.sin(angle), low: 1 - mix(140, 240), high: mix(2400, 4200)};
  });
  const raw = new Float32Array(count), target = new Float32Array(count), energy = new Float32Array(count);
  let bassLevel = 0, active = false, unsupported = false, frame = 0, last = 0, received = 0;
  function mapSpectrum() {
    const floor = Math.min(...raw), bed = tuning.ring / 100;
    const at = i => raw[Math.max(0, Math.min(count - 1, i))];
    for (let i = 0; i < count; i++) {
      const {low, high} = bins[i];
      const gain = low * tuning.bass + (1 - low - high) * tuning.mid + high * tuning.high;
      // Compare neighbours in THIS spectrum, not successive packets. Keep a
      // little texture between peaks without pumping the entire circumference.
      const surround = (at(i-3) + at(i-2) + at(i+2) + at(i+3)) / 4;
      const ridge = Math.max(0, raw[i] - surround);
      const body = raw[i] - floor;
      target[i] = Math.min(80, gain * (2.7 * ridge + 1.6 * bed * body) * body);
    }
  }
  function draw() {
    const shoulder = .32 * (tuning.width / 100) ** 2;
    const d = bins.map(({x,y}, i) => {
      // Width affects only the short shoulders; a narrow note stays a needle.
      const pulse = Math.min(80, Math.max(energy[i], shoulder * (energy[i-1] || 0), shoulder * (energy[i+1] || 0)));
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
    requestLevels(want, {spectrum: true});
    if (!want) {
      cancelAnimationFrame(frame); frame = last = 0;
      energy.fill(0); raw.fill(0); target.fill(0); bassLevel = 0; updateColor(0); draw();
    }
  }
  draw();
  return {
    update({playing}) {
      renderCover(wash); renderCover(art);
      listen(playing && !document.hidden && !reduceMotion.matches && !unsupported);
    },
    levels(values, spectrum) {
      if (!active) return;
      if (values === null) { unsupported = true; listen(false); return; }
      received = performance.now();
      const norm = v => Math.max(0, Math.min(1, (Number(v) || 0) / 100));
      bassLevel = norm(values[0]);
      for (let i = 0; i < count; i++) {
        const {low, high} = bins[i];
        // Older hosts still send three levels: interpolate those, never invent peaks.
        raw[i] = spectrum?.length === count ? norm(spectrum[i]) :
          low * bassLevel + (1 - low - high) * norm(values[1]) + high * norm(values[2]);
      }
      mapSpectrum(); updateColor(bassLevel);
      if (!frame && (bassLevel || target.some(Boolean))) frame = requestAnimationFrame(tick);
    },
    dispose() { listen(false); }
  };
};
