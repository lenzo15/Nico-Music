"use strict";
function createPlaybackShortcuts(notify, adjustVolume) {
  const labels = {
    prev: {label: "上一首", media: "MediaPrevious", mediaStatus: "mediaPrev"},
    next: {label: "下一首", media: "MediaNext", mediaStatus: "mediaNext"},
    toggle: {label: "播放 / 暂停", media: "MediaPlayPause", mediaStatus: "mediaToggle"},
  };
  const control = id => document.getElementById(id);
  const actions = ["prev", "next", "toggle"];
  let bindings = defaultHotkeyBindings(), captureMode = false, lastUnavailable = "";
  // Fixed hardware bindings, independent of the combination-key shortcuts.
  const media = {MediaPlayPause: "toggle", MediaTrackPrevious: "prev", MediaPrevious: "prev",
    MediaTrackNext: "next", MediaNext: "next", VolumeUp: "volumeUp", AudioVolumeUp: "volumeUp",
    VolumeDown: "volumeDown", AudioVolumeDown: "volumeDown"};
  function run(action) {
    if (action === "volumeUp" || action === "volumeDown") adjustVolume(action === "volumeUp" ? 5 : -5);
    else if (Object.hasOwn(labels, action)) control(action).click();
  }
  function showStatus(registered) {
    const unavailable = [], unavailableMedia = [];
    for (const [index, action] of actions.entries()) {
      const {label, media, mediaStatus} = labels[action], binding = bindings[index];
      const keys = hotkeyText(binding);
      const scope = binding.key ? registered[action] ? "全局" : "仅窗口内" : "未启用";
      const mediaScope = registered[mediaStatus] ? "全局" : "仅窗口内";
      control(action).title = `${label}（${keys}，${scope}；${media}，${mediaScope}）${action === "toggle" ? "；窗口内也可用空格" : ""}`;
      const aria = [action === "toggle" ? "Space" : "", hotkeyText(binding, true),
        {prev: "MediaTrackPrevious", next: "MediaTrackNext", toggle: "MediaPlayPause"}[action]].filter(Boolean).join(" ");
      control(action).setAttribute("aria-keyshortcuts", aria);
      if (binding.key && !registered[action]) unavailable.push(keys);
      if (!registered[mediaStatus]) unavailableMedia.push(media);
    }
    for (const key of ["volumeUp", "volumeDown"]) {
      if (!registered[key]) unavailableMedia.push(key === "volumeUp" ? "VolumeUp" : "VolumeDown");
    }
    control("volume").title = `播放器音量（VolumeUp：${registered.volumeUp ? "全局" : "仅窗口内"}；VolumeDown：${registered.volumeDown ? "全局" : "仅窗口内"}，每次 5%，支持长按）`;
    const unavailableKeys = [...unavailable, ...unavailableMedia];
    const notice = control("hotkeyAvailability");
    notice.hidden = !unavailableKeys.length;
    notice.textContent = unavailableKeys.length ? `快捷键注册失败：${unavailableKeys.join("、")}。可能被其他程序占用。` : "";
    const signature = unavailableKeys.join("|");
    if (signature && signature !== lastUnavailable) notify(notice.textContent);
    lastUnavailable = signature;
  }
  document.addEventListener("keydown", e => {
    if (e.defaultPrevented) return;
    // Standard DOM names and the MediaPrevious/MediaNext driver aliases.
    // Explicit media keys remain usable in inputs, IME composition and dialogs.
    if (!e.ctrlKey && !e.altKey && !e.shiftKey && !e.metaKey) {
      const action = Object.hasOwn(media, e.code) ? media[e.code] : Object.hasOwn(media, e.key) ? media[e.key] : null;
      if (action) {
        e.preventDefault();
        if (!e.repeat || action === "volumeUp" || action === "volumeDown") run(action);
        return;
      }
    }
    if (e.isComposing || captureMode) return;
    // Registered global chords are consumed by Windows; this is the local fallback.
    const pressed = hotkeyFromEvent(e);
    const match = pressed && bindings.findIndex(b => b.key === pressed.key && b.modifiers === pressed.modifiers);
    if (Number.isInteger(match) && match >= 0) {
      e.preventDefault();
      if (!e.repeat) run(actions[match]);
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
    bindings: () => bindings.map(b => ({...b})),
    message(msg) {
      if (msg.shortcut) { run(msg.shortcut); return true; }
      if (validHotkeyBindings(msg.hotkeyConfig)) bindings = msg.hotkeyConfig.map(b => ({...b}));
      if (typeof msg.hotkeyCapture === "boolean") captureMode = msg.hotkeyCapture;
      if (msg.hotkeys) { if (!captureMode) showStatus(msg.hotkeys); return true; }
      return !!msg.hotkeyConfig;
    },
  };
}
