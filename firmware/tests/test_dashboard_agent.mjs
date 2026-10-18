import test from 'node:test';
import assert from 'node:assert/strict';
import { RemoteController, DEFAULT_CALIBRATION, OP } from '../web/protocol.js';
import { createDashboardAgent, registerAgentTools } from '../web/dashboard-agent.js';

const flush = () => new Promise(resolve => setImmediate(resolve));
function harness({ connected = true } = {}) {
  let time = 100, active = true;
  const sent = [], permissions = { write: false, motion: false, servo: false };
  const controller = new RemoteController({ now: () => time });
  const speed = { validMask: 15, measured: [0,0,0,0], targets: [0,0,0,0] };
  const feedback = { encoder: { validMask: 15, counts: [0,0,0,0] }, encoderAt: 100, speed, speedAt: 100 };
  const values = DEFAULT_CALIBRATION.map(v => ({ ...v }));
  if (connected) {
    controller.attach(async bytes => sent.push(bytes.slice()));
    Object.assign(controller.state, { session: 9, uart: true, ready: true, pidReady: true, calibratedMask: 15 });
    controller.mainStatusAt = time;
  }
  const agent = createDashboardAgent({ controller, telemetry: () => feedback, calibration: () => values,
    permissions: () => permissions, foreground: () => active, now: () => time });
  return { agent, controller, sent, permissions, feedback, values,
    time: v => { time = v; }, foreground: v => { active = v; },
    armed: () => Object.assign(controller.state, { armed: true, remoteArmed: true }),
  };
}

test('status is read-only, detached and reports disconnected without pretending UART hardware failed', async () => {
  const h = harness({ connected: false });
  const r = await h.agent.execute({ command: 'status' });
  assert.equal(r.ok, true); assert.equal(r.status.canArm, false);
  assert.deepEqual(r.status.blockers.map(v => v.code), ['BLE_DISCONNECTED']);
  assert.equal(r.status.feedback.mainAgeMs, null);
  r.status.state.connected = true; r.status.calibrationForm[0].confirmed = true;
  assert.equal(h.controller.state.connected, false); assert.equal(h.values[0].confirmed, false);
  assert.equal(h.sent.length, 0);
});

test('diagnostics distinguish calibration, UART, fault and stale main telemetry', () => {
  const h = harness(); Object.assign(h.controller.state, { uart: false, ready: false, calibratedMask: 3, fault: true, faultCode: 2 });
  h.time(700);
  const s = h.agent.snapshot();
  for (const c of ['UART_OFFLINE','NOT_READY','CALIBRATION_REQUIRED','FAULT','STATUS_STALE']) assert(s.blockers.some(b => b.code === c));
  assert(s.blockers.find(b => b.code === 'CALIBRATION_REQUIRED').message.includes('M3 右后'));
});

test('writes default denied, STOP remains available and unknown fields never act', async () => {
  const h = harness();
  assert.equal((await h.agent.execute({ command: 'led', on: true })).error.code, 'WRITE_DISABLED');
  assert.equal((await h.agent.execute({ command: 'drive', throttle: 1, surprise: true })).error.code, 'INVALID_ARGUMENT');
  assert.equal((await h.agent.execute({ command: '__proto__' })).error.code, 'UNKNOWN_COMMAND');
  assert.equal(h.sent.length, 0);
  assert.equal((await h.agent.execute({ command: 'stop' })).ok, true);
  assert.equal(h.sent.at(-1)[1], OP.STOP);
});

test('LED uses existing queue and reports transport submission, no motion', async () => {
  const h = harness(); h.permissions.write = true;
  const r = await h.agent.execute({ command: 'led', on: true });
  assert.equal(r.ok, true); assert.equal(r.status.busy, false);
  assert.equal(h.sent.length, 1); assert.equal(h.sent[0][1], OP.LED);
  assert.equal(h.controller.state.armed, false);
  assert.equal((await h.agent.execute({ command: 'led', on: 1 })).error.code, 'INVALID_ARGUMENT');
});

test('configure never accepts supplied values or marks unmeasured form as confirmed', async () => {
  const h = harness(); h.permissions.write = true;
  assert.equal((await h.agent.execute({ command: 'configure', values: [] })).error.code, 'INVALID_ARGUMENT');
  assert.equal((await h.agent.execute({ command: 'configure' })).error.code, 'CALIBRATION_UNCONFIRMED');
  assert(h.values.every(v => !v.confirmed)); assert.equal(h.sent.length, 0);
  h.values.forEach(v => { v.confirmed = true; });
  let calls = 0;
  h.controller.configure = async values => { calls++; assert.deepEqual(values,h.values); };
  assert.equal((await h.agent.execute({ command: 'configure' })).ok, true); assert.equal(calls, 1);
});

test('configuration refuses stale, incomplete or moving speed feedback', async () => {
  for (const mode of ['stale','invalid','measured','target']) {
    const h = harness(); h.permissions.write = true; h.values.forEach(v => { v.confirmed = true; });
    if (mode === 'stale') h.time(300);
    if (mode === 'invalid') h.feedback.speed.validMask = 7;
    if (mode === 'measured') h.feedback.speed.measured[2] = 1;
    if (mode === 'target') h.feedback.speed.targets[3] = 1;
    assert.equal((await h.agent.execute({ command: 'configure' })).ok, false, mode);
    assert.equal(h.sent.length, 0);
  }
});

test('ARM needs separate motion permission, stopped feedback and existing readiness', async () => {
  const h = harness(); h.permissions.write = true;
  assert.equal((await h.agent.execute({ command: 'arm' })).error.code, 'MOTION_DISABLED');
  h.permissions.motion = true; h.controller.state.calibratedMask = 0;
  assert.equal((await h.agent.execute({ command: 'arm' })).ok, false);
  assert.equal(h.sent.length, 0);
  h.controller.state.calibratedMask = 15;
  const r = await h.agent.execute({ command: 'arm' });
  assert.equal(r.ok, true); assert.equal(r.status.state.armPending, true);
  assert.equal(r.status.state.armed, false); assert.equal(h.sent.at(-1)[1], OP.ARM);
  assert(h.sent.every(b => b[1] !== OP.DRIVE));
});

test('finite DRIVE never auto-arms, rejects invalid values or renewals', async () => {
  const h = harness(); Object.assign(h.permissions, { write: true, motion: true });
  const drive = { command: 'drive', throttle: 1000, steer: 0, limitMmS: 80, durationMs: 100 };
  assert.equal((await h.agent.execute(drive)).error.code, 'NOT_ARMED');
  h.armed();
  for (const patch of [{limitMmS:101},{durationMs:1001},{durationMs:NaN},{throttle:1001},{steer:0.5}])
    assert.equal((await h.agent.execute({...drive,...patch})).error.code,'INVALID_ARGUMENT');
  assert.equal((await h.agent.execute(drive)).ok, true);
  assert.equal((await h.agent.execute(drive)).error.code, 'BUSY');
  h.agent.tick(); h.controller.tick(); await flush();
  assert.equal(h.sent.at(-1)[1], OP.DRIVE);
  h.time(200); h.agent.tick(); h.controller.tick(); await flush();
  assert.equal(h.controller.state.armed, false); assert.equal(h.agent.isDriving(), false);
  assert.equal(h.sent.at(-1)[1], OP.STOP);
});

test('foreground, permission, session, input ownership and disconnect cancel lease without resuming', async () => {
  for (const reason of ['blur','permission','session','generation','input','limit','disconnect']) {
    const h = harness(); Object.assign(h.permissions, { write: true, motion: true }); h.armed();
    await h.agent.execute({command:'drive',throttle:1000,steer:0,limitMmS:80,durationMs:500});
    if(reason==='blur') h.foreground(false);
    if(reason==='permission') h.permissions.motion=false;
    if(reason==='session') h.controller.state.session++;
    if(reason==='generation') h.controller.generation++;
    if(reason==='input') h.controller.throttle=0;
    if(reason==='limit') h.controller.state.limit=200;
    if(reason==='disconnect') h.controller.state.connected=false;
    h.agent.tick(); await flush();
    assert.equal(h.agent.isDriving(),false,reason); assert.equal(h.controller.state.armed,false,reason);
  }
});

test('STOP cancels outstanding commands; late completion cannot report success', async () => {
  const h = harness(); h.permissions.write=true;
  let finish; h.controller.led=() => new Promise(resolve => { finish=resolve; });
  const pending=h.agent.execute({command:'led',on:true});
  assert.equal((await h.agent.execute({command:'led',on:false})).error.code,'BUSY');
  assert.equal((await h.agent.execute({command:'stop'})).ok,true);
  finish(); assert.equal((await pending).error.code,'CANCELLED');
});

test('servo has no setup checkbox gate; firmware and explicit reference still apply',async()=>{
  const h=harness();h.permissions.write=true;
  const r=await h.agent.execute({command:'servo',channel:0,pulseUs:1700,referenceUs:1300});
  assert.equal(r.ok,false);assert.notEqual(r.error.code,'SERVO_UNCONFIRMED');assert.equal(h.sent.length,0);
  let args;h.controller.servo=async(...v)=>{args=v;};
  assert.equal((await h.agent.execute({command:'servo',channel:0,pulseUs:1700,referenceUs:1300})).ok,true);
  assert.deepEqual(args,[0,1700,null,1300]);
  assert.equal((await h.agent.execute({command:'servo',channel:0,pulseUs:0,referenceUs:1300})).error.code,'INVALID_ARGUMENT');
});

test('WebMCP is optional and registered tools use the same facade', async () => {
  const h=harness(), tools=[];
  assert.equal(registerAgentTools(undefined,h.agent),false);
  assert.equal(registerAgentTools({registerTool:t=>tools.push(t)},h.agent),true);
  assert.deepEqual(tools.map(t=>t.name),['crtc_dashboard_status','crtc_dashboard_command']);
  assert.equal(JSON.parse((await tools[0].execute({})).content[0].text).ok,true);
  const r=JSON.parse((await tools[1].execute({command:'led',on:true})).content[0].text);
  assert.equal(r.error.code,'WRITE_DISABLED'); assert.equal(h.sent.length,0);
});
