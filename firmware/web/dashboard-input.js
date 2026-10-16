/* WASD → 行驶意图的纯逻辑层。不碰 DOM、不碰蓝牙，可直接在 Node 里测试。
 *
 * 语义与 firmware/shared/remote_control.c 的 DRIVE 分支严格一致：
 *   left  = throttle + steer
 *   right = throttle - steer
 *   scale = max(1000, |left|, |right|)      // 整数除法向零截断
 *   left_mm_s  = left  * limit / scale
 *   right_mm_s = right * limit / scale
 * 两个目标值分别下发给 M1/M3（左后/左前）与 M2/M4（右后/右前）。
 */

/** 协议里 throttle / steer 的满量程（encodeCommand 允许 ±1000）。 */
export const AXIS_FULL = 1000;

/** 按键分组：正向键与反向键相抵。 */
export const KEY_GROUPS = Object.freeze({
  forward: 'w',
  backward: 's',
  left: 'a',
  right: 'd',
});

/** 方向键是 WASD 的等价别名。 */
const ALIASES = Object.freeze({ arrowup: 'w', arrowdown: 's', arrowleft: 'a', arrowright: 'd' });

/** 归一化按键名：小写，方向键映射到 WASD。 */
export function normalizeKey(key) {
  const value = String(key ?? '').toLowerCase();
  return Object.prototype.hasOwnProperty.call(ALIASES, value) ? ALIASES[value] : value;
}

/** 是否是会驱动底盘的按键。 */
export function isDriveKey(key) {
  const normalized = normalizeKey(key);
  return normalized === 'w' || normalized === 's' || normalized === 'a' || normalized === 'd';
}

/** 是否是急停键（空格 / Esc）。 */
export function isStopKey(key) {
  return key === ' ' || key === 'Spacebar' || key === 'Escape' || key === 'Esc';
}

/** 把按键集合折叠成一组意图；同时按下相反键即为该轴归零。 */
export function intentFromKeys(active, { full = AXIS_FULL } = {}) {
  const held = new Set();
  for (const key of active ?? []) {
    const normalized = normalizeKey(key);
    if (isDriveKey(normalized)) held.add(normalized);
  }
  const axis = (positive, negative) => (held.has(positive) ? 1 : 0) - (held.has(negative) ? 1 : 0);
  return { throttle: axis('w', 's') * full, steer: axis('d', 'a') * full };
}

/** 与固件同式的轮速预览（mm/s）。仅用于界面显示，不参与控制。 */
export function mixWheels({ throttle, steer }, limit) {
  const left = throttle + steer;
  const right = throttle - steer;
  let scale = AXIS_FULL;
  if (Math.abs(left) > scale) scale = Math.abs(left);
  if (Math.abs(right) > scale) scale = Math.abs(right);
  const toMmS = value => Math.trunc(value * limit / scale);
  return { left: toMmS(left), right: toMmS(right) };
}

/** 意图的中文描述，用于界面与日志。 */
export function describeIntent({ throttle, steer }) {
  if (!throttle && !steer) return '停止';
  const parts = [];
  if (throttle > 0) parts.push('前进');
  else if (throttle < 0) parts.push('后退');
  if (steer > 0) parts.push(throttle ? '右转' : '原地右转');
  else if (steer < 0) parts.push(throttle ? '左转' : '原地左转');
  return parts.join(' · ');
}

/* 键盘按住集合：keydown 加入、keyup 移除，供 dashboard.js 直接使用。 */
export class KeyHoldSet {
  constructor() { this.held = new Set(); }
  press(key) {
    const normalized = normalizeKey(key);
    if (!isDriveKey(normalized) || this.held.has(normalized)) return false;
    this.held.add(normalized);
    return true;
  }
  release(key) {
    const normalized = normalizeKey(key);
    if (!this.held.delete(normalized)) return false;
    return true;
  }
  clear() { const had = this.held.size > 0; this.held.clear(); return had; }
  get size() { return this.held.size; }
  intent(options) { return intentFromKeys(this.held, options); }
}
