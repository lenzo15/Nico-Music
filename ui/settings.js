"use strict";
function createSettings({shortcuts, send, notify, desktop}) {
  const el = id => document.getElementById(id), dialog = el("settings");
  const actions = ["prev", "next", "toggle"];
  let draft = shortcuts.bindings(), ready = false, saving = false, target = -1, recording = false, captureRequest = 0;
  let loadWarning = "", retryRegistration = false;
  function sendCapture(enabled) { send(`hotkeys-capture ${enabled ? "on" : "off"} ${++captureRequest}`); }
  const dirty = () => JSON.stringify(draft) !== JSON.stringify(shortcuts.bindings());
  const needsSave = () => dirty() || !!loadWarning || retryRegistration;
  const status = (text = "") => {
    const message = el("hotkeyEditStatus");
    message.textContent = text; message.hidden = !text;
  };
  function render() {
    for (const [index, action] of actions.entries()) {
      const field = el("hotkey-" + action);
      field.value = target === index ? recording ? "请按组合键…" : "准备中…" : hotkeyText(draft[index]);
      field.classList.toggle("isRecording", target === index);
      field.disabled = !ready || !desktop || saving;
      field.title = target === index ? "按下 Ctrl 或 Alt 组合键，可加 Shift；Esc 取消，Backspace / Delete 清空" : "点击或按 Enter 修改；Backspace / Delete 清空";
      field.setAttribute("aria-description", field.title);
    }
    el("saveHotkeys").disabled = !ready || !desktop || saving || !needsSave();
    el("saveHotkeys").textContent = saving ? "保存中…" : "保存热键";
    el("resetHotkeys").disabled = !ready || !desktop || saving;
    el("cancelHotkeyChanges").disabled = saving || !dirty();
  }
  function stopCapture(sendRestore = true) {
    const active = target >= 0;
    target = -1; recording = false;
    if (active && sendRestore) sendCapture(false);
    render();
  }
  function showTab(name, focus = false) {
    stopCapture();
    for (const tab of dialog.querySelectorAll("[data-settings-tab]")) {
      const selected = tab.dataset.settingsTab === name;
      tab.setAttribute("aria-selected", String(selected)); tab.tabIndex = selected ? 0 : -1;
      el(tab.getAttribute("aria-controls")).hidden = !selected;
      if (selected && focus) tab.focus();
    }
    el("settingsBody").scrollTop = 0;
  }
  for (const tab of dialog.querySelectorAll("[data-settings-tab]")) {
    tab.onclick = () => showTab(tab.dataset.settingsTab);
    tab.onkeydown = event => {
      const tabs = [...dialog.querySelectorAll("[data-settings-tab]")], index = tabs.indexOf(tab);
      const next = {ArrowRight: (index + 1) % tabs.length, ArrowLeft: (index + tabs.length - 1) % tabs.length,
        Home: 0, End: tabs.length - 1}[event.key];
      if (next !== undefined) { event.preventDefault(); showTab(tabs[next].dataset.settingsTab, true); }
    };
  }
  el("settingsButton").onclick = () => {
    if (dialog.open) return;
    draft = shortcuts.bindings();
    status(!desktop ? "请在 NicoMusic 桌面窗口中编辑热键" : loadWarning);
    render(); showTab("hotkeys"); dialog.showModal(); el("settingsTabHotkeys").focus();
  };
  function record(index) {
    if (!ready || saving || !desktop) return;
    if (target === index) return;
    const alreadyRecording = recording;
    target = index;
    status();
    render();
    if (!alreadyRecording) sendCapture(true);
    el("hotkey-" + actions[index]).focus();
  }
  for (const [index, action] of actions.entries()) {
    const field = el("hotkey-" + action);
    field.onclick = () => record(index);
    field.onkeydown = event => {
      if (!ready || saving || !desktop || event.isComposing || event.ctrlKey || event.altKey || event.shiftKey || event.metaKey) return;
      if (["Enter", "Space", "Backspace", "Delete"].includes(event.code)) {
        event.preventDefault(); event.stopPropagation();
        if (event.code === "Enter" || event.code === "Space") record(index);
        else { stopCapture(); draft[index] = {modifiers: 0, key: 0}; status(); render(); }
      }
    };
    field.onblur = () => { if (target === index) { stopCapture(); status(); } };
  }
  document.addEventListener("keydown", event => {
    if (target < 0 || !dialog.open) return;
    // These fixed keys remain playback controls; they are never editor candidates.
    if (/^(Media|AudioVolume|Volume)/.test(event.code) || /^(Media|AudioVolume|Volume)/.test(event.key)) return;
    if (/^(Control|Alt|Shift|Meta)/.test(event.code)) return;
    if (event.code === "Tab" || event.key === "Tab") {
      stopCapture(); status(); return;
    }
    event.preventDefault(); event.stopPropagation();
    if (event.key === "Escape" || event.code === "Escape" || event.code === "Enter" && !event.ctrlKey && !event.altKey && !event.shiftKey && !event.metaKey) {
      stopCapture(); status(); return;
    }
    if (!recording || event.repeat) return;
    if (event.isComposing) { status("请先结束输入法组合，再录制热键"); return; }
    const clearing = ["Backspace", "Delete"].includes(event.code) && !event.ctrlKey && !event.altKey && !event.shiftKey && !event.metaKey;
    const binding = clearing ? {modifiers: 0, key: 0} : hotkeyFromEvent(event);
    if (!binding) { status("请使用 Ctrl 或 Alt 搭配字母、数字、F1–F11、空格或导航键；系统保留组合不可用"); return; }
    if (binding.key && draft.some((b, i) => i !== target && b.key === binding.key && b.modifiers === binding.modifiers)) {
      status("这个组合键已用于其他播放操作，请更换"); return;
    }
    draft[target] = binding; stopCapture(); render();
    status();
  }, true);
  el("resetHotkeys").onclick = () => {
    stopCapture(); draft = defaultHotkeyBindings(); render();
    status();
  };
  el("cancelHotkeyChanges").onclick = () => {
    stopCapture(); draft = shortcuts.bindings(); render(); status();
  };
  el("hotkeyForm").onsubmit = event => {
    event.preventDefault();
    if (!ready || !desktop || saving || !needsSave()) return;
    if (!validHotkeyBindings(draft)) { status("组合键无效或重复，请重新设置"); return; }
    stopCapture(); saving = true; render(); status();
    send("hotkeys-set " + draft.flatMap(b => [b.modifiers, b.key]).join(" "));
  };
  dialog.addEventListener("close", () => stopCapture());
  window.addEventListener("blur", () => {
    if (target >= 0) { stopCapture(); status(); }
  });
  render();
  return {message(msg) {
    if (typeof msg.hotkeyWarning === "string") loadWarning = msg.hotkeyWarning;
    if (msg.hotkeys && !msg.hotkeyCapture)
      retryRegistration = actions.some((action, index) => shortcuts.bindings()[index].key && !msg.hotkeys[action]);
    if (validHotkeyBindings(msg.hotkeyConfig)) {
      const first = !ready;
      ready = true;
      if (first || !dirty() && target < 0 && !saving) draft = shortcuts.bindings();
    }
    if (typeof msg.hotkeyCapture === "boolean" && target >= 0 &&
        (!msg.hotkeyCaptureRequest || msg.hotkeyCaptureRequest === captureRequest)) {
      if (msg.hotkeyCapture && dialog.open) { recording = true; render(); }
      else if (!msg.hotkeyCapture) { stopCapture(false); status(); }
    }
    if (msg.hotkeyResult) {
      saving = false;
      if (msg.hotkeyResult.ok) { draft = shortcuts.bindings(); status(); notify("热键设置已保存"); }
      else status(msg.hotkeyResult.error || "保存失败，原热键未修改");
    } else if (msg.hotkeyWarning && target < 0 && !dirty()) status(msg.hotkeyWarning);
    render();
  }};
}
