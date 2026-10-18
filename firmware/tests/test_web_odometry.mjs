import test from 'node:test';
import assert from 'node:assert/strict';
import { WheelOdometry } from '../web/odometry.js';

const calibration = [1756, 1791, 1774, 898].map((countsPerRev, wheel) => ({
  wheel, countsPerRev, diameterTenthMm: 650, encoderNegative: wheel % 2 === 0, confirmed: true,
}));
const signs = [-1, 1, -1, 1];
function setup(trackMm = 200, counts = [0, 0, 0, 0]) {
  const o = new WheelOdometry();
  o.start({ counts, calibration, trackMm, session: 7, now: 0 });
  return o;
}
function move(o, counts, now = 300, changes = {}) {
  return o.update({ counts, now, session: 7, healthy: true, validMask: 15, ...changes });
}
function near(actual, expected, tolerance = 1e-8) { assert.ok(Math.abs(actual - expected) < tolerance, `${actual} != ${expected}`); }
const circumference = Math.PI * 65;
const oneTurn = calibration.map((c, i) => signs[i] * c.countsPerRev);

test('真实轮位、独立 CPR 和 4×/2×：前进一圈约 204.2 mm，后退回原点', () => {
  const o = setup();
  assert.equal(move(o, oneTurn), true);
  near(o.x, circumference); near(o.y, 0); near(o.yaw, 0);
  o.travel.forEach(v => near(v, circumference));
  move(o, [0, 0, 0, 0], 600);
  near(o.x, 0); near(o.y, 0);
});

test('左转与右转按 M1/M3 右、M2/M4 左分组，原地转向无平移', () => {
  const o = setup();
  const counts = oneTurn.map((n, i) => i % 2 === 0 ? n : -n);
  move(o, counts);
  near(o.x, 0); near(o.y, 0); near(o.yaw, 2 * circumference / 200);
  move(o, [0, 0, 0, 0], 600);
  near(o.yaw, 0);
});

test('右侧单独前进：左弧线积分的位移与几何圆弧一致', () => {
  const o = setup();
  move(o, oneTurn.map((n, i) => i % 2 === 0 ? n : 0));
  const angle = circumference / 200;
  near(o.x, 100 * Math.sin(angle)); near(o.y, 100 * (1 - Math.cos(angle)));
  near(o.yaw, angle);
});

test('轮距未知只累计各轮路程，不制造二维位置', () => {
  const o = setup(null);
  move(o, oneTurn);
  o.travel.forEach(v => near(v, circumference));
  assert.deepEqual(o.path, []); assert.equal(o.track, null); assert.equal(o.x, 0);
});

test('断联/会话变化/采样缺失/无效轮使地图冻结，正常数据不自动恢复', () => {
  for (const change of [{ healthy: false }, { session: 8 }, { now: 501 }, { validMask: 7 }]) {
    const o = setup();
    assert.equal(move(o, oneTurn, 300, change), false);
    assert.equal(o.active, false); assert.equal(o.x, 0);
    assert.equal(move(o, oneTurn, 400), false);
    assert.equal(o.x, 0);
  }
});

test('无新通知也会因超时失效', () => {
  const o = setup();
  o.check({ now: 501, session: 7, healthy: true });
  assert.equal(o.active, false);
});

test('小幅 32 位计数回绕正常，非物理跳变不积分', () => {
  const baseline = [2147483646, 2147483646, 2147483646, 2147483646];
  const o = setup(200, baseline);
  move(o, baseline.map(n => (n + 4) | 0), 100);
  assert.equal(o.active, true);
  near(o.travel[0], -4 * circumference / 1756);
  const before = o.x;
  move(o, [0, 0, 0, 0], 200);
  assert.equal(o.active, false); assert.equal(o.x, before);
});

test('拒绝缺少确认、非法轮距、非有限计数，不覆盖既有状态', () => {
  const o = setup();
  for (const trackMm of [0, -10, NaN, Infinity, 1001]) {
    assert.throws(() => o.start({ counts: [0, 0, 0, 0], calibration, trackMm, session: 7, now: 0 }));
  }
  assert.throws(() => o.start({ counts: [0, 0, 0, 0], calibration: calibration.map(c => ({ ...c, confirmed: false })), session: 7, now: 0 }));
  assert.throws(() => o.start({ counts: [NaN, 0, 0, 0], calibration, session: 7, now: 0 }));
  assert.equal(o.track, 200);
});

test('历史轨迹有界，静止样本不无限追加', () => {
  const o = setup();
  for (let t = 100; t <= 200000; t += 100) move(o, oneTurn.map(n => Math.round(n * t / 10000)), t);
  assert.equal(o.active, true); assert.ok(o.path.length <= 1000);
  const size = o.path.length;
  for (let t = 200100; t <= 201000; t += 100) move(o, oneTurn.map(n => n * 20), t);
  assert.equal(o.path.length, size);
});

test('人工新起点清空旧轨迹，参数复制后不受表单对象变化影响', () => {
  const o = setup();
  move(o, oneTurn);
  o.start({ counts: oneTurn, calibration, trackMm: 180, session: 8, now: 1000 });
  assert.equal(o.x, 0); assert.deepEqual(o.travel, [0, 0, 0, 0]);
  assert.deepEqual(o.path, [{ x: 0, y: 0 }]);
  assert.equal(o.session, 8);
});
