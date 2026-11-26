"use strict";
const $ = (id) => document.getElementById(id);
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
let editHistory = null, pendingMutation = false, nativeHistoryRequest = null;
function send(message) {
  const mutation = /^(add|start|remove|removeurls|mix-import)\s/.test(message);
  if (mutation && (editHistory?.busy || pendingMutation)) {
    toast("请等待当前操作完成"); return false;
  }
  if (bridge) {
    if (mutation) { pendingMutation = true; editHistory?.refresh(); }
    bridge.postMessage(message); return true;
  }
  toast("请在 NicoMusic 桌面窗口中播放"); return false;
}
function toast(s, action = null) {
  const el = $("toast");
  el.replaceChildren();
  const text = document.createElement("span"); text.textContent = s; el.append(text);
  if (action) {
    const button = document.createElement("button");
    button.type = "button"; button.textContent = action.label;
    button.onclick = () => { el.classList.remove("show"); action.run(); };
    el.append(button);
  }
  el.classList.add("show");
  clearTimeout(timer);
  timer = setTimeout(() => el.classList.remove("show"), action ? 6500 : 3500);
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
function membershipSnapshot() {
  return {likes: [...fav], lists: (lists?.items || []).map(list => ({...list, urls: [...list.urls]}))};
}
function restoreMembership(next) {
  const valid = new Set(tracks.map(track => track.url));
  next = {likes: next.likes.filter(url => valid.has(url)),
    lists: next.lists.map(list => ({...list, urls: list.urls.filter(url => valid.has(url))}))};
  const oldLikes = localStorage.getItem("likes");
  const likesChanged = JSON.stringify([...fav]) !== JSON.stringify(next.likes);
  try {
    if (likesChanged) localStorage.setItem("likes", JSON.stringify(next.likes));
    if (lists && JSON.stringify(lists.items) !== JSON.stringify(next.lists)) lists.commit(next.lists);
  } catch (error) {
    if (likesChanged) {
      try { if (oldLikes === null) localStorage.removeItem("likes"); else localStorage.setItem("likes", oldLikes); }
      catch { throw Error("无法保存或恢复收藏，请检查磁盘空间"); }
    }
    throw Error("无法保存歌单或收藏，操作未完成");
  }
  fav = new Set(next.likes);
  rowSignature = ""; refreshListNav();
  if (page !== "library" && page !== "likes" && !currentList()) select("library"); else sync();
}
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
const deleteListDialog = $("deleteListDialog");
$("cancelDeleteList").onclick = () => deleteListDialog.close();
$("deleteList").onclick = () => {
  const list = currentList();
  if (!list) return;
  $("deleteListName").textContent = $("deleteListName").title = list.name;
  $("confirmDeleteList").onclick = () => {
    deleteListDialog.close();
    try {
      lists.remove(list.id); refreshListNav(); select("library");
      document.querySelector('nav [data-page="library"]').focus();
    } catch { toast("无法删除歌单"); }
  };
  deleteListDialog.showModal();
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
let pendingImport = null, mixToken = null;
$("shareMix").onclick = () => {
  const order = trackOrder.apply(page, defaultPageOrder(), tracks);
  const shared = order.filter(i => /^https?:\/\//i.test(tracks[i].url));
  if (!shared.length) return toast(order.length ? "此列表只有本地文件，不能分享本机路径" : "没有可分享的曲目");
  if (shared.length > 10000) return toast("每个分享歌单最多 10000 首");
  if (shared.length < order.length) toast(`已跳过 ${order.length - shared.length} 首本地歌曲`);
  const name = currentList()?.name || (page === "likes" ? "我喜欢的" : "全部音乐");
  send(`mix-export ${name.replace(/[\t\r\n]/g, " ")}\t${shared.join(" ")}`);
};
function previewMix(mix) {
  if (!lists || pendingImport || pendingMutation || editHistory?.busy ||
      document.querySelector("dialog[open]:not(#addDialog):not(#mixDialog)")) {
    send("mix-cancel " + mix.token); toast("请先完成当前操作再导入歌单"); return;
  }
  mixToken = mix.token;
  let name = mix.name.trim();
  for (let i=2; lists.items.some(l => l.name === name); i++) {
    const suffix = ` (${i})`; name = mix.name.trim().slice(0,60-suffix.length) + suffix;
  }
  $("mixCurrent").dataset.page = page;
  $("mixCurrent").textContent = currentList() ? "导入当前歌单" : page === "likes" ? "加入我喜欢的" : "仅加入音乐库";
  $("mixCurrent").title = currentList() ? `导入到「${currentList().name}」` : $("mixCurrent").textContent;
  $("mixName").value = name; $("mixName").setCustomValidity("");
  $("mixSummary").textContent = `共 ${mix.count} 首，不会重复加入音乐库。` +
    (mix.unavailable ? `其中 ${mix.unavailable} 首需要相应音源插件，装回后可见。` : "");
  if (!$("mixDialog").open) $("mixDialog").showModal();
}
$("mixDialog").addEventListener("close", () => {
  if ($("mixDialog").open) return; // A newer file preview opened before the queued close event.
  if (mixToken !== null) send("mix-cancel " + mixToken);
  mixToken = null;
});
$("mixName").oninput = () => $("mixName").setCustomValidity("");
$("mixCurrent").onclick = () => submitMix($("mixCurrent").dataset.page);
$("mixForm").onsubmit = e => { e.preventDefault(); submitMix(""); };
function submitMix(target) {
  const name = $("mixName").value.trim();
  if (!target && (!name || lists.items.some(l=>l.name===name))) {
    $("mixName").setCustomValidity("请输入不重复的歌单名称"); $("mixName").reportValidity(); return;
  }
  if (target && !["library", "likes"].includes(target) && !lists.find(target)) return toast("目标歌单已删除");
  if (mixToken === null || !send("mix-import " + mixToken)) return;
  pendingImport = target ? {page: target, priorOrder: trackOrder.apply(target, defaultPageOrder(target), tracks).map(i => tracks[i].url)} : {mixName:name};
  mixToken = null; $("mixDialog").close();
}
async function reviewCollection(preview) {
  const request = pendingImport;
  let accepted = false;
  try {
    if (request && $("addDialog").open) {
      const destination = request.page === "library" ? "全部音乐" :
        request.page === "likes" ? "我喜欢的" : lists?.find(request.page)?.name;
      if (destination) accepted = await confirmPluginCollection(preview, destination);
    }
  } catch (err) { toast(err.message || "无法显示音源确认界面"); }
  accepted = accepted && request === pendingImport && $("addDialog").open;
  send(`collection-decision ${preview.id} ${accepted ? "accept" : "cancel"}`);
  if (!accepted && request === pendingImport) {
    pendingImport = null;
    pendingMutation = false; editHistory?.refresh();
    $("importStatus").hidden = true;
  }
}
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
  if (files?.length !== 1) return toast("请一次选择一个文件");
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
// Accept a shared playlist on the main window, without hijacking audio or row reordering.
document.addEventListener("dragover", e => {
  if (!document.querySelector("dialog[open]") && e.dataTransfer.types.includes("Files")) e.preventDefault();
});
document.addEventListener("drop", e => {
  if (document.querySelector("dialog[open]") || !e.dataTransfer.types.includes("Files")) return;
  e.preventDefault();
  const files = e.dataTransfer.files;
  if (files.length===1 && /\.nicomix$/i.test(files[0].name)) fileSelection(files);
  else toast("请拖入一个 .nicomix 歌单，或使用添加音乐");
});
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
    if (local && /\.nicomix$/i.test(new URL(local).pathname)) { send("mix-open " + local); return; }
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
  if (!send(command)) pendingImport = null;
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
    b.innerHTML = '<i>' + icon("globe") + '</i>';
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
  const track = tracks[state.index];
  return track?.hidden ? undefined : track;
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
      if (!track || track.hidden) continue;
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
const trackOrder = createTrackOrder(localStorage);
const visibleUrls = () => visibleOrder().map(i => tracks[i].url);
const reorderHandles = createReorderHandles($("rows"), {
  selected: picked, visible: visibleUrls,
  move(moving, target, after) {
    if (!batch) return false;
    const order = moveVisibleTracks(pageOrder().map(i => tracks[i].url), visibleUrls(), moving, target, after);
    if (!order) return false;
    try { trackOrder.save(page, order); }
    catch { toast("无法保存歌曲顺序"); return false; }
    rowSignature = ""; renderRows();
    return true;
  },
});
let hiddenTrackCount = 0;
const librarySize = () => tracks.length - hiddenTrackCount;
function pageOrder() {
  return trackOrder.apply(page, defaultPageOrder().filter(i => !tracks[i].hidden), tracks);
}
function defaultPageOrder(scope = page) {
  const list = lists?.find(scope);
  const members = list ? new Set(list.urls) : scope === "likes" ? fav : null;
  // The native library appends on first import. Keep its indices intact for playback/removal.
  const order = [];
  for (let i = tracks.length - 1; i >= 0; --i)
    if (!members || members.has(tracks[i].url)) order.push(i);
  return order;
}
function playFromPage(i) {
  if (page === "library" && !trackOrder.has(page)) send("play " + i);
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
  if (editHistory.busy || pendingMutation) { toast("请等待当前操作完成"); return false; }
  const before = membershipSnapshot();
  try { lists.removeSongs(list.id, urls); }
  catch { toast("移出歌单失败"); return false; }
  editHistory.record({label: `从歌单移出歌曲（${urls.size} 首）`, before});
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
  reorderHandles.reset();
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
    if (batch) { reorderHandles.attach(row, t.url, title(t)); return; }
    const heart = btn(fav.has(t.url) ? "取消喜欢" : "喜欢", "heart", () =>
      like(t),
    );
    heart.classList.toggle("on", fav.has(t.url));
    row.append(heart, btn("加入歌单", "plus", () => openAssign([t.url], false, title(t))));
    if (page === "library")
      row.append(btn("从音乐库移除", "trash", () => send("remove " + i)));
    else if (currentList())
      row.append(btn("从当前歌单移除", "trash", () => {
        if (removeFromPlaylist(new Set([t.url]))) sync();
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
    if (id === "volume") send(`volume ${el.value}`);
    else $("elapsed").textContent = time(el.value);
  };
  if (id === "seek") el.onchange = () => send(`seek ${el.value}`);
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
  const playing = !!t && !!state.playing && !state.paused;
  $("sidebarStatusArea").dataset.playing = String(playing);
  const sidebarStatus = t
    ? state.paused ? "已暂停" : playing ? "正在播放" : state.elapsed < 0 ? "正在载入…" : "已停止"
    : librarySize() ? `${librarySize()} 首音乐` : "随心听好音乐";
  if ($("sidebarStatus").textContent !== sidebarStatus) $("sidebarStatus").textContent = sidebarStatus;
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
  $("libCount").textContent = librarySize();
  renderRows();
}
const playbackShortcuts = createPlaybackShortcuts(toast, delta => send("volume-step " + delta));
const appSettings = createSettings({shortcuts: playbackShortcuts, send, notify: toast, desktop: !!bridge});
editHistory = createEditHistory({storage: localStorage, snapshot: membershipSnapshot, restore: restoreMembership,
  blocked: () => pendingMutation || !!pendingImport,
  applyNative: (id, direction) => new Promise((resolve, reject) => {
    nativeHistoryRequest = {id, direction, resolve, reject};
    send(`history-apply ${id} ${direction}`);
  }),
  notify: toast,
  changed: history => {
    $("undoEdit").disabled = !history.canUndo;
    $("redoEdit").disabled = !history.canRedo;
    $("editNotices").checked = history.notices;
  }});
$("undoEdit").onclick = () => editHistory.undo();
$("redoEdit").onclick = () => editHistory.redo();
$("editNotices").onchange = e => {
  if (!editHistory.setNotices(e.target.checked)) e.target.checked = editHistory.notices;
  if (!editHistory.notices) $("toast").classList.remove("show");
};
document.addEventListener("keydown", event => {
  const direction = historyShortcut(event, !!document.querySelector("dialog[open]"));
  if (!direction) return;
  event.preventDefault(); event.stopImmediatePropagation();
  if (!event.repeat) editHistory[direction]();
}, true);
const updateChecker = createUpdateChecker({send, notify: toast, desktop: !!bridge,
  storage: {getItem: key => localStorage.getItem(key), setItem: (key, value) => localStorage.setItem(key, value)}});
bridge?.addEventListener("message", (e) => {
  const msg = e.data;
  if (msg.mixPreview) { previewMix(msg.mixPreview); return; }
  if (msg.mixNotice) { toast(msg.mixNotice); return; }
  let createdList;
  const editBefore = msg.libraryEdit ? membershipSnapshot() : null;
  if (msg.libraryEdit || msg.imported || msg.importFailed || msg.error && !msg.collectionPending) pendingMutation = false;
  if (updateChecker.message(msg)) return;
  if (playbackShortcuts.message(msg)) { appSettings.message(msg); return; }
  if ("localPath" in msg) {
    if ($("addDialog").open) {
      $("url").value = msg.localPath; $("url").setCustomValidity(""); $("url").focus();
    }
    return;
  }
  if (msg.windowState) return;
  if ("levels" in msg) { focusView.levels(msg.levels, msg.spectrum); return; }
  if (msg.focusPresets) focusView.discover(msg.focusPresets);
  if (msg.repeat && msg.repeat !== state.repeat) {
    if (state.repeat) toast(repeatModes[msg.repeat].label);
    try { localStorage.setItem("repeatMode", msg.repeat); } catch {}
  }
  state = { ...state, ...msg };
  if (msg.sources) renderSources(msg.sources);
  if (msg.tracks) {
    tracks = msg.tracks;
    hiddenTrackCount = tracks.reduce((n, t) => n + (t.hidden ? 1 : 0), 0);
    const valid = new Set(tracks.map(t => t.url));
    if (!msg.historyResult) {
      try {
        lists?.prune(valid);
        const kept = [...fav].filter(u => valid.has(u));
        if (kept.length !== fav.size) { localStorage.setItem("likes", JSON.stringify(kept)); fav = new Set(kept); }
      }
      catch { toast("歌单更新失败"); }
    }
  }
  if (msg.importFailed) document.querySelector("dialog.pluginConfirm[open]")?.close();
  if (msg.collectionPreview) reviewCollection(msg.collectionPreview);
  if (msg.collectionPending && pendingImport) pendingImport.collection = true;
  $("importStatus").hidden = !(pendingImport?.collection && !msg.imported && !msg.importFailed);
  if (msg.error && !msg.imported) {
    if (msg.importFailed || !pendingImport?.collection) {
      pendingImport = null;
      if ($("addDialog").open) {
        $("url").setCustomValidity(msg.error);
        $("url").reportValidity();
        $("url").setCustomValidity(""); // Allow retry after a transient provider error.
      } else toast(msg.error);
    } else toast(msg.error); // An unrelated playback error is not an import response.
  }
  if (msg.imported && pendingImport) {
    const {page: target, url, mixName, priorOrder} = pendingImport;
    const urls = msg.importedUrls || [url];
    pendingImport = null;
    let notice = "已加入音乐库";
    if (mixName) {
      try {
        createdList = lists.create(mixName, urls); trackOrder.save(createdList, urls);
        refreshListNav(); select(createdList); notice = "已导入分享歌单";
      } catch { notice = "已加入音乐库，但无法完整保存歌单或排序"; }
    } else if (target === "likes") {
      notice = updateLikes(urls, true) ? "已加入我喜欢的" : "";
    } else if (target !== "library" && lists?.find(target)) {
      try {
        lists.addSongs(urls, new Set([target]));
        rowSignature = "";
        notice = "已加入歌单";
      } catch { notice = "已加入音乐库，但无法保存到歌单"; }
    }
    if (priorOrder && notice && !notice.includes("但无法")) {
      // Append only new members, in file order; keep manual ordering and hidden tracks.
      const merged = new Set([...priorOrder, ...urls]);
      if (merged.size > priorOrder.length) {
        try { trackOrder.save(target, [...merged]); rowSignature = ""; }
        catch { notice = "已导入，但无法保存曲目顺序"; }
      }
    }
    if ($("addDialog").open) {
      $("addDialog").close();
      $("addForm").reset();
      select(page);
    }
    if (notice && msg.importedUrls) notice += `（${urls.length} 首）`;
    if (msg.error || notice.includes("但无法")) toast([notice, msg.error].filter(Boolean).join("；"));
    else if (!msg.libraryEdit && editHistory.notices) toast(notice);
  }
  if (msg.libraryEdit) {
    const edit = msg.libraryEdit;
    const label = edit.kind === "delete" ? "删除歌曲" : edit.kind === "collection" ? "导入歌单" : "导入音乐";
    const after = membershipSnapshot(), valid = new Set(tracks.map(track => track.url));
    after.likes = after.likes.filter(url => valid.has(url));
    after.lists = after.lists.map(list => ({...list, urls: list.urls.filter(url => valid.has(url))}));
    editHistory.record({label: `${label}（${edit.count} 首）`, nativeId: edit.id, createdList, before: editBefore, after});
    if (msg.error) toast(msg.error); // Error messages are never suppressed by the preference.
  }
  sync();
  editHistory.refresh();
  if (msg.historyResult && nativeHistoryRequest && msg.historyResult.id === nativeHistoryRequest.id &&
      msg.historyResult.direction === nativeHistoryRequest.direction) {
    const request = nativeHistoryRequest; nativeHistoryRequest = null;
    if (msg.historyResult.ok) request.resolve();
    else request.reject(Error(msg.historyResult.error || "撤销操作失败"));
  }
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
