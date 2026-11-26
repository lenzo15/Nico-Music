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
  renderCover() {}, requestLevels() {}, reduceMotion: {matches: false} });
const preset = mount();
function tune(key, valueAsNumber) { panel.oninput({target: {dataset: {key}, valueAsNumber}}); }
const brightenTime = () => ringStyle.get('--halo-brighten-time');
assert.equal(brightenTime(), '150ms'); tune('brightenMs', 0); assert.equal(brightenTime(), '0ms');
tune('brightenMs', 425); assert.equal(brightenTime(), '425ms');
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
  for (let n = 0; n < 12; n++) packet(levels);
  const held = path;
  const active = bars();
  assert.equal(active.length, 128); assert(Math.abs(Math.min(...active)) < .01); // common radial displacement removed
  assert(Math.max(...active) > 12); // local peaks survive
  for (let n = 0; n < 12; n++) packet(levels);
  assert.equal(path, held); // constant absolute energy holds, without onset accents
  for (let n = 0; n < 96; n++) frame();
  assert.equal(path, rest); // missing packets release fully
}
packet([0,100,100]); assert(!bassColor);
packet([72,0,0]); assert(bassColor); packet([68,0,0]); assert(bassColor);
packet([60,0,0]); assert(!bassColor); tune('colorAt', 55); assert(bassColor);
for (let n = 0; n < 12; n++) frame(); assert(!bassColor);
packet([80,0,0]); preset.update({playing:false}); assert(!bassColor); preset.update({playing:true});
tune('attack', 0); tune('release', 0); packet([50,0,0]);
const half = Math.max(...bars()); packet([100,0,0]);
assert(Math.abs(Math.max(...bars()) / half - 2) < .01);
tune('width', 10);
const moving = () => bars().filter(v => v > 1).length;
const wideCoverage = moving(); tune('ring', 0);
assert(wideCoverage > moving() + 32); // broad response still reaches beyond the narrow peaks
tune('width', 100); const broad = bars().filter(v => v > 2).length; tune('width', 25);
assert(bars().filter(v => v > 2).length < broad);
tune('bass', 0); assert.equal(path, rest);
tune('bass', 80); tune('inward', 0); assert(Math.max(...bars()) < .01 && path !== rest);
packet([0,0,0]); assert.equal(path, rest);
preset.dispose(); assert(!nextFrame);
ringStyle.clear();
const again = mount(); assert.equal(sliders[0].value, 80); assert.equal(brightenTime(), '425ms');
reset.onclick(); assert.equal(sliders[0].value, 34); assert.equal(brightenTime(), '150ms'); again.dispose();
console.log('PASS: sustained absolute levels, linear gain, live shape/response controls, persistence and reset');
