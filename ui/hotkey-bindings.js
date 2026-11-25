"use strict";
const defaultHotkeyBindings = () => [{modifiers: 3, key: 37}, {modifiers: 3, key: 39}, {modifiers: 3, key: 32}];
const hotkeySpecialKeys = {32: ["空格", "Space"], 33: ["PageUp", "PageUp"], 34: ["PageDown", "PageDown"],
  35: ["End", "End"], 36: ["Home", "Home"], 37: ["←", "ArrowLeft"], 38: ["↑", "ArrowUp"],
  39: ["→", "ArrowRight"], 40: ["↓", "ArrowDown"], 45: ["Insert", "Insert"], 46: ["Delete", "Delete"]};
function hotkeyName(key, aria = false) {
  if (hotkeySpecialKeys[key]) return hotkeySpecialKeys[key][aria ? 1 : 0];
  if (key >= 65 && key <= 90 || key >= 48 && key <= 57) return String.fromCharCode(key);
  if (key >= 112 && key <= 122) return "F" + (key - 111);
  return "";
}
function validHotkeyBinding(b) {
  if (!b || !Number.isInteger(b.modifiers) || !Number.isInteger(b.key)) return false;
  if (!b.key) return b.modifiers === 0;
  return b.modifiers >= 0 && b.modifiers <= 7 && !!(b.modifiers & 3) && !!hotkeyName(b.key) &&
    !(b.key === 115 && (b.modifiers & 1)) && !(b.key === 46 && (b.modifiers & 3) === 3);
}
function validHotkeyBindings(bindings) {
  return Array.isArray(bindings) && bindings.length === 3 && bindings.every(validHotkeyBinding) &&
    new Set(bindings.filter(b => b.key).map(b => `${b.modifiers}:${b.key}`)).size === bindings.filter(b => b.key).length;
}
function hotkeyText(binding, aria = false) {
  if (!binding.key) return aria ? "" : "未设置";
  const parts = [];
  if (binding.modifiers & 2) parts.push(aria ? "Control" : "Ctrl");
  if (binding.modifiers & 1) parts.push("Alt");
  if (binding.modifiers & 4) parts.push("Shift");
  parts.push(hotkeyName(binding.key, aria));
  return parts.join(aria ? "+" : " + ");
}
function hotkeyFromEvent(event) {
  if (event.metaKey) return null;
  let key = 0;
  if (/^[a-z0-9]$/i.test(event.key || "")) key = event.key.toUpperCase().charCodeAt(0);
  else if (/^Key[A-Z]$/.test(event.code || "")) key = event.code.charCodeAt(3);
  else if (/^Digit\d$/.test(event.code || "")) key = event.code.charCodeAt(5);
  else if (/^F([1-9]|1[01])$/.test(event.code || "")) key = 111 + Number(event.code.slice(1));
  else key = Number(Object.keys(hotkeySpecialKeys).find(k => hotkeySpecialKeys[k][1] === event.code) || 0);
  if (!key) return null;
  const binding = {modifiers: (event.ctrlKey ? 2 : 0) | (event.altKey ? 1 : 0) | (event.shiftKey ? 4 : 0), key};
  return validHotkeyBinding(binding) ? binding : null;
}
