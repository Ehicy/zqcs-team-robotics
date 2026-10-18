import test from 'node:test';
import assert from 'node:assert/strict';
import { cellCenter, fieldPose, fieldToImage, calibrationRoute, VEHICLE } from '../web/competition-field.js';

test('底图内框四角与格中心对齐，不把图外标签算入场地', () => {
  assert.deepEqual(fieldToImage(0, 0), { x: 78, y: 398 });
  assert.deepEqual(fieldToImage(2400, 2400), { x: 438, y: 38 });
  assert.deepEqual(fieldToImage(150, 150), { x: 100.5, y: 375.5 });
  assert.deepEqual(fieldToImage(2250, 2250), { x: 415.5, y: 60.5 });
});
test('红蓝起点旋转 180 度，实际前进朝向场内', () => {
  assert.deepEqual(cellCenter('a1'), { x: 150, y: 150 });
  assert.deepEqual(cellCenter('h8'), { x: 2250, y: 2250 });
  const pose = { x: 600, y: 100, yaw: Math.PI / 2 };
  assert.deepEqual(fieldPose(pose, 'red'), { x: 50, y: 750, screenAngle: -90 });
  assert.deepEqual(fieldPose(pose, 'blue'), { x: 2350, y: 1650, screenAngle: 90 });
  assert.throws(() => cellCenter('a0'));
  assert.throws(() => fieldPose(pose, 'unknown'));
});
test('600 mm 首测仅为预览、往返同一起点，车体使用用户实测尺寸', () => {
  for (const side of ['red', 'blue']) {
    const route = calibrationRoute(side);
    assert.deepEqual(route[0], route[2]);
    assert.equal(Math.hypot(route[1].x - route[0].x, route[1].y - route[0].y), 600);
  }
  assert.deepEqual(VEHICLE, { trackMm: 165, wheelbaseMm: 130, lengthMm: 240, widthMm: 190, diameterMm: 65 });
});
