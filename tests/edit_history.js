const test = require('node:test');
const assert = require('node:assert/strict');
const {createEditHistory, historyShortcut} = require('../ui/edit-history.js');
const clone = value => JSON.parse(JSON.stringify(value));
function fixture() {
  let state = {likes: ['a'], lists: [{id: 'one', name: 'One', urls: ['a', 'b']}]};
  const saved = new Map(), notices = [], calls = [];
  let failStorage = false, failNative = false, failRestore = false;
  const storage = {getItem: key => saved.get(key), setItem(key, value) { if (failStorage) throw Error('disk'); saved.set(key, value); }};
  const history = createEditHistory({storage, snapshot: () => clone(state),
    restore(next) { if (failRestore) throw Error('membership write failed'); state = next; },
    async applyNative(id, direction) { calls.push([id, direction]); if (failNative) throw Error('native write failed'); },
    notify: (...args) => notices.push(args)});
  return {history, saved, notices, calls, storage,
    get state() { return state; }, set state(value) { state = value; },
    failStorage(value) { failStorage = value; }, failNative(value) { failNative = value; }, failRestore(value) { failRestore = value; }};
}
test('batch deletion restores likes and memberships; redo removes them in one step', async () => {
  const f = fixture(), before = clone(f.state);
  f.state = {likes: [], lists: [{id: 'one', name: 'One', urls: []}]};
  f.history.record({label: '删除歌曲（2 首）', nativeId: '1', before});
  assert.equal(f.history.canUndo, true);
  assert.equal(await f.history.undo(), true);
  assert.deepEqual(f.state, before);
  assert.equal(f.history.canUndo, false);
  assert.equal(f.history.canRedo, true);
  assert.equal(await f.history.redo(), true);
  assert.deepEqual(f.state.likes, []);
  assert.deepEqual(f.state.lists[0].urls, []);
  assert.deepEqual(f.calls, [['1', 'undo'], ['1', 'redo']]);
});
test('import of existing songs can undo only new membership, preserving unrelated changes', async () => {
  const f = fixture(), before = clone(f.state);
  f.state.lists[0].urls.push('c');
  f.history.record({label: '导入歌单', before});
  f.state.likes.push('d'); f.state.lists[0].urls.push('d');
  assert.equal(await f.history.undo(), true);
  assert.deepEqual(f.state.likes, ['a', 'd']);
  assert.deepEqual(f.state.lists[0].urls, ['a', 'b', 'd']);
  await f.history.redo();
  assert.deepEqual(new Set(f.state.lists[0].urls), new Set(['a', 'b', 'c', 'd']));
  assert.equal(f.calls.length, 0);
});
test('new edit after undo clears redo; no-op duplicate import does not', async () => {
  const f = fixture(), before = clone(f.state);
  f.history.record({label: '导入', nativeId: '1', before});
  await f.history.undo();
  assert.equal(f.history.record({label: '重复导入', before: clone(f.state)}), false);
  assert.equal(f.history.canRedo, true);
  f.history.record({label: '另一次导入', nativeId: '2', before: clone(f.state)});
  assert.equal(f.history.canRedo, false);
});
test('native failure keeps history position; membership failure compensates native edit', async () => {
  const f = fixture();
  f.history.record({label: '删除', nativeId: '1', before: clone(f.state)});
  f.failNative(true);
  assert.equal(await f.history.undo(), false);
  assert.equal(f.history.canUndo, true);
  assert.equal(f.history.canRedo, false);
  f.failNative(false); f.failRestore(true); f.calls.length = 0;
  assert.equal(await f.history.undo(), false);
  assert.deepEqual(f.calls, [['1', 'undo'], ['1', 'redo']]);
  assert.equal(f.history.canUndo, true);
  f.failRestore(false);
  assert.equal(await f.history.undo(), true);
});
test('notices are persistent and optional, but never suppress errors or history', async () => {
  const f = fixture();
  assert.equal(f.history.setNotices(false), true);
  assert.equal(f.saved.get('nico.editNotices'), 'off');
  f.history.record({label: '导入', nativeId: '1', before: clone(f.state)});
  await f.history.undo();
  assert.equal(f.notices.length, 0);
  f.failNative(true); await f.history.redo();
  assert.equal(f.notices.length, 1);
  f.failStorage(true);
  assert.equal(f.history.setNotices(true), false);
  assert.equal(f.history.notices, false);
});
test('50-step bound, busy guard and repeated input cannot move the same edit twice', async () => {
  const f = fixture();
  for (let i = 0; i < 55; i++) f.history.record({label: '导入', nativeId: String(i), before: clone(f.state)});
  const pending = f.history.undo();
  assert.equal(f.history.busy, true);
  assert.equal(await f.history.undo(), false);
  await pending;
  let count = 1;
  while (await f.history.undo()) count++;
  assert.equal(count, 50);
});
test('Ctrl+Z/Y stays local; text inputs, dialogs, composition and other modifiers are untouched', () => {
  const event = {ctrlKey: true, key: 'z', target: {closest: () => null}};
  assert.equal(historyShortcut(event), 'undo');
  assert.equal(historyShortcut({...event, key: 'Y'}), 'redo');
  for (const extra of [{defaultPrevented: true}, {isComposing: true}, {ctrlKey: false}, {altKey: true},
    {shiftKey: true}, {metaKey: true}, {target: {isContentEditable: true}}, {target: {closest: () => ({})}}])
    assert.equal(historyShortcut({...event, ...extra}), null);
  assert.equal(historyShortcut(event, true), null);
});
