"use strict";
function membershipDiff(before, after) {
  const a = new Set(before), b = new Set(after);
  return {added: after.filter(url => !a.has(url)), removed: before.filter(url => !b.has(url))};
}
function membershipPatch(before, after) {
  const previous = new Map(before.lists.map(list => [list.id, list.urls]));
  return {likes: membershipDiff(before.likes, after.likes), lists: after.lists.map(list =>
    ({id: list.id, ...membershipDiff(previous.get(list.id) || [], list.urls)}))};
}
function applyMembershipPatch(current, patch, undo) {
  function apply(urls, diff) {
    const remove = new Set(undo ? diff.added : diff.removed);
    return [...new Set([...urls.filter(url => !remove.has(url)), ...(undo ? diff.removed : diff.added)])];
  }
  return {likes: apply(current.likes, patch.likes), lists: current.lists.map(list => {
    const diff = patch.lists.find(item => item.id === list.id);
    return diff ? {...list, urls: apply(list.urls, diff)} : list;
  })};
}
function historyShortcut(event, dialogOpen = false) {
  if (event.defaultPrevented || event.isComposing || dialogOpen || !event.ctrlKey ||
      event.altKey || event.shiftKey || event.metaKey || event.target?.isContentEditable ||
      event.target?.closest?.("input,textarea,select,[contenteditable],[role=textbox]")) return null;
  const key = (event.key || "").toLowerCase();
  return key === "z" ? "undo" : key === "y" ? "redo" : null;
}
function createEditHistory({storage, snapshot, restore, applyNative, notify, changed = () => {}, blocked = () => false}) {
  const undo = [], redo = [];
  let busy = false, notices = true;
  try { notices = storage.getItem("nico.editNotices") !== "off"; } catch {}
  function announce(message, direction) {
    if (notices) notify(message, direction ? {label: direction === "undo" ? "撤销" : "重做", run: () => step(direction)} : null);
  }
  const api = {
    get busy() { return busy; },
    get notices() { return notices; },
    get canUndo() { return !busy && !blocked() && !!undo.length; },
    get canRedo() { return !busy && !blocked() && !!redo.length; },
    refresh() { changed(api); },
    setNotices(enabled) {
      try { storage.setItem("nico.editNotices", enabled ? "on" : "off"); }
      catch { notify("无法保存操作提示设置"); return false; }
      notices = enabled; changed(api); return true;
    },
    record({label, nativeId = "", createdList, before, after = snapshot()}) {
      const patch = membershipPatch(before, after);
      const different = [patch.likes, ...patch.lists].some(diff => diff.added.length || diff.removed.length);
      if (nativeId || different) {
        undo.push({label, nativeId, patch, created: after.lists.find(l => l.id === createdList)});
        if (undo.length > 50) undo.shift();
        redo.length = 0;
        changed(api);
        announce(`已${label}，可按 Ctrl+Z 撤销`, "undo");
        return true;
      }
      announce(`已${label}（没有新增变更）`);
      return false;
    },
    undo: () => step("undo"),
    redo: () => step("redo"),
  };
  async function step(direction) {
    const back = direction === "undo", from = back ? undo : redo, to = back ? redo : undo;
    if (busy || blocked() || !from.length) return false;
    const entry = from.at(-1);
    busy = true; changed(api);
    let nativeApplied = false;
    try {
      if (entry.nativeId) { await applyNative(entry.nativeId, direction); nativeApplied = true; }
      const next = applyMembershipPatch(snapshot(), entry.patch, back);
      let removed = false;
      if (entry.created) {
        const {id, name, urls} = entry.created, list = next.lists.find(l => l.id === id);
        // Never delete unrelated songs subsequently added to the imported playlist.
        if (back && list && list.name === name && !list.urls.length) {
          next.lists = next.lists.filter(l => l.id !== id); removed = true;
        } else if (!back && entry.removed && !list) {
          if (next.lists.some(l => l.name === name)) throw Error("已有同名歌单，无法重做导入");
          next.lists.push({id, name, urls: [...urls]});
        }
      }
      restore(next);
      if (back) entry.removed = removed;
      from.pop(); to.push(entry);
      announce(`已${back ? "撤销" : "重做"}：${entry.label}`, back ? "redo" : "undo");
      return true;
    } catch (error) {
      let rollbackError = "";
      if (nativeApplied) {
        try { await applyNative(entry.nativeId, back ? "redo" : "undo"); }
        catch { rollbackError = "；音乐库恢复失败，请重启并检查数据"; }
      }
      notify((error.message || "操作失败，撤销记录未移动") + rollbackError);
      return false;
    } finally { busy = false; changed(api); }
  }
  changed(api);
  return api;
}
if (typeof module !== "undefined") module.exports = {membershipDiff, membershipPatch, applyMembershipPatch, historyShortcut, createEditHistory};
