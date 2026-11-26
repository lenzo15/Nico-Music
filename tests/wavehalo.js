// Exercise the preset with the host's 32 ms meter cadence and a 16 ms render clock.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const document = { currentScript: {}, createElement: () => ({}) };
let now = 0, nextFrame, path, bassColor = false;
const ringStyle = new Map();
const ring = {querySelector: () => ({}), style: {setProperty: (k,v) => ringStyle.set(k,v)}, toggleAttribute(name, on) { assert.equal(name, 'data-bass'); bassColor = on; }};
const title = {};
const root = {querySelector: s => s === '.halo-ring' ? ring : s === '#haloTitle' ? title : {dataset:{}, setAttribute: (_, value) => { path = value; }}};
vm.runInNewContext(fs.readFileSync(require.resolve('../plugins/wavehalo/focus/preset.js'), 'utf8'), {
  document, performance: { now: () => now },
  requestAnimationFrame(fn) { nextFrame = fn; return 1; }, cancelAnimationFrame() { nextFrame = undefined; },
});
const mount = () => document.currentScript.mountFocus({root, background: {append() {}},
  renderCover() {}, requestLevels(on, options) { if (on) assert.equal(options.spectrum, true); }, reduceMotion: {matches: false} });
const preset = mount();
assert.doesNotMatch(root.innerHTML, /<(?:button|details|input)\b/i);
assert.equal(ringStyle.get('--halo-boost'), '1.5');
assert.equal(ringStyle.get('--halo-brighten-time'), '30ms');
const rest = path;
assert.match(root.innerHTML, /<h2 id="haloTitle"><\/h2>/);
const trackTitle = '夜曲 <img src=x onerror=alert(1)> & Vinyl';
preset.update({playing: true, title: trackTitle});
assert.equal(title.textContent, trackTitle);
assert.equal(title.title, trackTitle);
function frame() { now += 16; const fn = nextFrame; nextFrame = undefined; fn?.(now); }
function packet(levels, spectrum) { preset.levels(levels, spectrum); frame(); frame(); }
function bars() {
  return [...path.matchAll(/M([-\d.]+),([-\d.]+)L([-\d.]+),([-\d.]+)/g)].map(([,x,y,u,v]) => {
    assert(Math.hypot(u,v) < 250); // leave room for the glow at maximum drive
    return 160 - Math.hypot(x,y);
  });
}
const single = (index, strength=85) => Array.from({length:128}, (_, i) => i===index ? strength : 0);
for (const index of [2, 14, 37, 83, 119]) {
  const levels = [68,60,47], spectrum = single(index);
  for (let n=0; n<30; ++n) packet(levels, spectrum);
  const held = path, lengths = bars(), peak = Math.max(...lengths);
  assert.equal(lengths.length, 128); assert.equal(lengths.indexOf(peak), index);
  assert(peak > 35 && lengths.filter(v => v > peak * .5).length===1);
  for (let n=0; n<4; ++n) packet(levels, spectrum);
  assert.equal(path, held); // a sustained note doesn't decay like a transient
  for (let n=0; n<100; ++n) frame();
  assert.equal(path, rest);
}
packet([0,100,100]); assert(!bassColor);
packet([72,0,0]); assert(bassColor); packet([68,0,0]); assert(bassColor);
packet([60,0,0]); assert(!bassColor); packet([80,0,0]); assert(bassColor);
for (let n = 0; n < 12; n++) frame(); assert(!bassColor);
packet([80,0,0]); preset.update({playing:false}); assert(!bassColor); preset.update({playing:true});
function settle(levels, spectrum) { for (let n = 0; n < 45; n++) packet(levels, spectrum); }
settle([65,77,50], single(2,30));
const half = Math.max(...bars()); settle([65,77,50], single(2,45)); assert(Math.max(...bars()) > half * 1.4);
settle([65,77,50], Array(128).fill(55)); assert.equal(path, rest); // no common displacement
preset.update({playing: false, title: '下一首'});
assert.equal(title.textContent, '下一首'); assert.equal(title.title, '下一首');
preset.dispose(); assert(!nextFrame);
console.log('PASS: independent frequency needles, sustained absolute levels, idle reset, safe track caption and no tuning panel');
