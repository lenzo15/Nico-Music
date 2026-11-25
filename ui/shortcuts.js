"use strict";
function createPlaybackShortcuts(notify) {
  const labels = {
    prev: {label: "上一首", keys: "Ctrl + Alt + ←"},
    next: {label: "下一首", keys: "Ctrl + Alt + →"},
    toggle: {label: "播放 / 暂停", keys: "Ctrl + Alt + 空格"},
  };
  const control = id => document.getElementById(id);
  function run(action) {
    if (Object.hasOwn(labels, action)) control(action).click();
  }
  function showStatus(registered) {
    const unavailable = [];
    for (const [action, {label, keys}] of Object.entries(labels)) {
      const scope = registered[action] ? "全局" : "仅窗口内";
      control(action).title = `${label}（${keys}，${scope}）${action === "toggle" ? "；窗口内也可用空格" : ""}`;
      if (!registered[action]) unavailable.push(keys);
    }
    const hint = control("shortcutHint");
    hint.textContent = unavailable.length
      ? `全局快捷键注册失败：${unavailable.join("、")}，可能已被其他程序占用；这些组合键仍可在播放器窗口内使用。`
      : "组合键在后台或收起到托盘时也可用；窗口内还可用空格播放 / 暂停（输入时除外）。";
    if (unavailable.length) notify(hint.textContent);
  }
  document.addEventListener("keydown", e => {
    if (e.defaultPrevented || e.isComposing) return;
    // Registered global chords are consumed by Windows. This also provides a
    // window-local fallback when another app owns one of the combinations.
    if (e.ctrlKey && e.altKey && !e.shiftKey && !e.metaKey) {
      const action = {ArrowLeft: "prev", ArrowRight: "next", Space: "toggle"}[e.code];
      if (action) {
        e.preventDefault();
        if (!e.repeat) run(action);
      }
      return;
    }
    const target = e.target instanceof Element ? e.target : null;
    if (e.ctrlKey || e.altKey || e.shiftKey || e.metaKey ||
        document.querySelector("dialog[open]") || target?.isContentEditable ||
        target?.closest("input,textarea,button,select,a[href],[role=button]")) return;
    if (e.code === "Space") {
      e.preventDefault();
      if (!e.repeat) run("toggle");
    }
  });
  return {
    message(msg) {
      if (msg.shortcut) { run(msg.shortcut); return true; }
      if (msg.hotkeys) { showStatus(msg.hotkeys); return true; }
      return false;
    },
  };
}
