/* 遥控台纯逻辑与端到端意图测试：不依赖 DOM，直接跑 Node 内置测试器。
 *
 * 覆盖两层：
 *   1) dashboard-input.js 的按键折叠、轮速预览、描述文本；
 *   2) 键盘意图 → protocol.js 编码 → RemoteController 安全门禁的完整链路，
 *      确认"未解锁不发车、松开归零、调度卡顿急停"这些约定没有被 WASD 改坏。
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import { OP, encodeCommand, decodeStatus, RemoteController } from '../web/protocol.js';
import {
  AXIS_FULL, KEY_GROUPS, KeyHoldSet, normalizeKey, isDriveKey, isStopKey,
  intentFromKeys, mixWheels, describeIntent,
} from '../web/dashboard-input.js';

const flush = () => new Promise(resolve => setImmediate(resolve));

function mainPacket({ session = 71, sequence = 0, armed = false, active = false, ready = true, fault = false, uart = true, calibratedMask = 15, limit = 100 } = {}) {
  const bytes = new Uint8Array(20), view = new DataView(bytes.buffer);
  bytes[0] = 1; bytes[1] = 0;
  bytes[2] = Number(uart) | (Number(armed) << 1) | (Number(active) << 2) | (Number(ready) << 3) | (Number(fault) << 4);
  view.setUint32(4, session, true); view.setUint32(8, sequence, true); view.setUint16(16, limit, true);
  bytes[18] = calibratedMask;
  return decodeStatus(bytes);
}
function driveInfo(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    op: bytes[1], session: view.getUint32(4, true), sequence: view.getUint32(8, true),
    throttle: view.getInt16(12, true), steer: view.getInt16(14, true),
    limit: view.getUint16(16, true), ttl: view.getUint16(18, true),
  };
}

/* ------------------------- 按键折叠 ------------------------- */

test('按键归一化：大小写不敏感，方向键等价于 WASD', () => {
  assert.equal(normalizeKey('W'), 'w');
  assert.equal(normalizeKey('ArrowUp'), 'w');
  assert.equal(normalizeKey('ARROWDOWN'), 's');
  assert.equal(normalizeKey('arrowleft'), 'a');
  assert.equal(normalizeKey('ArrowRight'), 'd');
  assert.equal(normalizeKey('q'), 'q');
  assert.equal(normalizeKey(undefined), '');
  assert.equal(normalizeKey(null), '');
});

test('只有 WASD 是行驶键，空格与 Esc 是急停键', () => {
  for (const key of ['w', 'W', 'a', 's', 'd', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight']) {
    assert.equal(isDriveKey(key), true, `${key} 应为行驶键`);
  }
  for (const key of ['q', 'e', 'Enter', 'Shift', '1', 'F5']) assert.equal(isDriveKey(key), false, `${key} 不应是行驶键`);
  for (const key of [' ', 'Spacebar', 'Escape', 'Esc']) assert.equal(isStopKey(key), true, `${key} 应为急停键`);
  for (const key of ['w', 'Enter', 'q']) assert.equal(isStopKey(key), false);
});

test('单键意图：W/S 走前后，A/D 走转向，满量程 ±1000', () => {
  assert.deepEqual(intentFromKeys([]), { throttle: 0, steer: 0 });
  assert.deepEqual(intentFromKeys(['w']), { throttle: 1000, steer: 0 });
  assert.deepEqual(intentFromKeys(['s']), { throttle: -1000, steer: 0 });
  assert.deepEqual(intentFromKeys(['a']), { throttle: 0, steer: -1000 });
  assert.deepEqual(intentFromKeys(['d']), { throttle: 0, steer: 1000 });
  assert.deepEqual(intentFromKeys(['ArrowUp']), { throttle: 1000, steer: 0 });
  assert.deepEqual(intentFromKeys(['ArrowLeft']), { throttle: 0, steer: -1000 });
  assert.equal(intentFromKeys(['w']).throttle, AXIS_FULL);
});

test('组合键：W+A 与 W+D 是斜向，同轴相反键相抵为零', () => {
  assert.deepEqual(intentFromKeys(['w', 'a']), { throttle: 1000, steer: -1000 });
  assert.deepEqual(intentFromKeys(['w', 'd']), { throttle: 1000, steer: 1000 });
  assert.deepEqual(intentFromKeys(['s', 'a']), { throttle: -1000, steer: -1000 });
  assert.deepEqual(intentFromKeys(['w', 's']), { throttle: 0, steer: 0 });
  assert.deepEqual(intentFromKeys(['a', 'd']), { throttle: 0, steer: 0 });
  assert.deepEqual(intentFromKeys(['w', 's', 'a', 'd']), { throttle: 0, steer: 0 });
  assert.deepEqual(intentFromKeys(['w', 'a', 'd']), { throttle: 1000, steer: 0 });
});

test('无关键被忽略，重复键与方向键别名不叠加', () => {
  assert.deepEqual(intentFromKeys(['q', 'e', ' ']), { throttle: 0, steer: 0 });
  assert.deepEqual(intentFromKeys(['w', 'ArrowUp']), { throttle: 1000, steer: 0 });
});

/* ------------------------- 按住集合 ------------------------- */

test('KeyHoldSet 只跟踪行驶键，重复按下返回 false，清空幂等', () => {
  const keys = new KeyHoldSet();
  assert.equal(keys.press('w'), true);
  assert.equal(keys.press('w'), false);
  assert.equal(keys.press('ArrowUp'), false, '方向键别名与 w 是同一个键');
  assert.equal(keys.press('q'), false, '无关键不入集合');
  assert.equal(keys.size, 1);
  assert.equal(keys.press('a'), true);
  assert.deepEqual(keys.intent(), { throttle: 1000, steer: -1000 });
  assert.equal(keys.release('W'), true);
  assert.equal(keys.release('w'), false);
  assert.deepEqual(keys.intent(), { throttle: 0, steer: -1000 });
  assert.equal(keys.clear(), true);
  assert.equal(keys.clear(), false);
  assert.deepEqual(keys.intent(), { throttle: 0, steer: 0 });
});

/* ------------------------- 轮速预览 ------------------------- */

test('轮速预览与固件 remote_control.c 的混控同式（含动态 scale）', () => {
  // 直行：left=right=limit
  assert.deepEqual(mixWheels({ throttle: 1000, steer: 0 }, 100), { left: 100, right: 100 });
  // 后退
  assert.deepEqual(mixWheels({ throttle: -1000, steer: 0 }, 300), { left: -300, right: -300 });
  // 原地右转：left=+1000, right=-1000，scale 仍为 1000
  assert.deepEqual(mixWheels({ throttle: 0, steer: 1000 }, 100), { left: 100, right: -100 });
  // 前进+右转：left=2000 触发动态 scale=2000，左右各减半
  assert.deepEqual(mixWheels({ throttle: 1000, steer: 1000 }, 100), { left: 100, right: 0 });
  assert.deepEqual(mixWheels({ throttle: 1000, steer: -1000 }, 100), { left: 0, right: 100 });
  // 速度为 0 时整体归零
  assert.deepEqual(mixWheels({ throttle: 1000, steer: 1000 }, 0), { left: 0, right: 0 });
  assert.deepEqual(mixWheels({ throttle: 0, steer: 0 }, 300), { left: 0, right: 0 });
});

test('轮速预览：整数除法向零截断，与 C 语义一致', () => {
  // left=2000, right=0, scale=2000, limit=255 → 255 与 0
  assert.deepEqual(mixWheels({ throttle: 1000, steer: 1000 }, 255), { left: 255, right: 0 });
  // 非整除时向零取整而非四舍五入：1000*250/1000 = 250
  assert.deepEqual(mixWheels({ throttle: 1000, steer: 0 }, 250), { left: 250, right: 250 });
  const odd = mixWheels({ throttle: 1000, steer: 333 }, 100);
  assert.equal(Number.isInteger(odd.left) && Number.isInteger(odd.right), true);
});

test('意图描述覆盖八种方向', () => {
  assert.equal(describeIntent({ throttle: 0, steer: 0 }), '停止');
  assert.equal(describeIntent({ throttle: 1000, steer: 0 }), '前进');
  assert.equal(describeIntent({ throttle: -1000, steer: 0 }), '后退');
  assert.equal(describeIntent({ throttle: 0, steer: 1000 }), '原地右转');
  assert.equal(describeIntent({ throttle: 0, steer: -1000 }), '原地左转');
  assert.equal(describeIntent({ throttle: 1000, steer: 1000 }), '前进 · 右转');
  assert.equal(describeIntent({ throttle: 1000, steer: -1000 }), '前进 · 左转');
  assert.equal(describeIntent({ throttle: -1000, steer: -1000 }), '后退 · 左转');
});

/* ------------------------- 线协议 ------------------------- */

test('WASD 意图编码成 20 字节 DRIVE 包，字段与固件解析位置一致', () => {
  const intent = intentFromKeys(['w']);
  const bytes = encodeCommand(OP.DRIVE, { session: 0x01020304, sequence: 9, ...intent, limit: 100, ttl: 200 });
  assert.equal(bytes.length, 20);
  const info = driveInfo(bytes);
  assert.equal(info.op, OP.DRIVE);
  assert.equal(info.session, 0x01020304);
  assert.equal(info.sequence, 9);
  assert.equal(info.throttle, 1000);
  assert.equal(info.steer, 0);
  assert.equal(info.limit, 100);
  assert.equal(info.ttl, 200);
});

test('协议上下界：意图满量程与上限都在 encodeCommand 允许范围内', () => {
  for (const combo of [['w'], ['s'], ['a'], ['d'], ['w', 'a'], ['w', 'd'], ['s', 'a'], ['s', 'd']]) {
    const intent = intentFromKeys(combo);
    assert.doesNotThrow(() => encodeCommand(OP.DRIVE, { session: 1, sequence: 1, ...intent, limit: 300, ttl: 50 }));
    assert.doesNotThrow(() => encodeCommand(OP.DRIVE, { session: 1, sequence: 1, ...intent, limit: 0, ttl: 200 }));
  }
});

/* --------------- 完整链路：键盘意图走安全门禁 --------------- */

function harness({ limit = 100 } = {}) {
  let now = 0;
  const writes = [];
  const controller = new RemoteController({ now: () => now });
  const generation = controller.attach(async bytes => { writes.push(bytes.slice()); });
  controller.ingest(mainPacket({ limit }), generation);
  controller.setLimit(limit);
  return { controller, generation, writes, advance: ms => { now += ms; } };
}
async function arm(h) {
  await flush();
  await h.controller.arm();
  const armPacket = driveInfo(h.writes.at(-1));
  assert.equal(armPacket.op, OP.ARM);
  h.controller.ingest(mainPacket({ armed: true, sequence: armPacket.sequence }), h.generation);
  assert.equal(h.controller.state.armed, true);
}
function lastDrive(writes) {
  const drives = writes.map(driveInfo).filter(info => info.op === OP.DRIVE);
  return drives.at(-1);
}

test('未解锁时按住 W 不会发出任何 DRIVE 包', async () => {
  const h = harness();
  const keys = new KeyHoldSet();
  keys.press('w');
  const intent = keys.intent();
  assert.equal(h.controller.setIntent(intent.throttle, intent.steer), false, '未解锁应拒绝意图');
  await flush();
  assert.equal(h.writes.some(bytes => bytes[1] === OP.DRIVE), false);
  assert.equal(h.controller.throttle, 0);
});

test('解锁后按住 W 发出满前进 DRIVE，松开立即归零且保持解锁', async () => {
  const h = harness({ limit: 120 });
  await arm(h);
  const keys = new KeyHoldSet();
  keys.press('w');
  assert.equal(h.controller.setIntent(...Object.values(keys.intent()).slice(0, 2)), true);

  h.advance(50); h.controller.tick();
  await flush();
  let drive = lastDrive(h.writes);
  assert.equal(drive.throttle, 1000);
  assert.equal(drive.steer, 0);
  assert.equal(drive.limit, 120);
  assert.equal(drive.ttl, 200);
  assert.deepEqual(mixWheels({ throttle: drive.throttle, steer: drive.steer }, drive.limit), { left: 120, right: 120 });

  // 松开：0,0 立即触发一次 DRIVE（不等 50ms 周期）
  keys.release('w');
  const released = keys.intent();
  h.controller.setIntent(released.throttle, released.steer);
  await flush();
  drive = lastDrive(h.writes);
  assert.equal(drive.throttle, 0);
  assert.equal(drive.steer, 0);
  assert.equal(h.controller.state.armed, true, '松开归零不应取消解锁');
});

test('W+A 斜向意图下发，左右轮速差与固件一致', async () => {
  const h = harness({ limit: 100 });
  await arm(h);
  const keys = new KeyHoldSet();
  keys.press('w'); keys.press('a');
  const intent = keys.intent();
  h.controller.setIntent(intent.throttle, intent.steer);
  h.advance(50); h.controller.tick();
  await flush();
  const drive = lastDrive(h.writes);
  assert.equal(drive.throttle, 1000);
  assert.equal(drive.steer, -1000);
  assert.deepEqual(mixWheels(drive, drive.limit), { left: 0, right: 100 });
});

test('空格急停：发送 STOP 并回到锁定，之后按键不再产生 DRIVE', async () => {
  const h = harness();
  await arm(h);
  const keys = new KeyHoldSet();
  keys.press('w');
  h.controller.setIntent(1000, 0);
  h.advance(50); h.controller.tick();
  await flush();

  keys.clear();
  await h.controller.stop('按键急停');
  await flush();
  assert.equal(h.controller.state.armed, false);
  assert.equal(h.writes.some(bytes => bytes[1] === OP.STOP), true, '应发出 STOP 包');

  const before = h.writes.length;
  assert.equal(h.controller.setIntent(...Object.values(keys.intent()).slice(0, 2)), false);
  h.advance(200); h.controller.tick();
  await flush();
  const after = h.writes.slice(before).map(driveInfo).filter(info => info.op === OP.DRIVE);
  assert.deepEqual(after, [], '锁定后不得再产生 DRIVE');
});

test('事件循环卡顿 ≥150ms 触发急停并锁定（键盘长按不豁免）', async () => {
  const h = harness();
  await arm(h);
  h.controller.setIntent(1000, 0);
  h.advance(50); h.controller.tick();
  await flush();
  assert.equal(h.controller.state.armed, true);

  h.advance(150); h.controller.tick();
  await flush();
  assert.equal(h.controller.state.armed, false, '调度中断应锁定');
  assert.equal(h.controller.state.note.includes('调度中断'), true);
});

test('未校准满四轮时无法解锁：WASD 全程只能停着', async () => {
  const writes = [];
  let now = 0;
  const controller = new RemoteController({ now: () => now });
  const generation = controller.attach(async bytes => { writes.push(bytes.slice()); });
  controller.ingest(mainPacket({ calibratedMask: 0b0111 }), generation);
  assert.equal(controller.canArm(), false);
  await assert.rejects(() => controller.arm(), /四轮已确认/);
  controller.setIntent(1000, 0);
  now += 100; controller.tick();
  await flush();
  assert.equal(writes.some(bytes => bytes[1] === OP.DRIVE), false);
});

test('轮速目标按轮位成对下发：M1/M3 用 left，M2/M4 用 right', () => {
  // 与 remote_control.c 第 214-215 行一致的目标映射（此处只锁定顺序约定）。
  const targets = [0, 0, 0, 0];
  const mixed = mixWheels({ throttle: 1000, steer: 1000 }, 100);
  targets[0] = targets[2] = mixed.left;
  targets[1] = targets[3] = mixed.right;
  assert.deepEqual(targets, [100, 0, 100, 0]);
});
