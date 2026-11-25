const test = require('node:test');
const assert = require('node:assert/strict');
const {createTrackOrder, moveVisibleTracks} = require('../ui/track-order');

test('filtered group move changes only visible slots and keeps group order', () => {
  const full = ['A', 'hidden1', 'B', 'C', 'hidden2', 'D'];
  const visible = ['A', 'B', 'C', 'D'], picked = new Set(['D', 'B']);
  assert.deepEqual(moveVisibleTracks(full, visible, picked, 'A', false), ['B', 'hidden1', 'D', 'A', 'hidden2', 'C']);
  assert.deepEqual(moveVisibleTracks(full, visible, picked, 'C', true), ['A', 'hidden1', 'C', 'B', 'hidden2', 'D']);
  assert.equal(moveVisibleTracks(full, visible, picked, 'D', false), null);
  assert.equal(moveVisibleTracks(full, visible, new Set(['A']), 'B', false), null);
});

test('page orders persist independently across reloads, imports and native index changes', () => {
  const map = new Map(), store = {getItem: k => map.get(k), setItem: (k,v) => map.set(k,v)};
  const initial = createTrackOrder(store);
  initial.save('library', ['B','C','A']); initial.save('my-list', ['A','B']);
  const next = createTrackOrder(store), tracks = ['C','B','D'].map(url => ({url}));
  assert.deepEqual(next.apply('library', [2,1,0], tracks), [2,1,0]);
  assert.deepEqual(next.apply('my-list', [0,1], tracks), [0,1]);
  assert.deepEqual(next.apply('likes', [1,0], tracks), [1,0]);
  assert.equal(next.has('likes'), false);
  store.setItem = () => { throw Error('quota'); };
  assert.throws(() => next.save('library', ['C','B','D']));
  assert.deepEqual(next.apply('library', [0,1,2], tracks), [2,1,0]);
});
