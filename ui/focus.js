"use strict";
// Transport and the minimal view never depend on a preset being installed.
function createFocusView() {
  const panel = $("focusView"), shell = document.querySelector(".shell"), trigger = $("cover");
  const root = $("focusContent"), background = $("focusBackground"), picker = $("focusPreset");
  const menu = $("focusPresetMenu"), tool = $("focusPresetTool");
  let selected = "";
  const factories = new Map();
  function hideMenu(restore = false) {
    menu.hidden = true; tool.setAttribute("aria-expanded", "false");
    if (restore) tool.focus();
  }
  function showMenu() {
    if (tool.hidden) return;
    menu.hidden = false; tool.setAttribute("aria-expanded", "true");
    picker.querySelector('[aria-checked="true"]')?.focus();
  }
  tool.onclick = () => menu.hidden ? showMenu() : hideMenu(true);
  tool.onkeydown = e => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); showMenu(); }
  };
  picker.onkeydown = e => {
    const items = [...picker.children], i = items.indexOf(document.activeElement);
    const next = {ArrowDown: (i+1)%items.length, ArrowUp: (i+items.length-1)%items.length,
      Home: 0, End: items.length-1}[e.key];
    if (next !== undefined) { e.preventDefault(); items[next].focus(); }
  };
  document.addEventListener("pointerdown", e => {
    if (!menu.hidden && !tool.contains(e.target) && !menu.contains(e.target)) hideMenu();
  });
  menu.addEventListener("focusout", e => {
    if (e.relatedTarget !== tool && !menu.contains(e.relatedTarget)) hideMenu();
  });
  let catalogue = "", generation = 0, instance, sheet, lifecycle, listening = false;
  function discover(presets) {
    const key = JSON.stringify(presets);
    if (catalogue === key) return;
    catalogue = key;
    hideMenu();
    picker.replaceChildren();
    for (const {id,name} of [{id: "", name: "简洁"}, ...presets]) {
      const b = document.createElement("button");
      b.type = "button"; b.tabIndex = -1; b.dataset.id = id; b.setAttribute("role", "menuitemradio");
      const label = document.createElement("span"); label.textContent = name; b.append(label);
      b.insertAdjacentHTML("beforeend", '<svg class="iconSvg" viewBox="0 0 24 24" aria-hidden="true"><path d="m5 12 4 4L19 6"/></svg>');
      b.onclick = () => {
        hideMenu(true);
        if (selected === id) return;
        selected = id;
        try { localStorage.setItem("focusPreset", id); } catch {}
        mount();
      };
      picker.append(b);
    }
    let id = presets[0]?.id || "";
    try { id = localStorage.getItem("focusPreset") ?? id; } catch {}
    selected = presets.some(p => p.id === id) ? id : "";
    tool.hidden = !presets.length;
    labelPicker();
    if (!panel.hidden) mount();
  }
  function stop() {
    ++generation;
    lifecycle?.abort(); lifecycle = null;
    if (listening) { listening = false; send("levels off"); }
    try { instance?.dispose?.(); } catch (error) { console.warn(error); }
    instance = null; sheet?.remove(); sheet = null;
    for (const slot of [root, background]) {
      slot.replaceChildren(); slot.removeAttribute("style"); slot.className = "";
      delete slot.dataset.cover; // renderCover caches the URL on its target
    }
    delete document.body.dataset.focusPreset;
  }
  function labelPicker() {
    for (const b of picker.children) {
      const checked = b.dataset.id === selected;
      b.setAttribute("aria-checked", String(checked));
      if (checked) tool.title = tool.ariaLabel = `切换预设（当前：${b.textContent}）`;
    }
  }
  function basic() {
    const title = document.createElement("h2"); title.className = "basicFocusTitle";
    title.textContent = trackLabel.textContent; root.replaceChildren(title);
  }
  function failed(error) {
    console.warn("Focus preset unavailable:", error);
    stop(); selected = ""; labelPicker(); basic(); toast("预设无法加载，已切换到简洁模式");
  }
  function asset(el, url) {
    return new Promise((resolve, reject) => {
      el.onload = () => resolve(el); el.onerror = () => reject(new Error(url));
      if (el.tagName === "SCRIPT") el.src = url; else el.href = url;
      document.head.append(el);
    });
  }
  async function mount() {
    stop(); basic(); labelPicker();
    const id = selected, token = generation;
    if (!id) return;
    const base = `https://${id}.focus.bilimusic.local/`;
    try {
      sheet = document.createElement("link"); sheet.rel = "stylesheet"; sheet.media = "not all";
      const css = asset(sheet, base + "style.css");
      if (!factories.has(id)) {
        const script = document.createElement("script");
        factories.set(id, asset(script, base + "preset.js").then(() => {
          if (typeof script.mountFocus !== "function") throw new Error("missing mountFocus");
          return script.mountFocus;
        }).finally(() => script.remove()));
      }
      const [, create] = await Promise.all([css, factories.get(id)]);
      if (generation !== token) return;
      root.replaceChildren(); sheet.media = "all"; document.body.dataset.focusPreset = id;
      lifecycle = new AbortController();
      instance = create({root, background, reduceMotion, signal: lifecycle.signal, renderCover: el => cover(el, current()),
        requestLevels(on) {
          if (generation !== token) return;
          const want = !!on && !document.hidden && !reduceMotion.matches && !!state.playing && !state.paused;
          if (want !== listening) { listening = want; send(want ? "levels on " + id : "levels off"); }
        }
      });
      if (typeof instance?.update !== "function") throw new Error("missing update");
      update();
    } catch (error) { if (generation === token) failed(error); }
  }
  function update() {
    const t = current(); trigger.disabled = !t;
    if (panel.hidden) return;
    if (!t) return close();
    const playing = !!state.playing && !state.paused;
    if (listening && (!playing || document.hidden || reduceMotion.matches)) {
      listening = false; send("levels off");
    }
    panel.classList.toggle("playing", playing);
    if (!instance) { if (root.firstChild) root.firstChild.textContent = trackLabel.textContent; return; }
    try { instance.update({title: trackLabel.textContent, playing, progress: state.duration > 0 ? Math.max(0, Math.min(1, state.elapsed/state.duration)) : 0}); }
    catch (error) { failed(error); }
  }
  function open() {
    if (!current() || !panel.hidden) return;
    shell.hidden = true; panel.hidden = background.hidden = false;
    trigger.setAttribute("aria-expanded", "true"); mount(); update(); panel.focus();
  }
  function close() {
    if (panel.hidden) return;
    hideMenu(); stop(); panel.hidden = background.hidden = true; shell.hidden = false;
    trigger.setAttribute("aria-expanded", "false"); (trigger.disabled ? $("query") : trigger).focus();
  }
  trigger.onclick = () => panel.hidden ? open() : close();
  $("focusBack").onclick = close;
  document.addEventListener("keydown", e => {
    if (e.key === "Escape" && !menu.hidden) { e.preventDefault(); hideMenu(true); return; }
    if (!panel.hidden && e.key === "Escape" && !document.querySelector("dialog[open]")) { e.preventDefault(); close(); }
  });
  document.addEventListener("visibilitychange", update); reduceMotion.addEventListener("change", update);
  return {open, close, update, discover, levels(value) {
    try { if (listening) instance?.levels?.(value); } catch (error) { failed(error); }
  }};
}
