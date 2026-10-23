/* Offline wiring model + DOM regression. Does not connect to BLE, serial or motors. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { parseHTML } = require(process.env.WIRING_DOM_LIB || 'linkedom');
const file = path.resolve(__dirname, '../web/esp32c3-wiring.html');
const html = fs.readFileSync(file, 'utf8');
const seed = JSON.parse(html.match(/<script id="seed-data"[^>]*>([\s\S]*?)<\/script>/)[1]);
const modules = new Map(seed.modules.map(m => [m.id, m]));
const wires = new Map(seed.wires.map(w => [w.id, w]));
const pins = new Map(seed.modules.flatMap(m => m.pins.map(p => [`${m.id}.${p.id}`, p])));
assert.equal(seed.modules.length, 18);
assert.equal(seed.wires.length, 72);
assert.equal(modules.size, seed.modules.length);
assert.equal(wires.size, seed.wires.length);
assert.match(seed.buildId, /v8$/);
for (const wire of seed.wires) {
  assert(pins.has(wire.from), `${wire.id} source ${wire.from}`);
  assert(pins.has(wire.to), `${wire.id} destination ${wire.to}`);
  if (wire.kind === 'ground') {
    assert.equal(pins.get(wire.from).role, 'ground');
    assert.equal(pins.get(wire.to).role, 'ground');
  }
}
function connected(a, b) {
  return seed.wires.find(w => w.from === a && w.to === b || w.from === b && w.to === a);
}
for (const [a, b] of [
  ['battery.POS', 'fuse.IN'], ['fuse.OUT', 'relay.COM'], ['fuse.OUT', 'relay.VPLUS'],
  ['relay.NO', 'boost.IN_POS'], ['relay.NO', 'buck.IN_POS'], ['relay.VMINUS', 'battery.NEG'],
  ['boost.IN_NEG', 'battery.NEG'], ['buck.IN_NEG', 'battery.NEG'],
  ['boost.OUT_POS', 'tb2.VM'], ['boost.OUT_NEG', 'tb2.GND2'],
  ['buck.OUT_POS', 'esp.V5_L'], ['buck.OUT_NEG', 'esp.GND_L3'],
  ['buck.OUT_POS', 'stm.V5'], ['buck.OUT_NEG', 'stm.GND'],
  ['esp.IO18', 'stm.PB11'], ['stm.PB10', 'esp.IO19'],
]) assert(connected(a, b), `${a} -> ${b}`);
assert(!seed.wires.some(w => w.from === 'relay.NC' || w.to === 'relay.NC'));
assert.equal(seed.wires.filter(w => w.from === 'relay.NO' || w.to === 'relay.NO').length, 2);
assert(!connected('boost.OUT_POS', 'buck.IN_POS'));
const cfg = fs.readFileSync(path.resolve(__dirname, '../shared/board_link_config.h'), 'utf8');
for (const [index, gpio, macro] of [[1, 7, 'ARM'], [2, 6, 'FOREARM'], [3, 10, 'DOOR']]) {
  assert(connected(`esp.IO${String(gpio).padStart(2, '0')}`, `servo${index}.S`));
  assert(connected('buck.OUT_POS', `servo${index}.VPLUS`));
  assert(connected('buck.OUT_NEG', `servo${index}.GND`));
  assert.match(cfg, new RegExp(`#define LINK_ESP_${macro}_SERVO_GPIO\\s+${gpio}\\b`));
}
assert(seed.wires.filter(w => w.from.startsWith('tb1.') || w.to.startsWith('tb1.')).every(w => w.optional));
for (const a of seed.modules) {
  assert(a.x >= 0 && a.y >= 0 && a.x + a.w <= seed.board.width && a.y + a.h <= seed.board.height);
  for (const b of seed.modules) if (a.id < b.id) {
    assert(!(a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y), `${a.id}/${b.id} overlap`);
  }
}
function boot(savedBuild) {
  const { window, document } = parseHTML(html);
  Object.defineProperty(window.HTMLSelectElement.prototype, 'value', {
    configurable: true, get() { return (this.querySelector('option[selected]') || this.querySelector('option'))?.value || ''; },
    set(v) { for (const o of this.querySelectorAll('option')) o.toggleAttribute('selected', o.value === String(v)); },
  });
  window.Element.prototype.scrollIntoView = () => {};
  const storage = new Map(), key = 'crtc-chassis-wiring-editor-v1-' + document.documentElement.dataset.planId;
  if (savedBuild) storage.set(key, JSON.stringify({ buildId: savedBuild, model: { ...seed, modules: [] } }));
  const ctx = vm.createContext({ window, document, console, location: { hash: '' },
    localStorage: { getItem: k => storage.get(k) ?? null, setItem: (k, v) => storage.set(k, v) },
    setTimeout: () => 0, clearTimeout: () => {}, URL, Blob,
  });
  for (const script of document.querySelectorAll('script:not([type="application/json"])')) {
    if (script.id === 'seed-data') continue;
    vm.runInContext(script.textContent, ctx, { filename: file, timeout: 10000 });
  }
  const api = window.chassisWiringEditor;
  assert(api);
  assert.equal(api.validate(api.getModel()).length, 0);
  assert.equal(document.querySelectorAll('#relay-wire-rows tr').length, 9);
  assert.equal(document.querySelectorAll('#power-scene [data-power-wire]').length, 8);
  assert.equal(document.querySelectorAll('#servo-wire-rows tr').length, 3);
  assert.equal(document.querySelectorAll('#ground-steps tr').length, seed.wires.filter(w => w.kind === 'ground').length);
  assert.equal(document.querySelectorAll('#all-wire-steps tr').length, 72);
  assert.equal(document.querySelectorAll('#uart-scene [data-uart-wire]').length, 2);
  assert.match(document.getElementById('relay-guide').textContent, /Dashboard.*断联/);
  assert.match(document.getElementById('servo-wire-rows').textContent, /IO6/);
  assert.match(document.getElementById('servo-wire-rows').textContent, /IO10/);
  document.querySelector('#relay-wire-rows tr[data-power-wire="f003"]').click();
  assert.equal(api.getState().filter, 'power');
  assert.equal(api.getState().selected.id, 'f003');
  for (const view of ['front-up', 'front-down', 'back-up', 'back-down']) {
    const select = document.getElementById('esp-view'); select.value = view; select.onchange({ target: select });
    assert.equal(api.validate(api.getModel()).length, 0);
    assert.equal(document.querySelectorAll('#c3-pin-rows tr').length, 16);
    assert.equal(document.querySelectorAll('#servo-wire-rows tr').length, 3);
    if (view === 'front-up') {
      for (const [pin, row] of [['IO07', 10], ['IO06', 11], ['IO10', 12]]) {
        const p = api.getModel().modules.find(m => m.id === 'esp').pins.find(p => p.id === pin);
        assert.equal(p.side, 'left'); assert.equal(p.physicalRow, row);
      }
    }
  }
  if (savedBuild) assert(storage.has(key + '-backup'), 'stale V7 save backed up');
  console.log('PASS wiring DOM:', savedBuild ? 'V7 backup + V8 seed' : 'V8 seed', '(18 modules / 72 wires, power/relay, 4 orientations)');
}
boot(); boot('c3-servo270-adjacent-20261023-v7');
console.log('PASS C3 wiring: relay topology, PWM 7/6/10 vs source, grounds, NC unused, TB1 disabled, nonoverlapping layout');
