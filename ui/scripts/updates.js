"use strict";
function parseUpdateVersion(value) {
  if (typeof value !== "string" || value.length > 128) return null;
  const match = /^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/.exec(value);
  if (!match) return null;
  const pre = match[4]?.split(".") || [];
  if (pre.some(part => /^\d+$/.test(part) && part.length > 1 && part[0] === "0")) return null;
  return {core: match.slice(1, 4).map(BigInt), pre};
}
function compareUpdateVersions(left, right) {
  const a = parseUpdateVersion(left), b = parseUpdateVersion(right);
  if (!a || !b) return null;
  for (let i = 0; i < 3; i++) if (a.core[i] !== b.core[i]) return a.core[i] > b.core[i] ? 1 : -1;
  if (!a.pre.length || !b.pre.length) return a.pre.length === b.pre.length ? 0 : a.pre.length ? -1 : 1;
  for (let i = 0; i < Math.max(a.pre.length, b.pre.length); i++) {
    const x = a.pre[i], y = b.pre[i];
    if (x === undefined || y === undefined) return x === y ? 0 : x === undefined ? -1 : 1;
    if (x === y) continue;
    const xn = /^\d+$/.test(x), yn = /^\d+$/.test(y);
    if (xn && yn) return BigInt(x) > BigInt(y) ? 1 : -1;
    if (xn !== yn) return xn ? -1 : 1;
    return x > y ? 1 : -1;
  }
  return 0;
}
function selectUpdateAsset(release) {
  const prefix = "https://github.com/lenzo15/Nico-Music/releases/download/";
  return (Array.isArray(release.assets) ? release.assets : []).find(asset => {
    if (!asset || asset.state !== "uploaded" || typeof asset.name !== "string" ||
        asset.name.length > 160 || asset.name.startsWith(".") || asset.name.includes("..") ||
        !/^[A-Za-z0-9_. -]*(?:windows|win)[-_]?x64\.zip$/i.test(asset.name) ||
        !Number.isSafeInteger(asset.size) || asset.size <= 0 || asset.size > 2 * 1024 ** 3 ||
        typeof asset.browser_download_url !== "string" ||
        asset.browser_download_url !== `${prefix}${encodeURIComponent(release.tag_name)}/${encodeURIComponent(asset.name)}` ||
        asset.browser_download_url.length > 4096 || /[\s\\"<>?#\x00-\x1f\x7f-\uffff]/.test(asset.browser_download_url)) return false;
    return /^sha256:[a-f0-9]{64}$/i.test(asset.digest);
  }) || null;
}
function createUpdateChecker({send, notify, desktop, storage, now = Date.now}) {
  const el = id => document.getElementById(id);
  const cacheKey = "nico.updateCheck.v1", preferenceKey = "nico.autoUpdateCheck";
  const cacheAge = 6 * 60 * 60 * 1000, cooldown = 60000;
  let currentVersion = "", latest = null, asset = null, available = false;
  let busy = false, lastAttempt = null, phase = "idle", announced = "";
  function read(key) { try { return storage.getItem(key); } catch { return null; } }
  function write(key, value) { try { storage.setItem(key, value); } catch {} }
  function text(value) { el("updateStatus").textContent = value; }
  function controls() {
    el("checkUpdates").disabled = busy || !currentVersion || !desktop;
    el("downloadUpdate").hidden = !available || !asset;
    el("downloadUpdate").disabled = busy;
    el("cancelUpdate").hidden = phase !== "downloading";
    el("cancelUpdate").disabled = false;
    el("settingsButton").dataset.update = String(available);
    el("settingsButton").title = available ? `设置（发现新版本 ${latest.tag_name}，可在关于中更新）` : "设置";
  }
  function releaseStatus() {
    if (!latest) return "暂无正式发布版本";
    if (!available) return `当前已是最新版本（最新正式版 ${latest.tag_name}）`;
    return `发现新版本 ${latest.tag_name}${asset ? "，点击更新后将自动安装并重启" : "，暂未提供可校验的 Windows x64 ZIP 安装包"}`;
  }
  function useRelease(release, announce = true) {
    if (release !== null) {
      const parsed = parseUpdateVersion(release?.tag_name);
      if (!parsed || parsed.pre.length || release.draft !== false || release.prerelease !== false)
        throw new Error("发布信息或版本标签无法识别，请稍后重试");
    }
    latest = release;
    available = !!release && compareUpdateVersions(release.tag_name, currentVersion) > 0;
    asset = available ? selectUpdateAsset(release) : null;
    text(releaseStatus());
    controls();
    if (available && announce && announced !== release.tag_name) {
      announced = release.tag_name;
      notify(`发现 NicoMusic ${release.tag_name}，可在「设置 → 关于」中下载更新`);
    }
  }
  function check() {
    if (busy || !currentVersion || !desktop) return;
    if (lastAttempt !== null && now() >= lastAttempt && now() - lastAttempt < cooldown) {
      text("刚刚已检查，请稍后再试（每分钟最多检查一次）");
      return;
    }
    lastAttempt = now(); busy = true; phase = "checking";
    el("downloadProgress").hidden = true;
    text("正在检查 GitHub 最新正式版…"); controls();
    send("check-update");
  }
  el("autoUpdate").checked = read(preferenceKey) !== "off";
  el("autoUpdate").onchange = () => {
    write(preferenceKey, el("autoUpdate").checked ? "on" : "off");
    if (el("autoUpdate").checked && currentVersion) check();
  };
  el("checkUpdates").onclick = check;
  el("downloadUpdate").onclick = () => {
    if (busy || !asset || !available || !desktop) return;
    busy = true; phase = "preparing";
    el("downloadProgress").hidden = true;
    text("正在准备更新…"); controls();
    const hash = asset.digest.slice(7).toLowerCase();
    send(`download-update ${asset.browser_download_url} ${asset.size} ${hash} ${asset.name}`);
  };
  el("cancelUpdate").onclick = () => {
    if (phase !== "downloading") return;
    send("cancel-update");
    el("cancelUpdate").disabled = true;
    text("正在取消下载…");
  };
  if (!desktop) text("请在 NicoMusic 桌面窗口中检查和下载更新");
  controls();
  function begin(version) {
    if (currentVersion === version) return;
    if (!parseUpdateVersion(version)) { text("当前程序版本无法识别"); return; }
    currentVersion = version;
    el("appVersion").textContent = "v" + version.replace(/^v/, "");
    controls();
    if (!desktop) return;
    if (!el("autoUpdate").checked) { text("已关闭启动时自动检查，可手动检查更新"); return; }
    try {
      const cache = JSON.parse(read(cacheKey));
      if (cache && Number.isFinite(cache.checkedAt) && now() >= cache.checkedAt && now() - cache.checkedAt < cacheAge &&
          Object.hasOwn(cache, "release")) {
        useRelease(cache.release);
        return;
      }
    } catch {}
    check();
  }
  function status(state) {
    if (state.phase === "idle") return;
    phase = state.phase;
    busy = ["checking", "downloading", "installing"].includes(phase);
    if (phase === "checking") text("正在检查 GitHub 最新正式版…");
    else if (phase === "checked") {
      try {
        if (state.httpStatus !== 200 && state.httpStatus !== 404)
          throw new Error([403, 429].includes(state.httpStatus) ? "GitHub 请求受限，请稍后重试" : `检查失败（HTTP ${state.httpStatus}），请稍后重试`);
        const release = state.httpStatus === 404 ? null : JSON.parse(state.body);
        if (state.httpStatus === 200 && !release) throw new Error("无法读取发布信息，请重试");
        useRelease(release);
        const selected = release && selectUpdateAsset(release);
        const cached = release ? {tag_name: release.tag_name, draft: false, prerelease: false, assets: selected ? [selected] : []} : null;
        write(cacheKey, JSON.stringify({checkedAt: now(), release: cached}));
      } catch (error) { text(error.message || "无法读取发布信息，请重试"); }
    } else if (phase === "downloading") {
      const received = Math.max(0, Number(state.received) || 0), total = Math.max(received, Number(state.total) || 0);
      const percent = total ? Math.floor(received / total * 100) : 0;
      el("downloadProgress").hidden = false;
      el("updateProgress").max = total || 1;
      el("updateProgress").value = received;
      el("downloadBytes").textContent = `${(received / 1024 ** 2).toFixed(1)} / ${(total / 1024 ** 2).toFixed(1)} MB（${percent}%）`;
      text("正在下载更新，完成后将校验安装包…");
    } else if (phase === "installing") {
      el("downloadProgress").hidden = true;
      text("正在解压并检查更新，完成后播放器将自动重启…");
    } else if (phase === "cancelled") {
      el("downloadProgress").hidden = true;
      text("已取消下载，临时文件已删除");
    } else if (phase === "check-error" || phase === "download-error") {
      el("downloadProgress").hidden = true;
      text(state.error || "更新任务失败，请重试");
    }
    controls();
  }
  return {message(msg) {
    if (msg.appVersion) begin(msg.appVersion);
    if (msg.updater) status(msg.updater);
    return !!(msg.appVersion || msg.updater);
  }};
}
