"use strict";
(() => {
function createAurora(canvas, button, api) {
  const ctx = canvas.getContext("2d");
  let enabled = true, playing = false, active = false, phase = 0, received = -Infinity;
  let unavailable = !ctx, ink;
  const target = [0, 0, 0], energy = [0, 0, 0];
  try { enabled = localStorage.getItem("focusAurora") !== "off"; } catch {}
  function sync() {
    button.disabled = unavailable || api.reduceMotion.matches;
    button.title = unavailable ? "当前设备不支持此特效" : api.reduceMotion.matches ? "减少动态效果已启用" : "切换可视化特效";
    button.setAttribute("aria-pressed", String(enabled && !button.disabled));
    const want = playing && enabled && !button.disabled;
    if (want === active) return;
    active = want;
    canvas.hidden = true; received = -Infinity; energy.fill(0); target.fill(0);
    api.requestLevels(want);
  }
  button.onclick = () => {
    enabled = !enabled;
    try { localStorage.setItem("focusAurora", enabled ? "on" : "off"); } catch {}
    sync();
  };
  function levels(values) {
    if (!active) return;
    if (!values) { unavailable = true; sync(); return; }
    for (let i = 0; i < 3; ++i) target[i] = Math.max(0, Math.min(1, values[i]/100));
    received = performance.now();
  }
  function draw(now, dt) {
    if (!active) return;
    for (let i = 0; i < 3; ++i) {
      const v = now - received < 160 ? target[i] : 0;
      energy[i] += (v-energy[i]) * (1-Math.exp(-dt/(v > energy[i] ? 32 : 190)));
    }
    const [bass, mid, air] = energy, power = Math.max(...energy);
    if (power < .008) { canvas.hidden = true; return; }
    canvas.hidden = false;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    const dpr = Math.min(window.devicePixelRatio, 1.5);
    const width = Math.round(w*dpr), height = Math.round(h*dpr);
    if (canvas.width !== width || canvas.height !== height || !ink) {
      canvas.width = width; canvas.height = height;
      ctx.setTransform(width/w, 0, 0, height/h, 0, 0);
      ink = ctx.createLinearGradient(0, h*.7, w, h*.3);
      for (const [p,c] of [[0,"#38d2c4"],[.26,"#a1fff3"],[.52,"#7772ff"],[.76,"#f7b0ef"],[1,"#ffc786"]]) ink.addColorStop(p,c);
    }
    phase += dt * (.00028 + bass*.00065 + air*.00025);
    ctx.clearRect(0,0,w,h);
    ctx.globalCompositeOperation = "lighter"; ctx.lineJoin = ctx.lineCap = "round";
    ctx.strokeStyle = ink;
    // Two tilted sheets of light. Twisting their cross sections changes the
    // apparent thickness; their centres pass behind the record, not over it.
    for (let side = -1; side <= 1; side += 2) {
      const line = (u, strand) => {
        const x = (u-.5)*2, taper = Math.sin(u*Math.PI);
        const twist = 7*u + side*phase;
        const spine = .085*Math.sin(5*u + phase*side) + side*x*(.09+bass*.13);
        const curtain = taper * strand*(.05+.12*mid)*Math.cos(twist + strand*.42);
        const ripple = taper*.026*air*Math.sin(16*u-phase*2+strand*2);
        return h*(.5 + spine + curtain + ripple);
      };
      for (let s = -5; s <= 5; ++s) {
        const strand = s/5;
        ctx.lineWidth = s === 0 ? 2.2 : .85;
        ctx.globalAlpha = Math.min(.85,power*1.8) * (s === 0 ? .84 : .28+.21*mid);
        ctx.beginPath();
        for (let k=0; k<=80; ++k) {
          const u = k/80, x = w*(.025+.95*u), y = line(u,strand);
          if (k) ctx.lineTo(x,y); else ctx.moveTo(x,y);
        }
        ctx.stroke();
        if (!s) { ctx.globalAlpha *= .13; ctx.lineWidth = 9; ctx.stroke(); }
      }
      ctx.fillStyle = "#fff1d9";
      for (let i=0; i<18; ++i) {
        const u = (i*.618034 + phase*(.05+i%3*.012))%1;
        ctx.globalAlpha = air*.92*Math.sin(u*Math.PI);
        ctx.beginPath(); ctx.arc(w*(.025+.95*u), line(u,Math.sin(i*3.7)), .6+1.3*air, 0, Math.PI*2); ctx.fill();
      }
    }
  }
  sync();
  return {levels, draw, setPlaying(value) { playing = value; sync(); }};
}

// Focus preset API v1. Nothing is mounted until the user enters focus mode.
document.currentScript.mountFocus = api => {
  const {root, background} = api;
  background.classList.add("focusBackdrop");
  background.innerHTML = '<div id="focusWash"></div>';
  root.innerHTML = `<button type="button" id="auroraToggle" aria-label="音效可视化" aria-pressed="true">
        <svg aria-hidden="true" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M3 10q4-8 9 0t9 0M3 15q4-8 9 0t9 0"/></svg>
      </button>
      <div class="focusStage" aria-hidden="true">
        <canvas id="auroraCanvas" hidden></canvas>
        <div class="vinyl"><div id="focusSleeve"></div><i class="spindle"></i></div>
        <svg class="tonearm" viewBox="0 0 100 100">
          <defs><linearGradient id="armMetal"><stop stop-color="#827f89"/><stop offset=".35" stop-color="#fff7f2"/><stop offset=".7" stop-color="#c6c7d3"/><stop offset="1" stop-color="#807d88"/></linearGradient></defs>
          <circle cx="76" cy="9" r="3.1" fill="#252630" stroke="#bab5c4" stroke-width=".3"/>
          <g><path d="M76 9 L80.6 20.6 Q81.4 22.6 80.2 24.5 L74.8 32.8" fill="none" stroke="url(#armMetal)" stroke-width="1.35" stroke-linecap="round"/>
          <path d="m75.3 30.5 2.1 1.4 -3.1 4.5 -2.2-1.4z" fill="#d5d3d9"/><path d="m72.4 34.1 2.2 1.4 -.8 1.1 -2.1-1.4z" fill="#ffa992"/>
          <circle cx="76" cy="9" r="1.7" fill="url(#armMetal)"/><circle cx="76" cy="9" r=".6" fill="#575561"/></g>
        </svg>
      </div>
      <div class="focusCaption"><h2 id="focusTitle"></h2></div>`;
  const sleeve = root.querySelector("#focusSleeve"), title = root.querySelector("#focusTitle");
  const ribbons = createAurora(root.querySelector("canvas"), root.querySelector("button"), api);
  let spinning = false, frame = 0, last = 0, speed = 0, angle = 0;
  function animate(now) {
    frame = 0;
    const dt = Math.min(64, now - (last || now - 16)); last = now;
    speed += ((spinning ? 24 : 0) - speed) * (1 - Math.exp(-dt / (spinning ? 530 : 290)));
    if (speed < .05) speed = 0;
    angle = (angle + speed * dt / 1000) % 360;
    sleeve.style.transform = `rotate(${angle}deg)`;
    if (spinning) ribbons.draw(now, dt);
    if (spinning || speed) frame = requestAnimationFrame(animate);
    else last = 0;
  }
  return {
    update(state) {
      title.textContent = title.title = state.title;
      api.renderCover(sleeve); api.renderCover(background.firstElementChild);
      root.style.setProperty("--needle-travel", `${state.progress * 8}deg`);
      const visible = !document.hidden && !api.reduceMotion.matches;
      spinning = state.playing && visible;
      ribbons.setPlaying(spinning);
      if (!visible) { cancelAnimationFrame(frame); frame = last = speed = 0; }
      else if (spinning && !frame) frame = requestAnimationFrame(animate);
    },
    levels: ribbons.levels,
    dispose() { cancelAnimationFrame(frame); }
  };
};
})();
