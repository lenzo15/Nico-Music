// node --test tests/updates.js
"use strict";
const {test} = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs"), path = require("node:path"), vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../ui/updates.js"), "utf8");
const prefix = "https://github.com/lenzo15/Nico-Music/releases/download/";
const asset = {name: "NicoMusic-v2.0.0-windows-x64.zip", state: "uploaded", size: 1048576,
  browser_download_url: prefix + "v2.0.0/NicoMusic-v2.0.0-windows-x64.zip", digest: "sha256:" + "a".repeat(64)};
const release = (tag = "v2.0.0", assets = [asset]) => ({tag_name: tag, draft: false, prerelease: false, assets});
function setup({data = {}, desktop = true, brokenStorage = false} = {}) {
  const controls = {}, sent = [], notices = [];
  for (const id of ["checkUpdates", "downloadUpdate", "cancelUpdate", "showDownload", "autoUpdate", "info",
    "updateStatus", "appVersion", "downloadProgress", "updateProgress", "downloadBytes"]) {
    controls[id] = {hidden: false, disabled: false, textContent: "", dataset: {}};
  }
  const storage = {
    getItem(key) { if (brokenStorage) throw Error("unavailable"); return data[key] ?? null; },
    setItem(key, value) { if (brokenStorage) throw Error("unavailable"); data[key] = value; },
  };
  const clock = {value: 100000000};
  const context = vm.createContext({document: {getElementById: id => controls[id]}});
  vm.runInContext(source, context);
  const checker = context.createUpdateChecker({send: value => sent.push(value), notify: value => notices.push(value),
    desktop, storage, now: () => clock.value});
  return {controls, sent, notices, data, clock, checker, context,
    begin(version = "1.0.0") { checker.message({appVersion: version}); },
    checked(value = release(), httpStatus = 200) {
      checker.message({updater: {phase: "checked", httpStatus, body: JSON.stringify(value)}});
    },
    state(phase, extra = {}) { checker.message({updater: {phase, ...extra}}); },
  };
}

test("semantic versions compare numerically, ignoring build metadata", () => {
  const {context: c} = setup();
  for (const [a, b, expected] of [["v1.10.0", "1.9.9", 1], ["1.0.0", "v1.0.0", 0],
    ["1.0.0+one", "1.0.0+two", 0], ["1.0.0-beta.2", "1.0.0-beta.11", -1],
    ["1.0.0-rc.1", "1.0.0", -1], ["1.0.0-alpha.1", "1.0.0-alpha.beta", -1],
    ["10.0.0", "2.99.99", 1], ["999999999999999999.0.0", "9.0.0", 1],
    ["v2.0.0", "v2.0.1", -1]]) {
    assert.equal(c.compareUpdateVersions(a, b), expected, `${a} vs ${b}`);
  }
  for (const bad of ["", "v1", "1.2", "latest", "01.2.3", "1.0.0-01", "1.0.0<script>", null])
    assert.equal(c.compareUpdateVersions(bad, "1.0.0"), null);
});

test("startup checks once and advertises only strictly newer stable releases", () => {
  const h = setup();
  h.begin(); h.begin();
  assert.deepEqual(h.sent, ["check-update"]);
  assert.equal(h.controls.appVersion.textContent, "v1.0.0");
  assert.equal(h.controls.checkUpdates.disabled, true);
  h.checked();
  assert.match(h.controls.updateStatus.textContent, /发现新版本 v2.0.0/);
  assert.equal(h.controls.downloadUpdate.hidden, false);
  assert.equal(h.controls.info.dataset.update, "true");
  assert.equal(h.notices.length, 1);
  h.checked();
  assert.equal(h.notices.length, 1);
  for (const tag of ["v1.0.0", "v0.9.9"]) {
    const current = setup(); current.begin(); current.checked(release(tag));
    assert.equal(current.controls.downloadUpdate.hidden, true);
    assert.equal(current.notices.length, 0);
  }
});

test("six-hour cache, opt-out and manual checks respect cooldown", () => {
  const h = setup(); h.begin(); h.checked();
  const cached = setup({data: h.data}); cached.begin();
  assert.deepEqual(cached.sent, []);
  assert.equal(cached.controls.downloadUpdate.hidden, false);
  cached.controls.checkUpdates.onclick();
  assert.deepEqual(cached.sent, ["check-update"]);
  cached.checked(); cached.controls.checkUpdates.onclick();
  assert.equal(cached.sent.length, 1);
  cached.clock.value += 60001; cached.controls.checkUpdates.onclick();
  assert.equal(cached.sent.length, 2);
  const disabled = setup({data: {"nico.autoUpdateCheck": "off"}}); disabled.begin();
  assert.deepEqual(disabled.sent, []);
  disabled.controls.checkUpdates.onclick();
  assert.deepEqual(disabled.sent, ["check-update"]);
  disabled.checked();
  disabled.controls.autoUpdate.checked = false; disabled.controls.autoUpdate.onchange();
  assert.equal(disabled.data["nico.autoUpdateCheck"], "off");
  const old = JSON.parse(h.data["nico.updateCheck.v1"]); old.checkedAt -= 7 * 3600000;
  const expired = setup({data: {"nico.updateCheck.v1": JSON.stringify(old)}}); expired.begin();
  assert.deepEqual(expired.sent, ["check-update"]);
});

test("malformed responses, previews, HTTP failures and offline checks remain retryable", () => {
  for (const value of [null, {}, release("not-a-version"), {...release(), prerelease: true},
    {...release(), draft: true}, release("v2.0.0-beta.1")]) {
    const h = setup(); h.begin(); h.checked(value);
    assert.equal(h.controls.downloadUpdate.hidden, true);
    assert.equal(h.controls.checkUpdates.disabled, false);
    assert.equal(h.notices.length, 0);
  }
  for (const code of [403, 429, 500]) {
    const h = setup(); h.begin(); h.checked({}, code);
    assert.match(h.controls.updateStatus.textContent, /受限|失败/);
    assert.equal(h.controls.checkUpdates.disabled, false);
  }
  const empty = setup(); empty.begin(); empty.checked(null, 404);
  assert.match(empty.controls.updateStatus.textContent, /暂无正式发布/);
  const offline = setup({brokenStorage: true}); offline.begin(); offline.state("check-error", {error: "网络不可用"});
  assert.equal(offline.controls.updateStatus.textContent, "网络不可用");
  assert.equal(offline.controls.checkUpdates.disabled, false);
  const preview = setup({desktop: false}); preview.begin();
  assert.deepEqual(preview.sent, []);
  assert.equal(preview.controls.checkUpdates.disabled, true);
});

test("selects only trusted Windows x64 ZIPs and rejects source, unsafe URLs and bad checksums", () => {
  const {context: c} = setup();
  const sourceZip = {...asset, name: "NicoMusic-source.zip"};
  assert.equal(c.selectUpdateAsset(release("v2.0.0", [sourceZip, asset])), asset);
  for (const changed of [{browser_download_url: "https://evil.example/update.zip"},
    {browser_download_url: "http://github.com/lenzo15/Nico-Music/releases/download/x.zip"},
    {browser_download_url: prefix + "x.zip\n"}, {browser_download_url: prefix + "../../other/windows-x64.zip"},
    {name: "../windows-x64.zip"},
    {name: "windows-arm64.zip"}, {name: "windows-x64.exe"}, {digest: "sha256:bad"},
    {size: 0}, {size: 3 * 1024 ** 3}, {state: "new"}]) {
    assert.equal(c.selectUpdateAsset(release("v2.0.0", [{...asset, ...changed}])), null);
  }
  const missing = setup(); missing.begin(); missing.checked(release("v2.0.0", [sourceZip]));
  assert.match(missing.controls.updateStatus.textContent, /暂未提供/);
  assert.equal(missing.controls.downloadUpdate.hidden, true);
});

test("download uses the native bridge without a browser, shows progress, cancels and opens its location", () => {
  const h = setup(); h.begin(); h.checked(); h.controls.downloadUpdate.onclick();
  assert.equal(h.sent[1], `download-update ${asset.browser_download_url} ${asset.size} ${"a".repeat(64)} ${asset.name}`);
  assert.equal(h.controls.checkUpdates.disabled, true);
  h.controls.downloadUpdate.onclick();
  assert.equal(h.sent.length, 2);
  h.state("downloading", {received: 524288, total: 1048576});
  assert.equal(h.controls.downloadProgress.hidden, false);
  assert.equal(h.controls.updateProgress.value, 524288);
  assert.match(h.controls.downloadBytes.textContent, /50%/);
  h.controls.cancelUpdate.onclick();
  assert.equal(h.sent.at(-1), "cancel-update");
  assert.equal(h.controls.cancelUpdate.disabled, true);
  h.state("cancelled");
  assert.equal(h.controls.checkUpdates.disabled, false);
  assert.match(h.controls.updateStatus.textContent, /临时文件已删除/);
  h.controls.downloadUpdate.onclick(); h.state("download-error", {error: "SHA-256 校验失败"});
  assert.match(h.controls.updateStatus.textContent, /校验失败/);
  assert.equal(h.controls.downloadUpdate.disabled, false);
  h.controls.downloadUpdate.onclick(); h.state("downloaded", {file: "C:\\下载\\NicoMusic.zip"});
  assert.equal(h.controls.showDownload.hidden, false);
  assert.match(h.controls.updateStatus.textContent, /退出播放器后解压/);
  h.controls.showDownload.onclick();
  assert.equal(h.sent.at(-1), "show-update");
});

test("save-dialog cancellation restores buttons; corrupt cache falls back to a network check", () => {
  const h = setup({data: {"nico.updateCheck.v1": "{invalid"}}); h.begin(); h.checked();
  h.controls.downloadUpdate.onclick(); h.state("save-cancelled");
  assert.equal(h.controls.downloadUpdate.disabled, false);
  assert.equal(h.controls.checkUpdates.disabled, false);
  assert.equal(h.controls.cancelUpdate.hidden, true);
  assert.equal(h.checker.message({playing: true}), false);
});
