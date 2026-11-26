// Exercise the preset with the host's 32 ms meter cadence and a 16 ms render clock.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const document = { currentScript: {}, createElement: () => ({}), addEventListener() {} };
const saved = new Map();
const localStorage = { getItem: k => saved.get(k) ?? null, setItem: (k,v) => saved.set(k,v) };
let now = 0, nextFrame, path, bassColor = false;
const ringStyle = new Map();
const ring = {style: {setProperty: (k,v) => ringStyle.set(k,v)}, toggleAttribute(name, on) { assert.equal(name, 'data-bass'); bassColor = on; }};
const sliders = ['bass','mid','high','colorAt','brightenMs','ring','width','inward','attack','release'].map(key => ({dataset:{key}, setAttribute(){}}));
const reset = {}, panel = {querySelectorAll: () => sliders, querySelector: s => s === 'button' ? reset : {}};
const root = {querySelector: s => s === '.halo-tuner' ? panel : s === '.halo-ring' ? ring : {setAttribute: (_, value) => { path = value; }}};
vm.runInNewContext(fs.readFileSync(require.resolve('../plugins/wavehalo/focus/preset.js'), 'utf8'), {
  document, localStorage, performance: { now: () => now },
  requestAnimationFrame(fn) { nextFrame = fn; return 1; }, cancelAnimationFrame() { nextFrame = undefined; },
});
const mount = () => document.currentScript.mountFocus({root, background: {append() {}},
  renderCover() {}, requestLevels(on, options) { if (on) assert.equal(options.spectrum, true); }, reduceMotion: {matches: false} });
const preset = mount();
function tune(key, valueAsNumber) { panel.oninput({target: {dataset: {key}, valueAsNumber}}); }
const brightenTime = () => ringStyle.get('--halo-brighten-time');
assert.equal(brightenTime(), '150ms'); tune('brightenMs', 0); assert.equal(brightenTime(), '0ms');
tune('brightenMs', 425); assert.equal(brightenTime(), '425ms');
const rest = path;
preset.update({playing: true});
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
packet([60,0,0]); assert(!bassColor); tune('colorAt', 55); assert(bassColor);
for (let n = 0; n < 12; n++) frame(); assert(!bassColor);
packet([80,0,0]); preset.update({playing:false}); assert(!bassColor); preset.update({playing:true});
tune('attack', 0); tune('release', 0); packet([65,77,50], single(2,60));
const half = Math.max(...bars()); packet([65,77,50], single(2,80)); assert(Math.max(...bars()) > half * 1.4);
const wide = bars()[3]; tune('width', 20); assert(bars()[3] < wide / 4);
packet([65,77,50], Array(128).fill(55)); assert.equal(path, rest); // no common displacement
tune('bass', 0); packet([90,0,0], single(2)); assert.equal(path, rest);
tune('bass', 80); tune('inward', 0); packet([90,0,0], single(2)); assert(Math.max(...bars()) < .01 && path !== rest);
packet([0,0,0], Array(128).fill(0)); assert.equal(path, rest);
preset.dispose(); assert(!nextFrame);
ringStyle.clear();
const again = mount(); assert.equal(sliders[0].value, 80); assert.equal(brightenTime(), '425ms');
reset.onclick(); assert.equal(sliders[0].value, 34); assert.equal(brightenTime(), '150ms'); again.dispose();
console.log('PASS: independent frequency needles, sustained absolute levels, live shape/response controls, persistence and reset');
