"use strict";

document.currentScript.mountFocus = ({root, background, renderCover, requestLevels, reduceMotion, signal}) => {
  // [label, default, min, max, unit]. Settings belong only to this preset.
  const fields = {
    bass: ["低频振幅", 34, 0, 80, ""], mid: ["中频振幅", 27, 0, 80, ""], high: ["高频振幅", 46, 0, 80, ""],
    ring: ["整圈响应", 65, 0, 100, "%"], width: ["尖峰宽度", 100, 10, 180, "%"], inward: ["向内延伸", 100, 0, 150, "%"],
    attack: ["上升时间", 32, 0, 250, " ms"], release: ["回落时间", 180, 0, 500, " ms"],
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
  const panel = root.querySelector(".halo-tuner");
  const sliders = [...panel.querySelectorAll("input")];
  function syncControls() {
    for (const input of sliders) {
      const k = input.dataset.key;
      input.value = tuning[k];
      input.setAttribute("aria-valuetext", tuning[k] + fields[k][4]);
      panel.querySelector(`[data-value="${k}"]`).textContent = tuning[k];
    }
  }
  syncControls();
  function apply() {
    updateWeights(); draw(); syncControls();
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

  const radius = 160, count = 128;
  const bins = Array.from({length: count}, (_, i) => {
    const angle = i * 2 * Math.PI / count;
    return {angle, x: Math.cos(angle), y: Math.sin(angle), base: 6 + 1.5 * Math.cos(4 * angle)};
  });
  function updateWeights() {
    for (const bin of bins) {
      const lobe = (center, width) => Math.exp(-(Math.sin(bin.angle - center) ** 2) / (width * (tuning.width / 100) ** 2));
      const ripple = .55 + .45 * Math.cos(7 * bin.angle) ** 2;
      // The host supplies three levels, not 128 FFT bins. Crossfade them around
      // the whole circle, then lay the narrow accents on top without raising their ceiling.
      const weight = (phase, peak) => {
        const blend = (1 + Math.cos(2 * bin.angle - phase)) / 2;
        const bed = tuning.ring / 100 * (.22 + .38 * blend * blend);
        const accent = ripple * peak;
        return bed + (1 - bed) * accent;
      };
      bin.bass = tuning.bass * weight(0, lobe(1.32, .075));
      bin.mid = tuning.mid * weight(2 * Math.PI / 3, lobe(.28, .036));
      bin.high = tuning.high * weight(4 * Math.PI / 3, lobe(-.58, .009));
    }
  }
  updateWeights();
  const energy = [0, 0, 0], target = [0, 0, 0];
  let active = false, unsupported = false, frame = 0, last = 0, received = 0;
  function draw() {
    // Remove the spatial floor of this frame, not the change since the last packet.
    let floor = Infinity;
    for (const bin of bins) {
      bin.level = bin.bass * energy[0] + bin.mid * energy[1] + bin.high * energy[2];
      floor = Math.min(floor, bin.level);
    }
    const d = bins.map(({x,y,base,level}) => {
      const pulse = Math.min(80, level - floor);
      const inner = radius - pulse * tuning.inward / 100, outer = radius + base + pulse;
      return `M${(x * inner).toFixed(2)},${(y * inner).toFixed(2)}L${(x * outer).toFixed(2)},${(y * outer).toFixed(2)}`;
    });
    path.setAttribute("d", d.join(""));
  }
  function tick(now) {
    frame = 0;
    const dt = Math.min(80, now - (last || now - 16)); last = now;
    if (now - received > 160) target.fill(0);
    let changed = false;
    for (let i = 0; i < 3; i++) {
      const gap = target[i] - energy[i], time = gap > 0 ? tuning.attack : tuning.release;
      changed ||= gap !== 0;
      energy[i] = !time || Math.abs(gap) < .004 ? target[i] : energy[i] + gap * (1 - Math.exp(-dt / time));
    }
    if (changed) draw();
    if (changed || target.some(Boolean)) frame = requestAnimationFrame(tick);
    else last = 0;
  }
  function listen(want) {
    if (active === want) return;
    active = want;
    requestLevels(want);
    if (!want) {
      cancelAnimationFrame(frame); frame = last = 0;
      energy.fill(0); target.fill(0); draw();
    }
  }
  draw();
  return {
    update({playing}) {
      renderCover(wash); renderCover(art);
      listen(playing && !document.hidden && !reduceMotion.matches && !unsupported);
    },
    levels(values) {
      if (!active) return;
      if (values === null) { unsupported = true; listen(false); return; }
      received = performance.now();
      for (let i = 0; i < 3; i++) target[i] = Math.max(0, Math.min(1, (Number(values[i]) || 0) / 100));
      if (!frame && target.some(Boolean)) frame = requestAnimationFrame(tick);
    },
    dispose() { listen(false); }
  };
};
