"use strict";
function createSourceCredentials({send, desktop}) {
  const el = id => document.getElementById(id);
  const dialog = el("sourceCredentialDialog"), form = el("sourceCredentialForm"), input = el("sourceCredentialInput");
  const message = el("sourceCredentialStatus"), save = el("saveSourceCredential"), clear = el("clearSourceCredential");
  let sources = new Map(), active = "", stored = false, pending, serial = 0;
  const title = source => source.credentialSettings.title || `${source.name} · 登录凭据`;
  function status(text, error = false) {
    message.textContent = text;
    message.toggleAttribute("data-error", error);
  }
  function render() {
    input.readOnly = !!pending;
    save.disabled = !!pending || !input.value.trim();
    clear.disabled = !!pending || !stored;
  }
  function request(action, value = "") {
    if (pending || !dialog.open || !sources.has(active)) return;
    const id = `${Date.now()}${++serial}`;
    pending = {id, action, source: active, timeout: setTimeout(() => reply({id, status: "failed"}), 7000)};
    render();
    status(action === "status" ? "正在读取本地状态…" : "正在处理…");
    if (!send(`source-credential ${id} ${active} ${action}${value ? ` ${value}` : ""}`))
      reply({id, status: "failed"});
  }
  function reply({id, status: result}) {
    if (!pending || pending.id !== id) return;
    const {source, action, timeout} = pending;
    clearTimeout(timeout); pending = null;
    if (!dialog.open) return;
    if (source !== active) { request("status"); return; }
    if ((result === "saved" && action !== "clear") || (result === "empty" && action !== "save")) {
      stored = result === "saved";
      status(action === "save" ? "已保存，返回歌曲列表即可重试播放。" : action === "clear" ?
        "已清除播放器中的登录凭据。" : stored ? "已有本地凭据。保存新凭据将替换它。" : "尚未保存登录凭据。");
    } else if (result === "invalid") status("登录凭据格式无效，请按说明重新粘贴。", true);
    else if (result === "unavailable") status("音源设置不可用，请检查插件是否安装完整或更新后重试。", true);
    else status("操作未完成，请稍后重试。", true);
    render();
    if (action !== "status") input.focus();
  }
  function open(id) {
    const source = sources.get(id);
    if (!source || dialog.open) return;
    if (active !== id) stored = false;
    active = id;
    const settings = source.credentialSettings;
    el("sourceCredentialTitle").textContent = title(source);
    el("sourceCredentialHelp").textContent = settings.help?.replaceAll("\\n", "\n") || "粘贴该音源的登录凭据。不要与他人分享。";
    el("sourceCredentialLabel").textContent = settings.label || "登录凭据";
    input.placeholder = settings.placeholder || "粘贴登录凭据";
    input.value = "";
    status("正在读取本地状态…");
    render(); dialog.showModal(); input.focus();
    request("status");
  }
  dialog.addEventListener("close", () => { if (!dialog.open) { input.value = ""; render(); } });
  input.oninput = render;
  input.onpaste = event => {
    if (/[\r\n\0]/.test(event.clipboardData?.getData("text") || "")) {
      event.preventDefault(); status("请粘贴单行登录凭据。", true);
    }
  };
  form.onsubmit = event => {
    event.preventDefault();
    const value = input.value.trim();
    if (pending || !value || !dialog.open) return;
    if (/[\r\n\0]/.test(value) || new TextEncoder().encode(value).length > input.maxLength) {
      status("请粘贴不超过 16 KB 的单行登录凭据。", true); return;
    }
    input.value = "";
    request("save", value);
  };
  clear.onclick = () => { if (!clear.disabled) { input.value = ""; request("clear"); } };
  function discover(items) {
    const supported = desktop ? items.filter(item => /^[a-z0-9-]{1,64}$/.test(item.id) && item.credentialSettings) : [];
    sources = new Map(supported.map(source => [source.id, source]));
    if (dialog.open && !sources.has(active)) { input.value = ""; dialog.close(); }
  }
  return {discover, open, reply, has: id => sources.has(id)};
}
