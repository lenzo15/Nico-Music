// Pure shortcut regression: node --test tests/shortcuts.js
"use strict";
const {test} = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../ui/scripts/hotkey-bindings.js"), "utf8") + "\n" +
  fs.readFileSync(path.join(__dirname, "../ui/scripts/shortcuts.js"), "utf8");

function setup() {
  const actions = [], notices = [], volumeChanges = [], controls = {};
  let keydown;
  class Element {
    constructor(interactive = false, editable = false) {
      this.interactive = interactive;
      this.isContentEditable = editable;
    }
    closest() { return this.interactive ? this : null; }
  }
  const document = {
    modal: false,
    getElementById: id => controls[id],
    querySelector() { return this.modal ? {} : null; },
    addEventListener(type, listener) { assert.equal(type, "keydown"); keydown = listener; },
  };
  for (const id of ["prev", "next", "toggle", "volume", "hotkeyAvailability"]) {
    controls[id] = {disabled: false, setAttribute(name, value) { this[name] = value; },
      click() { if (!this.disabled) actions.push(id); }};
  }
  const context = vm.createContext({document, Element, notify: s => notices.push(s), adjustVolume: delta => volumeChanges.push(delta)});
  vm.runInContext(source, context);
  const shortcuts = vm.runInContext("createPlaybackShortcuts(notify, adjustVolume)", context);
  return {actions, notices, volumeChanges, controls, document, Element, shortcuts, context,
    key(options = {}) {
      const e = {code: "Space", target: new Element(), defaultPrevented: false,
        preventDefault() { this.defaultPrevented = true; }, ...options};
      keydown(e);
      return e;
    },
  };
}

test("all three chords reuse transport buttons; disabled queue boundaries are respected", () => {
  const h = setup();
  for (const code of ["ArrowLeft", "ArrowRight", "Space"]) {
    assert.equal(h.key({code, ctrlKey: true, altKey: true}).defaultPrevented, true);
  }
  assert.deepEqual(h.actions, ["prev", "next", "toggle"]);
  h.controls.next.disabled = true;
  h.key({code: "ArrowRight", ctrlKey: true, altKey: true});
  assert.equal(h.actions.length, 3);
});

test("plain space only toggles once; long presses and IME do not retrigger", () => {
  const h = setup();
  assert.equal(h.key().defaultPrevented, true);
  assert.equal(h.key({repeat: true}).defaultPrevented, true);
  h.key({repeat: true, ctrlKey: true, altKey: true});
  h.key({isComposing: true});
  h.key({ctrlKey: true, altKey: true, isComposing: true});
  h.key({defaultPrevented: true});
  assert.deepEqual(h.actions, ["toggle"]);
});

test("space leaves input, contenteditable, buttons, links, dialogs and modified keys alone", () => {
  const h = setup();
  for (const target of [new h.Element(true), new h.Element(false, true)]) {
    assert.equal(h.key({target}).defaultPrevented, false);
  }
  for (const modifier of ["ctrlKey", "altKey", "shiftKey", "metaKey"]) {
    assert.equal(h.key({[modifier]: true}).defaultPrevented, false);
  }
  h.document.modal = true;
  assert.equal(h.key().defaultPrevented, false);
  assert.deepEqual(h.actions, []);
  // Explicit transport chords remain usable while typing or with a dialog open.
  h.key({target: new h.Element(true), ctrlKey: true, altKey: true});
  assert.deepEqual(h.actions, ["toggle"]);
});

test("unrelated keys and extra modifiers never control playback", () => {
  const h = setup();
  for (const options of [{code: "ArrowLeft"}, {code: "ArrowRight"},
    {code: "KeyA", ctrlKey: true, altKey: true},
    {ctrlKey: true, altKey: true, shiftKey: true},
    {ctrlKey: true, altKey: true, metaKey: true}]) {
    assert.equal(h.key(options).defaultPrevented, false);
  }
  assert.deepEqual(h.actions, []);
});

test("native hotkey messages work in the background and ignore unknown actions", () => {
  const h = setup();
  h.document.modal = true;
  for (const shortcut of ["prev", "next", "toggle", "toString", "close"]) {
    assert.equal(h.shortcuts.message({shortcut}), true);
  }
  assert.equal(h.shortcuts.message({volume: 30}), false);
  assert.deepEqual(h.actions, ["prev", "next", "toggle"]);
  h.controls.prev.disabled = true;
  h.shortcuts.message({shortcut: "prev"});
  assert.equal(h.actions.length, 3);
});

test("registration failure is visible and leaves local fallback usable", () => {
  const h = setup();
  const registered = {prev: true, next: true, toggle: true, mediaPrev: true, mediaNext: true, mediaToggle: true,
    volumeUp: true, volumeDown: true};
  h.shortcuts.message({hotkeys: registered});
  assert.equal(h.controls.hotkeyAvailability.hidden, true);
  assert.equal(h.controls.hotkeyAvailability.textContent, "");
  assert.equal(h.notices.length, 0);
  assert.match(h.controls.prev.title, /全局/);
  h.shortcuts.message({hotkeys: {...registered, next: false}});
  assert.match(h.controls.next.title, /仅窗口内/);
  assert.match(h.notices[0], /Ctrl \+ Alt \+ →/);
  assert.match(h.controls.hotkeyAvailability.textContent, /注册失败/);
  h.key({code: "ArrowRight", ctrlKey: true, altKey: true});
  assert.deepEqual(h.actions, ["next"]);
});

test("default media keys and driver aliases map to the same transport actions", () => {
  const h = setup();
  for (const code of ["MediaPlayPause", "MediaPrevious", "MediaNext", "MediaTrackPrevious", "MediaTrackNext"]) {
    assert.equal(h.key({code}).defaultPrevented, true);
  }
  assert.deepEqual(h.actions, ["toggle", "prev", "next", "prev", "next"]);
  for (const key of ["MediaPlayPause", "MediaPrevious", "MediaNext", "MediaTrackPrevious", "MediaTrackNext"]) {
    assert.equal(h.key({code: "Unidentified", key}).defaultPrevented, true);
  }
  assert.deepEqual(h.actions.slice(5), ["toggle", "prev", "next", "prev", "next"]);
});

test("media keys work in inputs, dialogs and IME without repeating or bypassing disabled buttons", () => {
  const h = setup();
  h.document.modal = true;
  for (const code of ["MediaPlayPause", "MediaTrackPrevious", "MediaTrackNext"]) {
    const event = {code, target: new h.Element(true), isComposing: true};
    h.key(event);
    assert.equal(h.key({...event, repeat: true}).defaultPrevented, true);
  }
  assert.deepEqual(h.actions, ["toggle", "prev", "next"]);
  h.controls.next.disabled = true;
  h.key({code: "MediaNext"});
  assert.equal(h.actions.length, 3);
  h.key({code: "MediaPlayPause", defaultPrevented: true});
  for (const modifier of ["ctrlKey", "altKey", "shiftKey", "metaKey"]) {
    assert.equal(h.key({code: "MediaPlayPause", [modifier]: true}).defaultPrevented, false);
  }
  assert.equal(h.key({code: "MediaStop"}).defaultPrevented, false);
  assert.equal(h.actions.length, 3);
});

test("media key registration conflicts are reported independently of existing chords", () => {
  const h = setup();
  h.shortcuts.message({hotkeys: {prev: true, next: true, toggle: true,
    mediaPrev: true, mediaNext: false, mediaToggle: true, volumeUp: true, volumeDown: true}});
  assert.match(h.controls.next.title, /Ctrl \+ Alt \+ →，全局；MediaNext，仅窗口内/);
  assert.equal(h.notices.length, 1);
  assert.match(h.notices[0], /MediaNext/);
  assert.doesNotMatch(h.notices[0], /Ctrl \+ Alt/);
  assert.match(h.controls.hotkeyAvailability.textContent, /MediaNext/);
  assert.doesNotMatch(h.controls.hotkeyAvailability.textContent, /Ctrl \+ Alt/);
  h.key({code: "MediaNext"});
  assert.deepEqual(h.actions, ["next"]);
});

test("fixed volume keys and standard DOM aliases send relative 5-percent steps and support holding", () => {
  const h = setup();
  h.document.modal = true;
  for (const code of ["VolumeUp", "AudioVolumeUp", "VolumeDown", "AudioVolumeDown"]) {
    assert.equal(h.key({code, target: new h.Element(true), isComposing: true}).defaultPrevented, true);
  }
  h.key({code: "AudioVolumeUp", repeat: true});
  h.key({code: "VolumeDown", repeat: true});
  h.key({code: "Unidentified", key: "AudioVolumeUp"});
  assert.deepEqual(h.volumeChanges, [5, 5, -5, -5, 5, -5, 5]);
  assert.deepEqual(h.actions, []);
  for (const modifier of ["ctrlKey", "altKey", "shiftKey", "metaKey"]) {
    assert.equal(h.key({code: "VolumeUp", [modifier]: true}).defaultPrevented, false);
  }
  assert.equal(h.key({code: "AudioVolumeMute"}).defaultPrevented, false);
  assert.equal(h.volumeChanges.length, 7);
});

test("native volume messages and independent volume conflicts keep built-ins outside combination shortcuts", () => {
  const h = setup();
  h.shortcuts.message({shortcut: "volumeUp"});
  h.shortcuts.message({shortcut: "volumeDown"});
  assert.deepEqual(h.volumeChanges, [5, -5]);
  h.shortcuts.message({hotkeys: {prev: true, next: true, toggle: true,
    mediaPrev: true, mediaNext: true, mediaToggle: true, volumeUp: false, volumeDown: true}});
  assert.match(h.controls.volume.title, /VolumeUp：仅窗口内；VolumeDown：全局/);
  assert.match(h.controls.hotkeyAvailability.textContent, /VolumeUp/);
  assert.doesNotMatch(h.controls.hotkeyAvailability.textContent, /Ctrl \+ Alt/);
  assert.equal(h.notices.length, 1);
  assert.doesNotMatch(h.notices[0], /Ctrl \+ Alt/);
});

test("custom combinations replace default fallback and update accessible button hints", () => {
  const h = setup();
  const config = [{modifiers: 6, key: 75}, {modifiers: 1, key: 119}, {modifiers: 0, key: 0}];
  h.shortcuts.message({hotkeyConfig: config, hotkeyCapture: false, hotkeys: {prev: true, next: true, toggle: false,
    mediaPrev: true, mediaNext: true, mediaToggle: true, volumeUp: true, volumeDown: true}});
  assert.equal(h.notices.length, 0);
  h.key({code: "KeyK", key: "k", ctrlKey: true, shiftKey: true});
  h.key({code: "F8", altKey: true});
  assert.equal(h.key({code: "ArrowLeft", ctrlKey: true, altKey: true}).defaultPrevented, false);
  h.key({code: "Space"}); h.key({code: "MediaPlayPause"});
  assert.deepEqual(h.actions, ["prev", "next", "toggle", "toggle"]);
  assert.match(h.controls.prev.title, /Ctrl \+ Shift \+ K/);
  assert.equal(h.controls.prev["aria-keyshortcuts"], "Control+Shift+K MediaTrackPrevious");
  assert.equal(h.controls.toggle["aria-keyshortcuts"], "Space MediaPlayPause");
  const copy = h.shortcuts.bindings(); copy[0].key = 65;
  assert.equal(h.shortcuts.bindings()[0].key, 75);
});

test("recording suspends combination fallback without disabling any fixed media key", () => {
  const h = setup();
  h.shortcuts.message({hotkeyConfig: [{modifiers: 3, key: 37}, {modifiers: 3, key: 39}, {modifiers: 3, key: 32}],
    hotkeyCapture: true, hotkeys: {prev: false, next: false, toggle: false}});
  assert.equal(h.key({code: "ArrowLeft", ctrlKey: true, altKey: true}).defaultPrevented, false);
  assert.equal(h.key({code: "Space"}).defaultPrevented, false);
  assert.equal(h.notices.length, 0);
  h.key({code: "MediaPlayPause"}); h.key({code: "AudioVolumeUp", repeat: true});
  assert.deepEqual(h.actions, ["toggle"]);
  assert.deepEqual(h.volumeChanges, [5]);
  h.shortcuts.message({hotkeyCapture: false});
  h.key({code: "ArrowLeft", ctrlKey: true, altKey: true});
  assert.deepEqual(h.actions, ["toggle", "prev"]);
});

test("editable binding validation rejects reserved, duplicate, media and malformed combinations", () => {
  const h = setup(), c = h.context;
  for (const b of [{modifiers: 0, key: 0}, {modifiers: 3, key: 32}, {modifiers: 6, key: 75}, {modifiers: 1, key: 119}])
    assert.equal(c.validHotkeyBinding(b), true);
  for (const b of [{modifiers: 0, key: 75}, {modifiers: 4, key: 75}, {modifiers: 8, key: 75},
    {modifiers: 1, key: 115}, {modifiers: 3, key: 46}, {modifiers: 3, key: 123},
    {modifiers: 2, key: 89}, {modifiers: 2, key: 90},
    {modifiers: 3, key: 179}, {modifiers: 3, key: 175}, {modifiers: 3, key: 174},
    {modifiers: 3, key: 0}, {modifiers: "3", key: 75}]) assert.equal(c.validHotkeyBinding(b), false);
  const defaults = h.shortcuts.bindings();
  assert.equal(c.validHotkeyBindings([defaults[0], defaults[0], defaults[2]]), false);
  assert.equal(c.validHotkeyBindings([{modifiers: 0, key: 0}, {modifiers: 0, key: 0}, {modifiers: 0, key: 0}]), true);
  assert.equal(c.validHotkeyBindings([]), false);
  assert.equal(c.hotkeyFromEvent({code: "AudioVolumeUp", ctrlKey: true}), null);
  h.shortcuts.message({hotkeyConfig: [{modifiers: 2, key: 179}, defaults[1], defaults[2]]});
  assert.equal(h.shortcuts.bindings()[0].key, 37);
});
