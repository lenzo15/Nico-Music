// Pure shortcut regression: node --test tests/shortcuts.js
"use strict";
const {test} = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../ui/shortcuts.js"), "utf8");

function setup() {
  const actions = [], notices = [], controls = {};
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
  for (const id of ["prev", "next", "toggle", "shortcutHint"]) {
    controls[id] = {disabled: false, click() { if (!this.disabled) actions.push(id); }};
  }
  const context = vm.createContext({document, Element, notify: s => notices.push(s)});
  vm.runInContext(source, context);
  const shortcuts = vm.runInContext("createPlaybackShortcuts(notify)", context);
  return {actions, notices, controls, document, Element, shortcuts,
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
  h.shortcuts.message({hotkeys: {prev: true, next: true, toggle: true}});
  assert.equal(h.notices.length, 0);
  assert.match(h.controls.prev.title, /全局/);
  h.shortcuts.message({hotkeys: {prev: true, next: false, toggle: true}});
  assert.match(h.controls.next.title, /仅窗口内/);
  assert.match(h.notices[0], /Ctrl \+ Alt \+ →/);
  assert.match(h.controls.shortcutHint.textContent, /注册失败/);
  h.key({code: "ArrowRight", ctrlKey: true, altKey: true});
  assert.deepEqual(h.actions, ["next"]);
});
