import test from 'node:test';
import assert from 'node:assert/strict';
import { DirectDrive } from '../web/direct-drive.js';
import { RemoteController, OP, PA_CHASSIS_CALIBRATION } from '../web/protocol.js';
const flush = () => new Promise(resolve => setImmediate(resolve));
const configuration = () => PA_CHASSIS_CALIBRATION.map(v => ({ ...v, confirmed: true }));
function fixture({ configured = false, deferredConfig = false } = {}) {
  let time = 0, foreground = true, resolveConfig, rejectConfig;
  const writes = [], errors = [];
  let direct;
  const controller = new RemoteController({ now: () => time, locked: () => direct?.cancel('external stop', false) });
  const generation = controller.attach(async bytes => { writes.push(bytes.slice()); });
  function status({ session = 7, armed = false, ready = configured, lastSequence = controller.sequence, uart = true } = {}) {
    controller.ingest({ kind: 0, session, uart, armed, ready, pidReady: ready, phase: armed ? 4 : 2,
      active: false, fault: false, faultCode: 0, reason: 0, calibratedMask: ready ? 15 : 0,
      lastSequence, left: 0, right: 0 }, generation);
  }
  status();
  controller.appliedCalibration = configured ? configuration() : null;
  let configureCount = 0;
  controller.configure = async values => {
    configureCount++; controller.assertStopped(); controller.state.configBusy = true;
    try {
      if (deferredConfig) await new Promise((resolve, reject) => { resolveConfig = resolve; rejectConfig = reject; });
      controller.appliedCalibration = values.map(v => ({ ...v })); status({ ready: true });
    } finally { controller.state.configBusy = false; }
  };
  direct = new DirectDrive({ controller, calibration: configuration, foreground: () => foreground, error: e => errors.push(e) });
  const advance = (ms = 25) => { time += ms; controller.mainStatusAt = time; direct.tick(); controller.tick(); };
  const drives = () => writes.filter(b => b[1] === OP.DRIVE).map(b => new DataView(b.buffer).getInt16(12, true));
  return { direct, controller, writes, errors, status, advance, drives,
    configureCount: () => configureCount, resolveConfig: () => resolveConfig(), rejectConfig: () => rejectConfig(new Error('rejected')),
    background: () => { foreground = false; } };
}
test('connecting and idle ticks never configure, ARM or move', async () => {
  const h = fixture(); h.advance(); await flush();
  assert.equal(h.configureCount(), 0); assert.deepEqual(h.writes, []);
});
test('fresh direction applies current values, waits for ARM status, then drives only held latest intent', async () => {
  const h = fixture(); h.direct.request(1000, 0); await flush();
  assert.equal(h.configureCount(), 1); assert.equal(h.controller.state.armPending, true);
  assert.deepEqual(h.drives(), []);
  h.direct.request(-1000, 0); h.status({ ready: true, armed: true }); h.advance(); await flush();
  assert.equal(h.direct.phase, 'DRIVING'); assert.deepEqual(h.drives(), [-1000]);
  h.direct.request(0, 0); await flush();
  assert.equal(h.controller.state.armed, false); assert.equal(h.writes.at(-1)[1], OP.STOP);
  const n = h.drives().length; h.advance(); await flush(); assert.equal(h.drives().length, n);
});
test('release while configuration is pending invalidates completion and sends no ARM', async () => {
  const h = fixture({ deferredConfig: true }); h.direct.request(1000, 0);
  h.direct.request(0, 0); h.resolveConfig(); await flush(); h.advance(); await flush();
  assert.equal(h.direct.job, null); assert.equal(h.writes.some(b => b[1] === OP.ARM), false);
  assert.deepEqual(h.drives(), []);
});
test('STOP during ARM and a late armed status cannot start motion', async () => {
  const h = fixture({ configured: true }); h.direct.request(1000, 0); await flush();
  const armSequence = h.controller.armSequence;
  await h.controller.stop('STOP');
  h.status({ ready: true, armed: true, lastSequence: armSequence }); h.advance(); await flush();
  assert.equal(h.direct.job, null); assert.equal(h.controller.state.armed, false); assert.deepEqual(h.drives(), []);
});
test('session change, disconnect and blur cancel preparation without restart', async () => {
  for (const reason of ['session', 'disconnect', 'blur']) {
    const h = fixture({ configured: true }); h.direct.request(1000, 0); await flush();
    if (reason === 'session') h.status({ session: 8, ready: true });
    if (reason === 'disconnect') h.controller.disconnect();
    if (reason === 'blur') h.background();
    h.advance(); await flush(); assert.equal(h.direct.job, null, reason); assert.deepEqual(h.drives(), [], reason);
  }
});
test('a scheduler stall cancels a held command and never resumes', async () => {
  const h = fixture({ configured: true }); h.direct.request(1000, 0); await flush();
  h.status({ armed: true, ready: true }); h.advance(150); await flush();
  assert.equal(h.direct.job, null); assert(h.drives().every(v => v === 0));
});
test('failed configuration stops and leaves no automatic retry', async () => {
  const h = fixture({ deferredConfig: true }); h.direct.request(1000, 0); h.rejectConfig(); await flush();
  h.advance(); await flush(); assert.equal(h.configureCount(), 1); assert.equal(h.errors.length, 1); assert.deepEqual(h.drives(), []);
});
test('fresh presses while a servo is moving are refused without changing servo ownership', () => {
  const h = fixture({ configured: true }), owner = {}; h.controller.servoOwner = owner;
  assert.equal(h.direct.request(1000, 0), false); assert.equal(h.direct.job, null); assert.equal(h.controller.servoOwner, owner);
});
