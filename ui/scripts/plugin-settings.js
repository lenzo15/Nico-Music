"use strict";
// Non-secret visual preferences only. Credentials always go through the native helper.
function presetPreferences(id) {
  try { return JSON.parse(localStorage.getItem(`preset.settings.${id}`)); } catch { return null; }
}
function createPluginSettings(credentials) {
  const dialog = $("pluginConfig");
  let page, catalog;
  function unload() {
    clearTimeout(page?.request?.timer);
    page = undefined; $("pluginConfigPage").replaceChildren();
  }
  dialog.addEventListener("close", () => { if (!dialog.open) unload(); });
  function open(row) {
    if (dialog.open) return;
    const frame = document.createElement("iframe");
    frame.setAttribute("sandbox", "allow-scripts");
    frame.title = $("pluginConfigTitle").textContent = `${row.name} · 设置`;
    frame.src = sourceUrl({id:row.id, revision:row.settingsRevision}, "settings.html");
    page = {source:row, frame};
    $("pluginConfigPage").replaceChildren(frame); dialog.showModal();
  }
  function discover(sources, presets) {
    const signature = JSON.stringify([sources, presets]);
    if (signature === catalog) return;
    catalog = signature;
    const rows = new Map();
    for (const [kind, items] of [["isSource", sources], ["isPreset", presets]]) {
      for (const item of items) {
        if (!/^[a-z0-9-]{1,64}$/.test(item.id)) continue;
        const row = rows.get(item.id) || {...item, isSource:false, isPreset:false};
        row[kind] = true; rows.set(item.id, row);
      }
    }
    if (page) {
      const next = rows.get(page.source.id);
      if (!next || next.settingsRevision !== page.source.settingsRevision || next.isSource !== page.source.isSource || next.isPreset !== page.source.isPreset) {
        unload(); dialog.close();
      }
    }
    $("pluginVersions").replaceChildren(...Array.from(rows.values(), row => {
      const li = document.createElement("li"), actions = document.createElement("div");
      const kinds = [row.isSource && "音源", row.isPreset && "专注预设"].filter(Boolean);
      const mark = document.createElement("i");
      mark.className = row.isSource ? "pluginMark sourceMark" : "pluginMark presetMark";
      mark.innerHTML = icon(row.isSource ? "disc" : "presets");
      if (row.isSource && row.icon) {
        const image = document.createElement("img"); image.alt = "";
        image.onload = () => mark.replaceChildren(image);
        image.src = sourceUrl(row, row.icon);
      }
      const info = document.createElement("div"), name = document.createElement("strong"), meta = document.createElement("small");
      info.className = "pluginInfo"; name.textContent = row.name;
      meta.textContent = kinds.join("、") + "  ·  " + (row.version ? "v" + row.version : "未声明版本");
      info.append(name, meta); li.append(mark, info);
      actions.className = "pluginActions";
      function button(label, description, click) {
        const b = document.createElement("button"); b.type = "button"; b.className = "pluginButton";
        b.textContent = label; b.title = description; b.setAttribute("aria-label", description);
        b.setAttribute("aria-haspopup", "dialog"); b.onclick = click; actions.append(b);
      }
      if (credentials.has(row.id)) button("配置登录", `配置登录：${row.credentialSettings.title || row.name}`, () => credentials.open(row.id));
      if (row.settingsRevision) button("插件设置", `插件设置：${row.name}`, () => open(row));
      if (actions.childElementCount) li.append(actions);
      return li;
    }));
    $("pluginCount").textContent = rows.size;
    $("noPluginVersions").hidden = rows.size > 0;
  }
  // Storage keys and reply targets are derived from the open dialog, never from frame input.
  window.addEventListener("message", event => {
    const msg = event.data;
    if (!dialog.open || !page || event.source !== page.frame.contentWindow || event.origin !== "null" ||
        !page.source.isPreset || !["preset-read", "preset-write"].includes(msg?.type) ||
        typeof msg.id !== "string" || msg.id.length > 64) return;
    const id = page.source.id, reply = {type:"preset-response", id:msg.id};
    try {
      if (msg.type === "preset-write") {
        const data = JSON.stringify(msg.data ?? null);
        if (new TextEncoder().encode(data).length > 16384) throw new Error();
        localStorage.setItem(`preset.settings.${id}`, data);
      }
      reply.data = presetPreferences(id);
    } catch { reply.error = "配置无法保存"; }
    event.source.postMessage(reply, "*");
  });
  return {discover, page: () => dialog.open ? page : undefined};
}
