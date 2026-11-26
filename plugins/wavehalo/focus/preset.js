"use strict";

// Fixed angular bins, driven by the three bands supplied by Focus API 1 (not a second audio stream).
document.currentScript.mountFocus = ({root, background, renderCover, requestLevels, reduceMotion}) => {
  const wash = document.createElement("div");
  wash.className = "halo-wash";
  background.append(wash);
  root.innerHTML = `<div class="halo-stage" aria-hidden="true">
    <div class="halo-rim"></div><div class="halo-art"></div>
    <svg class="halo-ring" viewBox="-256 -256 512 512"><path/></svg>
  </div>`;
  const stage = root.firstElementChild, art = stage.querySelector(".halo-art");
  const path = stage.querySelector("path"), count = 128, radius = 160;
  const bins = Array.from({length: count}, (_, i) => {
    const angle = i * 2 * Math.PI / count;
    const x = Math.cos(angle), y = Math.sin(angle);
    // Paired lobes keep the ring balanced. Sharper treble accents form a few long needles.
    const lobe = (center, width) => Math.exp(-(Math.sin(angle - center) ** 2) / width);
    return {x, y, base: 6 + 1.5 * Math.cos(4 * angle),
      bass: 34 * lobe(1.32, .075), mid: 27 * lobe(.28, .036),
      air: 46 * lobe(-.58, .009), ripple: .55 + .45 * Math.cos(7 * angle) ** 2};
  });
  const energy = [0, 0, 0], target = [0, 0, 0];
  let active = false, unsupported = false, frame = 0, last = 0, received = 0;

  function draw() {
    const d = bins.map(({x,y,base,bass,mid,air,ripple}) => {
      const pulse = ripple * (bass * energy[0] + mid * energy[1] + air * energy[2]);
      const inner = radius - pulse, outer = radius + base + pulse;
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
      const old = energy[i], gap = target[i] - old;
      energy[i] = Math.abs(gap) < .004 ? target[i] : old + gap * (1 - Math.exp(-dt / (gap > 0 ? 32 : 180)));
      changed ||= energy[i] !== old;
    }
    if (changed) draw();
    if (changed || target.some(v => v > 0)) frame = requestAnimationFrame(tick);
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
      // The host supplies live levels only in these states. The idle visuals run independently.
      listen(playing && !document.hidden && !reduceMotion.matches && !unsupported);
    },
    levels(values) {
      if (!active) return;
      if (values === null) { unsupported = true; listen(false); return; }
      received = performance.now();
      for (let i = 0; i < 3; i++) target[i] = Math.max(0, Math.min(1, (Number(values[i]) || 0) / 100));
      if (!frame && (target.some(v => v > 0) || energy.some(v => v > 0))) frame = requestAnimationFrame(tick);
    },
    dispose() { listen(false); }
  };
};
