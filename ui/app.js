"use strict";
const $ = (id) => document.getElementById(id);
const paths = {
  compass:
    '<circle cx="12" cy="12" r="9"/><path d="m16 8-2.3 5.7L8 16l2.3-5.7Z"/>',
  library: '<path d="M4 4v16M9 4v16m4-15 4-1 4 15-4 1z"/>',
  heart:
    '<path d="M20.7 5.9c-2.5-2.7-6.5-1.7-8.7 1-2.3-2.7-6.2-3.7-8.7-1C-.3 10.1 4.2 15 12 20.5c7.8-5.5 12.3-10.4 8.7-14.6Z"/>',
  tv: '<rect x="2.5" y="6.5" width="19" height="14" rx="4"/><path d="m8 2 3 4m5-4-3 4m-6 6v2m10-2v2"/>',
  external: '<path d="M7 17 18 6M7 6h11v11"/>',
  record:
    '<path d="M5 13a7 7 0 1 0 7-7" stroke-width="2.3"/><path d="M3.3 9A10 10 0 0 1 17 3.8"/><circle cx="12" cy="13" r="2.4"/>',
  disc: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="2.3"/><path d="M6 12a6 6 0 0 1 6-6"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v.1"/>',
  settings: '<circle cx="12" cy="12.5" r="3"/><path d="m10 3-.5 2.5-2.2 1.3-2.4-.8-2 3.5 1.9 1.7v2.6l-1.9 1.7 2 3.5 2.4-.8 2.2 1.3L10 22h4l.5-2.5 2.2-1.3 2.4.8 2-3.5-1.9-1.7v-2.6l1.9-1.7-2-3.5-2.4.8-2.2-1.3L14 3Z"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  checklist: '<path d="m3 7 2 2 4-5m3 3h9M3 16l2 2 4-5m3 3h9"/>',
  play: '<path d="m7 4 13 8-13 8z" fill="currentColor"/>',
  pause: '<path d="M8 5v14m8-14v14" stroke-width="4"/>',
  order: '<path d="M4 6h11M4 12h11M4 18h16m-4-4 4 4-4 4"/>',
  repeat: '<path d="M4 10V8a2 2 0 0 1 2-2h14m-3-3 3 3-3 3M20 14v2a2 2 0 0 1-2 2H4m3 3-3-3 3-3"/>',
  repeatOne: '<path d="M4 10V8a2 2 0 0 1 2-2h14m-3-3 3 3-3 3M20 14v2a2 2 0 0 1-2 2H4m3 3-3-3 3-3M11 11l2-1v5"/>',
  prev: '<path d="M5 5v14" stroke-width="2.5"/><path d="m18 5-10 7 10 7z" fill="currentColor"/>',
  next: '<path d="M19 5v14" stroke-width="2.5"/><path d="m6 5 10 7-10 7z" fill="currentColor"/>',
  volume: '<path d="M4 9h4l5-4v14l-5-4H4zM16 9q3 3 0 6m3-9q5 6 0 12"/>',
  silent: '<path d="M4 9h4l5-4v14l-5-4H4zm13 0 5 6m0-6-5 6"/>',
  playQueue: '<path d="M3 6h16M3 12h10M3 18h8"/><path d="m16 12 6 4-6 4z" fill="currentColor"/>',
  queue: '<path d="M4 6h16M4 12h12M4 18h9"/>',
  search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 5 5"/>',
  close: '<path d="m6 6 12 12M18 6 6 18"/>',
  trash: '<path d="M4 7h16M9 4h6M6 7l1 13h10l1-13M10 10v7m4-7v7"/>',
};
function icon(name) {
  return `<svg class="iconSvg" aria-hidden="true" viewBox="0 0 24 24">${paths[name]}</svg>`;
}
document
  .querySelectorAll("[data-icon]")
  .forEach((el) => (el.innerHTML = icon(el.dataset.icon)));
const bridge = window.chrome?.webview;
let state = {
    index: -1,
    queue: [],
    elapsed: -1,
    duration: -1,
    volume: 70,
    paused: false,
    title: "",
  },
  tracks = [],
  page = "library",
  fav = new Set(),
  lastVolume = 70,
  timer,
  rowSignature = "",
  drag = new Set();
try {
  fav = new Set(JSON.parse(localStorage.getItem("likes") || "[]"));
} catch {}
function send(message) {
  if (bridge) bridge.postMessage(message);
  else toast("请在 NicoMusic 桌面窗口中播放");
}
function toast(s) {
  $("toast").textContent = s;
  $("toast").classList.add("show");
  clearTimeout(timer);
  timer = setTimeout(() => $("toast").classList.remove("show"), 3500);
}
function btn(label, glyph, action) {
  const el = document.createElement("button");
  el.className = "icon";
  el.title = label;
  el.setAttribute("aria-label", label);
  el.innerHTML = icon(glyph);
  el.onclick = action;
  return el;
}
let lists;
try { lists = new PlaylistStore(localStorage); }
catch { toast("无法读取自建歌单"); }
const currentList = () => lists?.find(page);
function refreshListNav() {
  const nav = $("listNav");
  nav.replaceChildren();
  for (const list of lists?.items || []) {
    const b = document.createElement("button");
    b.type = "button";
    b.dataset.page = list.id;
    b.title = b.ariaLabel = list.name;
    b.innerHTML = icon("queue");
    const name = document.createElement("span");
    name.textContent = list.name;
    b.append(name);
    b.onclick = () => select(list.id);
    nav.append(b);
  }
}
function choice(container, label, value, checked) {
  const row = document.createElement("label"), box = document.createElement("input");
  box.type = "checkbox"; box.value = value; box.checked = checked;
  const text = document.createElement("span"); text.textContent = label;
  row.append(box, text); container.append(row);
}
function checkedValues(container) {
  return [...container.querySelectorAll("input:checked")].map(el => el.value);
}
let seedUrls = [], assignUrls = [], bulkAssign = false;
let batch = false;
const picked = new Set();
function openCreate(initialUrls = []) {
  if (!lists) return toast("歌单存储不可用");
  seedUrls = [...initialUrls];
  $("listName").value = "";
  $("listError").textContent = "";
  $("listDialog").showModal();
  $("listName").focus();
}
$("newList").onclick = () => openCreate();
$("listForm").onsubmit = e => {
  e.preventDefault();
  try {
    const id = lists.create($("listName").value, seedUrls);
    $("listDialog").close(); refreshListNav(); select(id);
  } catch (err) { $("listError").textContent = err.message || "创建失败"; }
};
$("deleteList").onclick = () => {
  const list = currentList();
  if (!list || !confirm(`删除「${list.name}」？音乐仍保留在音乐库中。`)) return;
  try { lists.remove(list.id); refreshListNav(); select("library"); }
  catch { toast("无法删除歌单"); }
};
function openAssign(urls, isBatch, caption) {
  if (!lists) return toast("歌单存储不可用");
  assignUrls = [...urls]; bulkAssign = isBatch;
  if (!lists.items.length) return openCreate(assignUrls);
  $("assignTrack").textContent = caption;
  $("listChoices").replaceChildren();
  for (const l of lists.items) choice($("listChoices"), l.name, l.id, !isBatch && l.urls.includes(urls[0]));
  $("assignError").textContent = "";
  $("assignDialog").showModal();
}
$("assignForm").onsubmit = e => {
  e.preventDefault();
  try {
    const ids = new Set(checkedValues($("listChoices")));
    if (bulkAssign) {
      if (!ids.size) { $("assignError").textContent = "请选择目标歌单"; return; }
      lists.addSongs(assignUrls, ids);
    } else lists.assign(assignUrls[0], ids);
    $("assignDialog").close();
    if (bulkAssign) setBatch(false);
    else { rowSignature = ""; sync(); }
  } catch { $("assignError").textContent = "无法保存歌单"; }
};
$("createWithSong").onclick = () => { $("assignDialog").close(); openCreate(assignUrls); };
refreshListNav();
function select(p) {
  focusView.close();
  page = p;
  document
    .querySelectorAll("nav [data-page]")
    .forEach((b) => b.classList.toggle("selected", b.dataset.page === p));
  const list = currentList();
  $("heading").textContent = list?.name || (page === "likes" ? "我喜欢的" : "全部音乐");
  batch = false; picked.clear();
  $("deleteList").hidden = !list;
  $("query").value = "";
  rowSignature = "";
  $("rows").scrollTop = 0;
  sync();
}
document
  .querySelectorAll("[data-page]")
  .forEach((b) => (b.onclick = () => select(b.dataset.page)));
let pendingImport = null;
function addDialog() {
  if (pendingImport) return toast("正在添加音乐");
  const d = $("addDialog");
  $("url").setCustomValidity("");
  d.showModal();
  $("url").focus();
}
document.querySelectorAll(".addLink").forEach((b) => (b.onclick = addDialog));
document
  .querySelectorAll(".close")
  .forEach((b) => (b.onclick = () => b.closest("dialog").close()));
document.querySelectorAll("dialog").forEach(
  (d) =>
    (d.onclick = (e) => {
      if (e.target === d) {
        const r = d.getBoundingClientRect();
        if (
          e.clientX < r.left ||
          e.clientX > r.right ||
          e.clientY < r.top ||
          e.clientY > r.bottom
        )
          d.close();
      }
    }),
);
function fileSelection(files) {
  if (files?.length !== 1) return toast("请一次选择一首音乐");
  try { bridge.postMessageWithAdditionalObjects("selectedFile", [files[0]]); }
  catch { toast("无法读取文件，请粘贴本机文件的完整路径"); }
}
$("localDrop").onclick = () => $("localFile").click();
$("localFile").onchange = e => { fileSelection(e.target.files); e.target.value = ""; };
const addForm = $("addForm");
addForm.ondragover = e => {
  if (!e.dataTransfer.types.includes("Files")) return;
  e.preventDefault(); e.dataTransfer.dropEffect = "copy"; addForm.classList.add("dragging");
};
addForm.ondragleave = e => { if (!addForm.contains(e.relatedTarget)) addForm.classList.remove("dragging"); };
addForm.ondrop = e => {
  if (!e.dataTransfer.types.includes("Files")) return;
  e.preventDefault(); addForm.classList.remove("dragging"); fileSelection(e.dataTransfer.files);
};
function localURI(input) {
  let path = input.replace(/^"(.*)"$/, "$1");
  if (/^file:/i.test(path)) {
    const u = new URL(path);
    if (u.hostname || u.search || u.hash) throw Error();
    path = decodeURIComponent(u.pathname.slice(1));
  }
  if (!/^[a-z]:[\\/]/i.test(path)) return null;
  const parts = path.slice(3).split(/[\\/]/).map(encodeURIComponent);
  return new URL("file:///" + path[0].toUpperCase() + ":/" + parts.join("/")).href;
}
$("addForm").onsubmit = (e) => {
  e.preventDefault();
  if (pendingImport) return;
  let url = $("url").value.trim();
  try {
    const local = localURI(url);
    if (local) url = local;
    const u = new URL(url);
    if (!local && (
      !["https:", "http:"].includes(u.protocol) ||
      u.username ||
      u.password ||
      /[\s\\<>\"]/.test(url)
    ))
      throw Error();
  } catch {
    $("url").setCustomValidity("请输入 HTTP(S) 链接或本机音频文件的完整路径");
    $("url").reportValidity();
    return;
  }
  const command = `${e.submitter?.value === "add" ? "add" : "start"} ${url} ${$("name").value.trim()}`;
  if (new TextEncoder().encode(command).length > 12000) return toast("链接或曲名过长");
  if (bridge) pendingImport = {page, url};
  send(command);
};
$("url").oninput = () => $("url").setCustomValidity("");
$("projectLink").onclick = (e) => {
  if (!bridge) return; // Keep the ordinary link working in browser previews.
  e.preventDefault();
  send("project");
};
const builtinFileTypes = $("localFile").accept.split(",");
let sourceSignature;
function renderSources(sources) {
  const signature = JSON.stringify(sources);
  if (signature === sourceSignature) return;
  sourceSignature = signature;
  $("sources").replaceChildren();
  $("localFile").accept = [...new Set([...builtinFileTypes, ...sources.flatMap(s => s.extensions || [])])].join(",");
  for (const s of sources) {
    if (s.hasHome === false) continue;
    const b = document.createElement("button");
    b.className = "source";
    b.style.width = "100%";
    b.title = s.name;
    b.setAttribute("aria-label", s.name);
    b.innerHTML = '<i>' + icon("tv") + '</i>';
    b.append(document.createTextNode(s.name));
    const arrow = document.createElement("i");
    arrow.className = "trail";
    arrow.innerHTML = icon("external");
    b.append(arrow);
    b.onclick = () => send("source " + s.id);
    $("sources").append(b);
  }
  $("sourcePanel").hidden = !$("sources").childElementCount;
}
function current() {
  return tracks[state.index];
}
// The queue belongs to the player, not to the page being browsed.
let drawnQueue, drawnTracks;
function renderQueue() {
  if (!$("queueDialog").open) return;
  const list = $("queueItems");
  if (drawnQueue !== state.queue || drawnTracks !== tracks) {
    const rows = document.createDocumentFragment();
    for (const index of state.queue) {
      const track = tracks[index];
      if (!track) continue;
      const li = document.createElement("li"), b = document.createElement("button");
      b.type = "button";
      b.dataset.index = index;
      b.title = b.textContent = title(track);
      b.onclick = () => send("playqueued " + track.url);
      li.append(b); rows.append(li);
    }
    list.replaceChildren(rows);
    $("queueCount").textContent = list.childElementCount + " 首";
    $("queueEmpty").hidden = !!list.childElementCount;
    drawnQueue = state.queue; drawnTracks = tracks;
  }
  const active = list.querySelector('[aria-current="true"]');
  if (active && Number(active.dataset.index) === state.index) return;
  active?.removeAttribute("aria-current");
  list.querySelector(`button[data-index="${state.index}"]`)?.setAttribute("aria-current", "true");
}
$("viewQueue").onclick = () => {
  $("queueDialog").showModal();
  renderQueue();
  $("queueItems").querySelector('[aria-current="true"]')?.scrollIntoView({block: "nearest"});
};
function pageOrder() {
  const list = currentList();
  if (list) {
    const indices = new Map(tracks.map((t, i) => [t.url, i]));
    return list.urls.map(u => indices.get(u)).filter(i => i !== undefined);
  }
  return tracks.flatMap((t, i) => page !== "likes" || fav.has(t.url) ? [i] : []);
}
function playFromPage(i) {
  if (page === "library") send("play " + i);
  else send("playset " + i + " " + pageOrder().join(" "));
}
function toggle() {
  if (current()) send("pause");
  else { const i = pageOrder()[0]; if (i !== undefined) playFromPage(i); }
}
$("toggle").onclick = toggle;
$("next").onclick = () => send("next");
$("prev").onclick = () => send("prev");
const repeatModes = {
  off: {label: "顺序播放", icon: "order", next: "one"},
  one: {label: "单曲循环", icon: "repeatOne", next: "all"},
  all: {label: "列表循环", icon: "repeat", next: "off"},
};
$("repeat").onclick = () => send("repeat " + repeatModes[state.repeat || "off"].next);
function renderRepeat() {
  const mode = state.repeat || "off", el = $("repeat");
  if (el.dataset.mode === mode) return;
  const {label, icon: glyph, next} = repeatModes[mode];
  el.dataset.mode = mode;
  el.innerHTML = icon(glyph);
  el.title = el.ariaLabel = `当前：${label}；切换为${repeatModes[next].label}`;
}
function like(t) {
  if (!t) return;
  updateLikes([t.url], !fav.has(t.url));
}
function updateLikes(urls, add) {
  const next = new Set(fav);
  for (const u of urls) { if (add) next.add(u); else next.delete(u); }
  try { localStorage.setItem("likes", JSON.stringify([...next])); }
  catch { toast("无法保存喜欢的曲目"); return false; }
  fav = next; sync(); return true;
}
$("like").onclick = () => like(current());
$("query").oninput = () => { picked.clear(); rowSignature = ""; renderRows(); };
function setBatch(on) {
  batch = on; picked.clear(); rowSignature = ""; renderRows();
}
$("batchToggle").onclick = () => setBatch(!batch);
function visibleOrder() {
  const query = $("query").value.trim().toLowerCase();
  return pageOrder().filter(i => !query || (title(tracks[i]) + " " + tracks[i].url).toLowerCase().includes(query));
}
function refreshBatch(visible) {
  $("batchToggle").setAttribute("aria-pressed", String(batch));
  $("batchBar").hidden = !batch;
  if (!batch) return;
  const count = picked.size, all = $("batchAll");
  all.disabled = !visible.length;
  all.checked = !!count && count === visible.length;
  all.indeterminate = count > 0 && !all.checked;
  $("batchCount").textContent = `已选 ${count} 首`;
  for (const id of ["batchPlay", "batchAdd", "batchFav", "batchRemove"]) $(id).disabled = !count;
  $("batchFav").textContent = page === "likes" || (count && [...picked].every(u => fav.has(u))) ? "取消喜欢" : "喜欢";
  $("batchRemove").hidden = page === "likes";
  $("batchRemove").textContent = currentList() ? "移出歌单" : "从音乐库移除";
}
$("batchAll").onchange = () => {
  const checked = $("batchAll").checked;
  for (const box of $("rows").querySelectorAll('input[type=checkbox]')) {
    box.checked = checked;
    if (checked) picked.add(box.value); else picked.delete(box.value);
  }
  refreshBatch(visibleOrder());
};
$("batchPlay").onclick = () => {
  const order = visibleOrder().filter(i => picked.has(tracks[i].url));
  if (order.length) send("playset " + order[0] + " " + order.join(" "));
  setBatch(false);
};
$("batchAdd").onclick = () => openAssign(visibleOrder().map(i => tracks[i].url).filter(u => picked.has(u)), true, `已选 ${picked.size} 首`);
$("batchFav").onclick = () => {
  if (updateLikes([...picked], ![...picked].every(u => fav.has(u)))) setBatch(false);
};
function removeFromPlaylist(urls) {
  const list = currentList();
  if (!list) return false;
  try { lists.removeSongs(list.id, urls); }
  catch { toast("移出歌单失败"); return false; }
  rowSignature = "";
  return true;
}
$("batchRemove").onclick = () => {
  if (!picked.size) return;
  if (currentList()) {
    if (!removeFromPlaylist(picked)) return;
  } else if (page === "library") {
    if (!confirm(`从音乐库移除选中的 ${picked.size} 首音乐？这些歌曲也会从所有歌单中移除。`)) return;
    send("removeurls " + [...picked].join(" "));
  }
  setBatch(false);
};
function source(t) {
  if (t.source) return t.source;
  if (t.url.startsWith("file:///")) return "本地音乐";
  try {
    let h = new URL(t.url).hostname;
    return h;
  } catch {
    return "网页音频";
  }
}
function title(t) {
  if (t.name === t.url && t.url.startsWith("file:///")) {
    try { return decodeURIComponent(t.url.split("/").pop()); } catch {}
  }
  return t.name === t.url
    ? source(t) + " · " + t.url.split("/").filter(Boolean).pop().split("?")[0]
    : t.name;
}
function thumbnailOf(t) {
  if (t?.thumbnail) {
    try {
      const u = new URL(t.thumbnail);
      if (["https:", "http:"].includes(u.protocol) && !u.username && !u.password) {
        return u.href;
      }
    } catch {}
  }
  return "";
}
function cover(el, t) {
  const src = thumbnailOf(t);
  if (el.dataset.cover === src) return;
  el.dataset.cover = src;
  el.innerHTML = icon("disc");
  if (!src) return;
  const img = new Image();
  img.alt = "";
  img.referrerPolicy = "no-referrer";
  img.decoding = "async";
  if (el.classList.contains("rowCover")) img.loading = "lazy";
  img.onerror = () => img.remove();
  el.append(img);
  img.src = src;
}
function renderRows() {
  const query = $("query").value.trim().toLowerCase();
  const key = JSON.stringify([tracks, state.index, page, query, [...fav], batch]);
  if (key === rowSignature) return;
  rowSignature = key;
  $("rows").replaceChildren();
  const visible = visibleOrder();
  if (batch) {
    const valid = new Set(visible.map(i => tracks[i].url));
    for (const u of picked) if (!valid.has(u)) picked.delete(u);
  }
  refreshBatch(visible);
  visible.forEach(i => {
    const t = tracks[i];
    const row = document.createElement(batch ? "label" : "div");
    row.className = "row" + (batch ? " pick" : "") + (i === state.index ? " current" : "");
    if (batch) {
      const box = document.createElement("input");
      box.type = "checkbox"; box.value = t.url; box.checked = picked.has(t.url);
      box.setAttribute("aria-label", title(t));
      box.onchange = () => {
        if (box.checked) picked.add(t.url); else picked.delete(t.url);
        refreshBatch(visible);
      };
      row.append(box);
    }
    const active = i === state.index;
    const sleeve = batch ? document.createElement("span") : btn((active ? "进入专注模式：" : "播放 ") + title(t), "play", active ? focusView.open : () => playFromPage(i));
    sleeve.className = "rowCover";
    cover(sleeve, t);
    const info = document.createElement(batch ? "span" : "button");
    info.className = "rowText";
    if (!batch) info.onclick = () => playFromPage(i);
    const n = document.createElement("strong");
    n.textContent = title(t);
    const sub = document.createElement("small");
    sub.textContent = source(t);
    info.append(n, sub);
    row.append(sleeve, info);
    $("rows").append(row);
    if (batch) return;
    const heart = btn(fav.has(t.url) ? "取消喜欢" : "喜欢", "heart", () =>
      like(t),
    );
    heart.classList.toggle("on", fav.has(t.url));
    row.append(heart, btn("加入歌单", "plus", () => openAssign([t.url], false, title(t))));
    if (page === "library")
      row.append(btn("从音乐库移除", "trash", () => send("remove " + i)));
    else if (currentList())
      row.append(btn("从当前歌单移除", "trash", () => {
        if (removeFromPlaylist(new Set([t.url]))) { sync(); toast("已从当前歌单移除"); }
      }));
  });
  if (!$("rows").childElementCount) {
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.innerHTML = `<i>${icon(query ? "search" : page === "likes" ? "heart" : "library")}</i><h3>${query ? "未找到曲目" : page === "likes" ? "暂无喜欢的音乐" : "歌单还是空的"}</h3>`;
    if (!query) {
      let b = document.createElement("button");
      b.className = "subtle";
      b.textContent = page !== "library" ? "去音乐库" : "添加链接";
      b.onclick = page !== "library" ? () => select("library") : addDialog;
      empty.append(b);
    }
    $("rows").append(empty);
  }
}
function time(t) {
  t = Math.max(0, Math.floor(t || 0));
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, "0")}`;
}
function range(el, value, max) {
  el.max = max;
  el.value = value;
  el.style.setProperty(
    "--fill",
    `${Math.max(0, Math.min(100, (100 * value) / max))}%`,
  );
}
for (const id of ["volume", "seek"]) {
  let el = $(id);
  el.onpointerdown = () => drag.add(id);
  el.onpointerup = el.onpointercancel = el.onblur = () => drag.delete(id);
  el.oninput = () => {
    el.style.setProperty("--fill", (100 * el.value) / el.max + "%");
    if (id === "seek") $("elapsed").textContent = time(el.value);
  };
  el.onchange = () => send(`${id} ${el.value}`);
}
$("mute").onclick = () => {
  if (state.volume > 0) lastVolume = state.volume;
  send("volume " + (state.volume > 0 ? 0 : lastVolume || 70));
};
// Animate the text, never the viewport, so playback controls stay in place.
const trackLabel = $("track"), trackWindow = $("trackWindow");
const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)");
let titleAnimation;
function layoutTitle() {
  titleAnimation?.cancel();
  const overflow = trackLabel.offsetWidth - trackWindow.clientWidth;
  if (reduceMotion.matches || overflow <= 0) return;
  const travel = Math.max(500, overflow * 32), duration = travel + 900;
  const end = `translateX(-${overflow}px)`;
  titleAnimation = trackLabel.animate([
    {transform: "translateX(0)", offset: 0},
    {transform: "translateX(0)", offset: 450 / duration},
    {transform: end, offset: 1 - 450 / duration},
    {transform: end, offset: 1},
  ], {duration, direction: "alternate", iterations: Infinity});
}
const titleObserver = new ResizeObserver(layoutTitle);
titleObserver.observe(trackWindow);
titleObserver.observe(trackLabel);
reduceMotion.addEventListener("change", layoutTitle);
const focusView = createFocusView();
function sync() {
  const t = current();
  const displayTitle = t
    ? t.name === t.url && state.title
      ? state.title
      : title(t)
    : "尚未播放";
  if (trackLabel.textContent !== displayTitle) {
    trackLabel.textContent = trackWindow.title = displayTitle;
    layoutTitle();
  }
  $("artist").textContent = t
    ? state.elapsed < 0
      ? "载入中…"
      : source(t)
    : "";
  cover($("cover"), t);
  focusView.update();
  $("toggle").disabled = !t && !pageOrder().length;
  $("toggle").innerHTML = icon(t && !state.paused ? "pause" : "play");
  $("toggle").setAttribute("aria-label", t && !state.paused ? "暂停" : "播放");
  $("like").disabled = !t;
  $("like").setAttribute("aria-pressed", String(!!t && fav.has(t.url)));
  $("like").classList.toggle("on", !!t && fav.has(t.url));
  renderQueue();
  renderRepeat();
  $("prev").disabled = !state.canPrev;
  $("next").disabled = !state.canNext;
  $("duration").textContent = time(state.duration);
  $("seek").disabled = !t || state.duration <= 0;
  if (!drag.has("seek")) {
    range(
      $("seek"),
      Math.max(0, Math.floor(state.elapsed)),
      Math.max(1, Math.floor(state.duration)),
    );
    $("elapsed").textContent = time(state.elapsed);
  }
  if (!drag.has("volume")) range($("volume"), state.volume, 100);
  $("mute").innerHTML = icon(state.volume ? "volume" : "silent");
  $("libCount").textContent = tracks.length;
  renderRows();
}
const playbackShortcuts = createPlaybackShortcuts(toast, delta => send("volume-step " + delta));
const appSettings = createSettings({shortcuts: playbackShortcuts, send, notify: toast, desktop: !!bridge});
const updateChecker = createUpdateChecker({send, notify: toast, desktop: !!bridge,
  storage: {getItem: key => localStorage.getItem(key), setItem: (key, value) => localStorage.setItem(key, value)}});
bridge?.addEventListener("message", (e) => {
  const msg = e.data;
  if (updateChecker.message(msg)) return;
  if (playbackShortcuts.message(msg)) { appSettings.message(msg); return; }
  if ("localPath" in msg) {
    if ($("addDialog").open) {
      $("url").value = msg.localPath; $("url").setCustomValidity(""); $("url").focus();
    }
    return;
  }
  if (msg.windowState) return;
  if ("levels" in msg) { focusView.levels(msg.levels); return; }
  if (msg.focusPresets) focusView.discover(msg.focusPresets);
  if (msg.repeat && msg.repeat !== state.repeat) {
    if (state.repeat) toast(repeatModes[msg.repeat].label);
    try { localStorage.setItem("repeatMode", msg.repeat); } catch {}
  }
  state = { ...state, ...msg };
  if (msg.sources) renderSources(msg.sources);
  if (msg.tracks) {
    tracks = msg.tracks;
    const valid = new Set(tracks.map(t => t.url));
    try {
      lists?.prune(valid);
      const kept = [...fav].filter(u => valid.has(u));
      if (kept.length !== fav.size) { localStorage.setItem("likes", JSON.stringify(kept)); fav = new Set(kept); }
    }
    catch { toast("歌单更新失败"); }
  }
  if (msg.error) {
    if (!msg.imported) pendingImport = null;
    if ($("addDialog").open) {
      $("url").setCustomValidity(msg.error);
      $("url").reportValidity();
    } else toast(msg.error);
  }
  if (msg.imported && pendingImport) {
    const {page: target, url} = pendingImport;
    pendingImport = null;
    let notice = "已加入音乐库";
    if (target === "likes") {
      notice = updateLikes([url], true) ? "已加入我喜欢的" : "";
    } else if (target !== "library" && lists?.find(target)) {
      try {
        lists.addSongs([url], new Set([target]));
        rowSignature = "";
        notice = "已加入歌单";
      } catch { notice = "已加入音乐库，但无法保存到歌单"; }
    }
    if ($("addDialog").open) {
      $("addDialog").close();
      $("addForm").reset();
      select(page);
    }
    if (notice) toast(notice);
  }
  sync();
});
sync();
if (bridge) {
  let mode = "off";
  try {
    const saved = localStorage.getItem("repeatMode");
    if (saved === "one" || saved === "all") mode = saved;
  } catch {}
  send("ready " + mode);
}
