// Exercise the preset with the host's 32 ms meter cadence and a 16 ms render clock.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const document = { currentScript: {}, createElement: () => ({}) };
let now = 0, nextFrame, path;
const stage = { querySelector: () => ({ setAttribute: (_, value) => { path = value; } }) };
vm.runInNewContext(fs.readFileSync(require.resolve('../plugins/wavehalo/focus/preset.js'), 'utf8'), {
  document, performance: { now: () => now },
  requestAnimationFrame(fn) { nextFrame = fn; return 1; }, cancelAnimationFrame() { nextFrame = undefined; },
});
const preset = document.currentScript.mountFocus({ root: {firstElementChild: stage}, background: {append() {}},
  renderCover() {}, requestLevels() {}, reduceMotion: {matches: false} });
const rest = path;
preset.update({playing: true});
function frame() { now += 16; const fn = nextFrame; nextFrame = undefined; fn?.(now); }
function packet(levels) { preset.levels(levels); frame(); frame(); }
function bars() {
  return [...path.matchAll(/M([-\d.]+),([-\d.]+)L([-\d.]+),([-\d.]+)/g)].map(([,x,y,u,v]) => {
    assert(Math.hypot(u,v) < 250); // leave room for the glow at maximum drive
    return 160 - Math.hypot(x,y);
  });
}
for (const levels of [[90,0,0],[0,90,0],[0,0,90],[100,100,100]]) {
  preset.levels(levels); frame();
  const peaks = bars(), max = Math.max(...peaks), i = peaks.indexOf(max);
  assert.equal(peaks.length, 128);
  assert(max > 50 && peaks.filter(v => v > 25).length <= 6);
  assert(Math.max(peaks[(i+127)%128], peaks[(i+1)%128]) < max * .3);
  frame(); for (let n = 0; n < 6; n++) packet(levels);
  assert(bars()[i] < max * .4); // held tone, not another attack
  for (let n = 0; n < 48; n++) frame();
  assert.equal(path, rest); // missing packets release fully
}
for (let n = 0; n < 20; n++) packet([70,0,0]);
const steady = bars(); preset.levels([78,0,0]); frame();
assert(bars().some((v,i) => v - steady[i] > 22)); // modest kick above a loud backing
preset.update({playing: false}); assert.equal(path, rest); assert(!nextFrame);
console.log('PASS: sparse needles, first-frame attacks, fast release, small level rises and idle reset');
