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
  const bands = [[28, 49, 85, 110], [8, 38, 68, 98], [115, 17, 57, 78]].map(anchors => ({
    anchors, previous: 0, target: 0, energy: 0, slot: 0, lastAttack: -Infinity, hits: [0, 0, 0, 0],
  }));
  const bins = Array.from({length: count}, (_, i) => {
    const angle = i * 2 * Math.PI / count;
    return {x: Math.cos(angle), y: Math.sin(angle), base: 6 + 1.5 * Math.cos(4 * angle),
      band: bands[i % 3], gain: 2 + (i * 7 % 11) / 2, slot: -1, accent: 0};
  });
  // A transient gets a single tall needle and two short shoulders, not a broad lobe.
  bands.forEach((band, b) => band.anchors.forEach((i, slot) => {
    for (const [offset, weight] of [[-1, .12], [0, 1], [1, .20]]) {
      Object.assign(bins[i + offset], {band, slot, gain: 3 + 16 * weight, accent: [59, 48, 54][b] * weight});
    }
  }));
  let active = false, unsupported = false, frame = 0, last = 0, received = 0;

  function draw() {
    const d = bins.map(({x,y,base,band,gain,slot,accent}) => {
      const pulse = gain * band.energy + (slot < 0 ? 0 : accent * band.hits[slot]);
      const inner = radius - pulse, outer = radius + base + pulse;
      return `M${(x * inner).toFixed(2)},${(y * inner).toFixed(2)}L${(x * outer).toFixed(2)},${(y * outer).toFixed(2)}`;
    });
    path.setAttribute("d", d.join(""));
  }
  function tick(now) {
    frame = 0;
    const dt = Math.min(80, now - (last || now - 16)); last = now;
    let changed = false;
    for (const band of bands) {
      if (now - received > 100) band.target = band.previous = 0;
      const gap = band.target - band.energy;
      band.energy = Math.abs(gap) < .002 ? band.target : band.energy + gap * (1 - Math.exp(-dt / (gap > 0 ? 8 : 52)));
      changed ||= gap !== 0 || band.hits.some(Boolean);
      for (let j = 0; j < band.hits.length; j++) if (band.hits[j] < .004) band.hits[j] = 0;
    }
    if (changed) draw(); // Show the attack at full height before starting its release.
    const decay = Math.exp(-dt / 56);
    for (const band of bands) for (let j = 0; j < band.hits.length; ++j) {
      band.hits[j] *= decay;
    }
    if (bands.some(b => b.target || b.energy || b.hits.some(Boolean))) frame = requestAnimationFrame(tick);
    else last = 0;
  }
  function listen(want) {
    if (active === want) return;
    active = want;
    requestLevels(want);
    if (!want) {
      cancelAnimationFrame(frame); frame = last = 0;
      for (const b of bands) {
        b.energy = b.target = b.previous = 0; b.hits.fill(0); b.lastAttack = -Infinity;
      }
      draw();
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
      bands.forEach((band, i) => {
        const level = Math.max(0, Math.min(1, (Number(values[i]) || 0) / 100));
        // The host reports compressed dB levels. Accentuate packet-to-packet rises
        // so a drum hit can stand out even in an already loud passage.
        const rise = level - band.previous;
        band.previous = level;
        band.target = level * level;
        if (rise <= .015 || level < .08) return;
        if (received - band.lastAttack > 90) band.slot = (band.slot + 1) % band.hits.length;
        band.lastAttack = received;
        const hit = Math.min(1, (rise - .015) * 7, level * 2);
        const opposite = (band.slot + 2) % band.hits.length;
        band.hits[band.slot] = Math.max(band.hits[band.slot], hit);
        band.hits[opposite] = Math.max(band.hits[opposite], hit * .47);
      });
      if (!frame && bands.some(b => b.target > 0)) frame = requestAnimationFrame(tick);
    },
    dispose() { listen(false); }
  };
};
