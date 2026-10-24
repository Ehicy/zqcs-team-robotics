import test from 'node:test';
import assert from 'node:assert/strict';
import { OP, WHEELS, DEFAULT_CALIBRATION, encodeCommand, decodeStatus, validateCalibration, WriteQueue, AckRegistry, RemoteController } from '../web/protocol.js';

test('measured wheel directions retain the explicit calibration safety gate', () => {
  assert.deepEqual(WHEELS, ['M1 右前', 'M2 左前', 'M3 右后', 'M4 左后']);
  assert.deepEqual(DEFAULT_CALIBRATION.map(c => c.motorNegative), [false, true, false, true]);
  assert.deepEqual(DEFAULT_CALIBRATION.map(c => c.encoderNegative), [true, false, true, false]);
  assert.ok(DEFAULT_CALIBRATION.every(c => c.confirmed === false));
});

const flush = () => new Promise(resolve => setImmediate(resolve));
const header = { session: 0x12345678, sequence: 0x90abcdef };
function packet(kind) { const b = new Uint8Array(20); b[0] = 1; b[1] = kind; return b; }
test('v13 softened servo telemetry decodes alongside all earlier profiles',()=>{
  const b=packet(6),v=new DataView(b.buffer);
  v.setUint32(4,71,true);v.setUint16(8,1600,true);v.setUint16(18,500,true);b[2]=1;
  for(const [speed,acceleration] of [[100,200],[1000,4000],[1000,2500],[600,1200]]){
    v.setUint16(14,speed,true);v.setUint16(16,acceleration,true);
    const s=decodeStatus(b);assert.equal(s.speedUsS,speed);assert.equal(s.accelerationUsS2,acceleration);
  }
  v.setUint16(16,0,true);assert.throws(()=>decodeStatus(b),/舵机轨迹/);
});
function main({ session = 71, sequence = 0, armed = false, active = false, ready = true, fault = false, uart = true, calibratedMask = 15 } = {}) {
  const b = packet(0), v = new DataView(b.buffer);
  b[2] = Number(uart) | (Number(armed) << 1) | (Number(active) << 2) | (Number(ready) << 3) | (Number(fault) << 4) | 32;
  v.setUint32(4, session, true); v.setUint32(8, sequence, true); v.setUint16(16, 100, true); b[18] = calibratedMask;
  return decodeStatus(b);
}
function ack({ session = 71, sequence, wheel, result = 0 }) {
  const b = packet(3), v = new DataView(b.buffer);
  b[2] = wheel; b[3] = result; v.setUint32(4, session, true); v.setUint32(8, sequence, true);
  return decodeStatus(b);
}
function info(bytes) { const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); return { op: bytes[1], session: v.getUint32(4, true), sequence: v.getUint32(8, true), wheel: bytes[1] === OP.PID ? 253 : bytes[12] }; }
function fixture() {
  let now = 0, locks = 0;
  const writes = [], controller = new RemoteController({ now: () => now, locked: () => locks++ });
  const generation = controller.attach(async bytes => { writes.push(bytes.slice()); });
  controller.ingest(main(), generation);
  return { controller, generation, writes, advance: ms => { now += ms; }, locks: () => locks };
}
async function armFixture(f) {
  await flush(); await f.controller.arm();
  const arm = info(f.writes.at(-1));
  assert.equal(arm.op, OP.ARM);
  f.controller.ingest(main({ armed: true, sequence: arm.sequence }), f.generation);
  assert.equal(f.controller.state.armed, true);
}

test('codec writes exactly 20 LE bytes, zero reserved fields and distinct operation payloads', () => {
  for (const op of [OP.ARM, OP.STOP, OP.CLEAR_FAULT]) {
    const bytes = encodeCommand(op, header), v = new DataView(bytes.buffer);
    assert.equal(bytes.length, 20); assert.deepEqual([...bytes.slice(0, 4)], [1, op, 0, 0]);
    assert.equal(v.getUint32(4, true), header.session); assert.equal(v.getUint32(8, true), header.sequence);
    assert.deepEqual([...bytes.slice(12)], Array(8).fill(0));
  }
  const drive = encodeCommand(OP.DRIVE, { ...header, throttle: -1000, steer: 1000, limit: 300, ttl: 200 });
  const d = new DataView(drive.buffer);
  assert.equal(d.getInt16(12, true), -1000); assert.equal(d.getInt16(14, true), 1000);
  assert.equal(d.getUint16(16, true), 300); assert.equal(d.getUint16(18, true), 200);
  const mechanical = encodeCommand(OP.MECHANICAL, { ...header, ...DEFAULT_CALIBRATION[3], motorNegative: true, encoderNegative: true, confirmed: true });
  assert.equal(mechanical[12], 3); assert.equal(mechanical[13], 7);
  assert.equal(new DataView(mechanical.buffer).getUint16(14, true), 650);
  assert.equal(new DataView(mechanical.buffer).getUint32(16, true), 884);
  const tuning = encodeCommand(OP.TUNING, { ...header, ...DEFAULT_CALIBRATION[0] });
  assert.equal(tuning[13], 0); assert.equal(new DataView(tuning.buffer).getUint16(14, true), 1000);
  assert.equal(new DataView(tuning.buffer).getUint16(16, true), 2000);
  assert.equal(new DataView(tuning.buffer).getUint16(18, true), 300);
  assert.deepEqual([...encodeCommand(OP.LED, { ...header, on: 1 }).slice(12)], [1, 0, 0, 0, 0, 0, 0, 0]);
});

test('codec bounds reject overflow, NaN, nonintegers and silent truncation', () => {
  const drive = { ...header, throttle: 0, steer: 0, limit: 100, ttl: 200 };
  for (const [field, values] of Object.entries({ throttle: [-1001, 1001, NaN, 0.5], steer: [-1001, 1001], limit: [-1, 1001], ttl: [0, 49, 201], session: [0, -1, 0x100000000], sequence: [-1, 0x100000000] })) {
    for (const value of values) assert.throws(() => encodeCommand(OP.DRIVE, { ...drive, [field]: value }), RangeError);
  }
  for (const op of [0, 8, 1.5]) assert.throws(() => encodeCommand(op, header));
  for (const on of [-1, 2, true]) assert.throws(() => encodeCommand(OP.LED, { ...header, on }));
  for (const [field, values] of Object.entries({ wheel: [-1, 4], diameterTenthMm: [0, 5001], countsPerRev: [0, 1000001], kpMilli: [-1, 20001], kiMilli: [-1, 20001], maxMmS: [0, 1001] })) {
    for (const value of values) assert.throws(() => validateCalibration({ ...DEFAULT_CALIBRATION[0], [field]: value }));
  }
  assert.throws(() => validateCalibration({ ...DEFAULT_CALIBRATION[0], confirmed: 1 }));
  assert.throws(() => validateCalibration({ ...DEFAULT_CALIBRATION[0], kpMilli: 0, kiMilli: 0 }));
  assert.throws(() => encodeCommand(OP.TUNING, { ...header, ...DEFAULT_CALIBRATION[0], kpMilli: 0, kiMilli: 0 }));
  assert.doesNotThrow(() => encodeCommand(OP.DRIVE, { ...drive, sequence: 0, throttle: 1000, steer: -1000, limit: 0, ttl: 50 }));
  assert.doesNotThrow(() => validateCalibration({ ...DEFAULT_CALIBRATION[0], diameterTenthMm: 5000, countsPerRev: 1000000, kpMilli: 20000, kiMilli: 0, maxMmS: 1 }));
});

test('decodes every status kind, signed values, offset views and rejects malformed frames', () => {
  const b = packet(1), v = new DataView(b.buffer); b[2] = 15;
  [-2147483648, -1, 0, 2147483647].forEach((value, i) => v.setInt32(4 + 4 * i, value, true));
  const padded = new Uint8Array(26); padded.set(b, 3);
  assert.deepEqual(decodeStatus(new DataView(padded.buffer, 3, 20)).counts, [-2147483648, -1, 0, 2147483647]);
  const speeds = packet(2), sv = new DataView(speeds.buffer); speeds[2] = 15; speeds[3] = 7;
  [-300, 300, 0, -1].forEach((n, i) => { sv.setInt16(4 + i * 2, n, true); sv.setInt16(12 + i * 2, -n, true); });
  assert.deepEqual(decodeStatus(speeds).targets, [300, -300, 0, 1]);
  assert.equal(decodeStatus(speeds).faultCode, 7);
  assert.equal(main({ armed: true, active: true }).flags, 47);
  assert.equal(ack({ wheel: 255, sequence: 88 }).sourceSequence, 88);
  for (const value of [new Uint8Array(19), new Uint8Array(21), new Uint8Array(20), packet(4)]) assert.throws(() => decodeStatus(value));
  b[3] = 1; assert.throws(() => decodeStatus(b)); b[3] = 0; b[2] = 16; assert.throws(() => decodeStatus(b));
  const badAck = packet(3); badAck[2] = 4; assert.throws(() => decodeStatus(badAck)); badAck[2] = 0; badAck[19] = 1; assert.throws(() => decodeStatus(badAck));
  const badMain = packet(0); badMain[2] = 128; assert.throws(() => decodeStatus(badMain)); badMain[2] = 0; badMain[18] = 16; assert.throws(() => decodeStatus(badMain));
});

test('write queue keeps one in flight and STOP discards queued movement/actions', async () => {
  let now = 0; const sent = [], releases = [];
  const q = new WriteQueue({ now: () => now });
  q.setWriter(bytes => { sent.push(bytes[0]); return new Promise(resolve => releases.push(resolve)); });
  const first = q.offer(Uint8Array.of(1), { kind: 'drive' });
  const stale = q.offer(Uint8Array.of(2), { kind: 'drive' }).catch(error => error);
  const replaced = q.offer(Uint8Array.of(3), { kind: 'drive' }).catch(error => error);
  const action = q.offer(Uint8Array.of(4)).catch(error => error);
  const stop = q.offer(Uint8Array.of(5), { kind: 'stop' });
  assert.deepEqual(sent, [1]);
  releases.shift()(); await first; await flush(); assert.deepEqual(sent, [1, 5]);
  releases.shift()(); await stop;
  for (const result of await Promise.all([stale, replaced, action])) assert.ok(result instanceof Error);
  assert.equal(q.flight, null);
});

test('stale queue entries expire and old connection completion cannot drain new transport', async () => {
  let now = 0, release; const sent = [];
  const q = new WriteQueue({ now: () => now });
  q.setWriter(bytes => { sent.push(bytes[0]); return new Promise(resolve => { release = resolve; }); });
  const first = q.offer(Uint8Array.of(1)).catch(error => error);
  const stale = q.offer(Uint8Array.of(2), { kind: 'drive', expiresAt: 100 }).catch(error => error);
  now = 100; release(); await first; await flush(); assert.ok(await stale instanceof Error); assert.deepEqual(sent, [1]);
  const old = q.offer(Uint8Array.of(3)).catch(error => error); const oldRelease = release;
  q.setWriter(async bytes => sent.push(bytes[0]));
  await q.offer(Uint8Array.of(4)); oldRelease(); assert.ok(await old instanceof Error);
  assert.deepEqual(sent, [1, 3, 4]);
});

test('ordinary actions and latest DRIVE retain source sequence order', async () => {
  const sent = [], releases = [];
  const q = new WriteQueue({ now: () => 0 });
  q.setWriter(bytes => { sent.push(bytes[0]); return new Promise(resolve => releases.push(resolve)); });
  const first = q.offer(Uint8Array.of(1));
  const drive = q.offer(Uint8Array.of(2), { kind: 'drive' });
  const action = q.offer(Uint8Array.of(3));
  releases.shift()(); await first; await flush(); assert.deepEqual(sent, [1, 2]);
  releases.shift()(); await drive; await flush(); assert.deepEqual(sent, [1, 2, 3]);
  releases.shift()(); await action;
  const held = q.offer(Uint8Array.of(4));
  const discarded = q.offer(Uint8Array.of(5), { kind: 'drive' }).catch(error => error);
  const earlierAction = q.offer(Uint8Array.of(6));
  const latest = q.offer(Uint8Array.of(7), { kind: 'drive' });
  releases.shift()(); await held; await flush(); assert.deepEqual(sent, [1, 2, 3, 4, 6]);
  releases.shift()(); await earlierAction; await flush(); assert.deepEqual(sent, [1, 2, 3, 4, 6, 7]);
  releases.shift()(); await latest; assert.ok(await discarded instanceof Error);
});

test('ACK requires exact session, source sequence and wheel; failure and timeout reject', async () => {
  let now = 0; const a = new AckRegistry(() => now);
  const waiting = a.expect(71, 9, 2);
  for (const values of [{ session: 70, sequence: 9, wheel: 2 }, { sequence: 8, wheel: 2 }, { sequence: 9, wheel: 1 }]) assert.equal(a.accept(ack(values)), false);
  assert.equal(a.accept(ack({ sequence: 9, wheel: 2 })), true); await waiting;
  const rejected = a.expect(71, 10, 3).catch(error => error); a.accept(ack({ sequence: 10, wheel: 3, result: 4 })); assert.match((await rejected).message, /拒绝/);
  const expired = a.expect(71, 11, 0, 100).catch(error => error); now = 100; a.expire(); assert.match((await expired).message, /超时/);
});

test('controller cannot drive before ARM status, supports release-to-zero and STOP locks', async () => {
  const f = fixture(); await flush();
  assert.equal(f.controller.setIntent(1000, 0), false); f.controller.tick();
  assert.ok(f.writes.every(bytes => bytes[1] === OP.STOP));
  await f.controller.arm(); const command = info(f.writes.at(-1));
  assert.equal(f.controller.state.armed, false);
  f.controller.ingest(main({ armed: true, sequence: command.sequence - 1 }), f.generation);
  assert.equal(f.controller.state.armed, false);
  f.controller.ingest(main({ armed: true, sequence: command.sequence }), f.generation);
  f.controller.setIntent(1000, -1000); f.advance(50); f.controller.tick(); await flush();
  const drive = new DataView(f.writes.at(-1).buffer); assert.equal(drive.getInt16(12, true), 1000); assert.equal(drive.getInt16(14, true), -1000);
  f.controller.setIntent(0, 0); await flush(); assert.equal(new DataView(f.writes.at(-1).buffer).getInt16(12, true), 0);
  assert.equal(f.controller.state.armed, true);
  await f.controller.stop(); assert.equal(f.controller.state.armed, false); assert.equal(info(f.writes.at(-1)).op, OP.STOP);
});

test('event loop interruption at 150ms clears intent and never resumes on future ticks', async () => {
  const f = fixture(); await armFixture(f); f.controller.setIntent(1000, 0);
  f.advance(150); f.controller.tick(); await flush();
  assert.equal(f.controller.state.armed, false); assert.equal(f.controller.throttle, 0);
  assert.equal(info(f.writes.at(-1)).op, OP.STOP);
  const count = f.writes.length; f.advance(50); f.controller.tick(); await flush(); assert.equal(f.writes.length, count);
});

test('idle scheduling gaps do not send STOP or cancel an unrelated pending ACK', async () => {
  const f = fixture();
  const waiting = f.controller.acks.expect(71, 99, 0);
  f.advance(250); f.controller.tick(); await flush();
  assert.equal(f.writes.length, 0);
  assert.equal(f.controller.acks.pending.size, 1);
  f.controller.ingest(ack({ sequence: 99, wheel: 0 }), f.generation);
  await waiting;
});

test('STOP followed by a fresh disarmed session does not send STOP again', async () => {
  const f = fixture(); await flush(); assert.equal(f.writes.length, 0);
  await armFixture(f); f.controller.setIntent(1000, 0);
  await f.controller.stop(); const sent = f.writes.length;
  f.controller.ingest(main({ session: 72, armed: false, sequence: 0 }), f.generation);
  await flush(); assert.equal(f.writes.length, sent); assert.equal(f.controller.state.armed, false);
  assert.equal(f.controller.throttle, 0); assert.equal(f.controller.state.session, 72);
  f.advance(50); f.controller.tick(); await flush(); assert.equal(f.writes.length, sent);
});

test('unsolicited armed state sends one STOP even when faulted; repeated status does not loop', async () => {
  const f = fixture(); await flush();
  f.controller.ingest(main({ session: 72, armed: true, ready: false, fault: true }), f.generation);
  await flush(); assert.equal(f.writes.length, 1); assert.equal(info(f.writes[0]).op, OP.STOP);
  f.controller.ingest(main({ session: 72, armed: true, ready: false, fault: true }), f.generation);
  await flush(); assert.equal(f.writes.length, 1);
  f.controller.ingest(main({ session: 73 }), f.generation); await flush(); assert.equal(f.writes.length, 1);
});

test('disconnect, session change, invalid readiness and old notifications cannot restore motion', async () => {
  const f = fixture(); await armFixture(f); const oldGeneration = f.generation;
  f.controller.disconnect(); assert.equal(f.controller.ingest(main({ armed: true, sequence: 999 }), oldGeneration), false);
  const generation = f.controller.attach(async bytes => f.writes.push(bytes));
  f.controller.ingest(main(), generation); await flush(); assert.equal(f.controller.state.armed, false);
  await f.controller.arm(); f.controller.ingest(main({ armed: true, sequence: info(f.writes.at(-1)).sequence }), generation);
  assert.equal(f.controller.state.armed, true);
  f.controller.ingest(main({ session: 72, armed: true, sequence: 999 }), generation); await flush();
  assert.equal(f.controller.state.armed, false); assert.equal(info(f.writes.at(-1)).op, OP.STOP);
  f.controller.ingest(main({ session: 72, calibratedMask: 7 }), generation); assert.equal(f.controller.canArm(), false);
  await assert.rejects(f.controller.arm());
});

test('configuration sends unified PID then eight packets serially, waiting for each matching ACK', async () => {
  const f = fixture(); await flush();
  const values = DEFAULT_CALIBRATION.map(v => ({ ...v, confirmed: true }));
  const configuring = f.controller.configure(values); await flush();
  const initialCount = f.writes.length;
  for (let index = 0; index < 9; index++) {
    const item = info(f.writes.at(-1)); assert.equal(item.op, index === 0 ? OP.PID : (index-1) % 2 ? OP.TUNING : OP.MECHANICAL); assert.equal(item.wheel, index === 0 ? 253 : Math.floor((index-1) / 2));
    f.controller.ingest(ack({ sequence: item.sequence, wheel: (item.wheel + 1) % 4 }), f.generation);
    await flush(); assert.equal(f.writes.length, initialCount + index);
    f.controller.ingest(ack({ sequence: item.sequence, wheel: item.wheel }), f.generation); await flush();
  }
  await configuring; assert.equal(f.controller.state.configBusy, false); assert.equal(f.controller.state.armed, false);
  assert.equal(f.writes.filter(b => b[1] === OP.MECHANICAL).length, 4); assert.equal(f.writes.filter(b => b[1] === OP.TUNING).length, 4);
});

test('config reject/STOP/disconnect stop the sequence; malformed later wheel is rejected before any write', async () => {
  const f = fixture(); await flush(); const baseline = f.writes.length;
  const invalid = DEFAULT_CALIBRATION.map(v => ({ ...v })); invalid[3].countsPerRev = 0;
  await assert.rejects(f.controller.configure(invalid)); assert.equal(f.writes.length, baseline);
  const config = f.controller.configure(DEFAULT_CALIBRATION).catch(error => error); await flush();
  const item = info(f.writes.at(-1)); f.controller.ingest(ack({ sequence: item.sequence, wheel: item.wheel, result: 1 }), f.generation);
  assert.ok(await config instanceof Error); assert.equal(f.writes.length, baseline + 1);
  const config2 = f.controller.configure(DEFAULT_CALIBRATION).catch(error => error); await flush(); await f.controller.stop(); assert.ok(await config2 instanceof Error);
  const atStop = f.writes.length; await flush(); assert.equal(f.writes.length, atStop);
  const config3 = f.controller.configure(DEFAULT_CALIBRATION).catch(error => error); await flush(); f.controller.disconnect(); assert.ok(await config3 instanceof Error);
});

test('clear fault uses wheel 255 ACK and never arms; sequence wraps safely', async () => {
  const f = fixture(); await flush(); f.controller.sequence = 0xffffffff;
  const clearing = f.controller.clearFault(); await flush(); const item = info(f.writes.at(-1));
  assert.equal(item.op, OP.CLEAR_FAULT); assert.equal(item.sequence, 0);
  f.controller.ingest(ack({ sequence: item.sequence, wheel: 255 }), f.generation); await clearing;
  assert.equal(f.controller.state.armed, false);
});

function servoStatus({ session=71, pulses=[0,0], available=true }={}) {
  const b=packet(4), v=new DataView(b.buffer);
  b[2]=Number(!!pulses[0]) | Number(!!pulses[1])*2; b[3]=available ? 7 : 0;
  v.setUint32(4,session,true); pulses.forEach((p,i)=>v.setUint16(8+2*i,p,true));
  [1300,1700,25,100].forEach((p,i)=>v.setUint16(12+2*i,p,true));
  return decodeStatus(b);
}
test('servo codec bounds and capability telemetry', () => {
  const b=encodeCommand(OP.SERVO,{...header,channel:1,pulseUs:1500});
  assert.equal(b[12],1); assert.equal(new DataView(b.buffer).getUint16(14,true),1500);
  assert.throws(()=>encodeCommand(OP.SERVO,{...header,channel:3,pulseUs:1500}));
  for(const pulseUs of [1000,1500,2000]) assert.equal(encodeCommand(OP.SERVO,{...header,channel:2,pulseUs})[12],2);
  for(const pulseUs of [499,2501]) assert.throws(()=>encodeCommand(OP.SERVO,{...header,channel:2,pulseUs}));
  for(const p of [NaN,499,2501,-1,1500.5]) assert.throws(()=>encodeCommand(OP.SERVO,{...header,channel:0,pulseUs:p}));
  for(const p of [499,2501]) assert.throws(()=>encodeCommand(OP.SERVO,{...header,channel:1,pulseUs:p}));
  assert.deepEqual(servoStatus({pulses:[1500,1550]}).pulses,[1500,1550]);
  const extended=packet(5), v=new DataView(extended.buffer);
  extended[2]=7; extended[3]=15; v.setUint32(4,71,true);
  [1500,1550,2000,1000,2000,25].forEach((p,i)=>v.setUint16(8+2*i,p,true));
  assert.deepEqual(decodeStatus(extended).pulses,[1500,1550,2000]);
  assert.equal(decodeStatus(extended).quickProfile,false);
  extended[3]|=32; assert.equal(decodeStatus(extended).quickProfile,true);
  extended[3]|=64; assert.equal(decodeStatus(extended).encoderReset,true); extended[3]&=~64;
  extended[2]=3; assert.throws(()=>decodeStatus(extended));
});
function slewStatus(pulses=[0,0,0]) {
  const b=packet(5),v=new DataView(b.buffer);b[3]=143;
  v.setUint32(4,71,true);pulses.forEach((p,i)=>{if(p)b[2]|=1<<i;v.setUint16(8+2*i,p,true);});
  [1000,2000,25].forEach((p,i)=>v.setUint16(14+2*i,p,true));return decodeStatus(b);
}
function motionStatus({targets=[0,0,0],moving=0,error=0,session=71}={}) {
  const b=packet(6),v=new DataView(b.buffer);b[2]=moving;b[3]=error;v.setUint32(4,session,true);
  targets.forEach((p,i)=>v.setUint16(8+2*i,p,true));[100,200,500].forEach((p,i)=>v.setUint16(14+2*i,p,true));return decodeStatus(b);
}
test('servo rejects old firmware, missing reference, stale motion status, wrong session and armed chassis',async()=>{
  const f=fixture(),c=f.controller;
  c.ingest(servoStatus(),f.generation);assert.equal(c.servoAvailable(0),false);assert.ok(c.servoAvailable(0,true));
  await assert.rejects(c.servo(0,1500,null,1300));
  c.ingest(slewStatus(),f.generation);assert.equal(c.servoAvailable(0),false);
  assert.equal(c.ingest(motionStatus({session:99}),f.generation),false);
  c.ingest(motionStatus(),f.generation);assert.ok(c.servoAvailable(0));
  await assert.rejects(c.servo(0,1600));
  c.state.armed=true;await assert.rejects(c.servo(0,1600,null,1300));c.state.armed=false;
  f.advance(501);await assert.rejects(c.servo(0,1600,null,1300));assert.equal(f.writes.length,0);
});
test('target codec includes explicit reference and validates reserved/bounded fields',()=>{
  const b=encodeCommand(OP.SERVO_TARGET,{...header,channel:2,pulseUs:2000,referenceUs:1100}),v=new DataView(b.buffer);
  assert.equal(b[1],10);assert.equal(v.getUint16(14,true),2000);assert.equal(v.getUint16(16,true),1100);
  assert.equal(v.getUint16(18,true),0);
  assert.throws(()=>encodeCommand(OP.SERVO_TARGET,{...header,channel:0,pulseUs:1700,referenceUs:499}));
  assert.throws(()=>encodeCommand(OP.SERVO_TARGET,{...header,channel:2,pulseUs:0,referenceUs:1500}));
  assert.deepEqual(motionStatus({targets:[1500,0,2000],moving:4}).targets,[1500,0,2000]);
  const bad=packet(6);bad[3]=128;assert.throws(()=>decodeStatus(bad));
});
test('target ACK does not pretend PWM/position arrived; STOP and session clear pending work',async()=>{
  const f=fixture(),c=f.controller;c.ingest(slewStatus(),f.generation);c.ingest(motionStatus(),f.generation);
  const task=c.servo(0,1700,null,1300);await flush();
  assert.equal(c.state.configBusy,true);const sent=info(f.writes.at(-1));assert.equal(sent.op,OP.SERVO_TARGET);
  c.ingest(ack({sequence:sent.sequence,wheel:254}),f.generation);await task;
  assert.equal(c.servoStatus.pulses[0],0); /* ACK accepts target; it is not current PWM */
  c.ingest(slewStatus([1300,0,0]),f.generation);
  c.ingest(motionStatus({targets:[1700,0,0],moving:1}),f.generation);assert.equal(c.canArm(),false);
  await assert.rejects(c.servo(0,1500,null,1500));
  const pending=c.servo(0,1600);const rejected=assert.rejects(pending);await flush();await c.stop();await rejected;
  assert.equal(info(f.writes.at(-1)).op,OP.STOP);
  c.ingest(main({session:72}),f.generation);assert.equal(c.servoStatus,null);assert.equal(c.servoMotion,null);
  c.disconnect();assert.equal(c.servoAvailable(0),false);
});
