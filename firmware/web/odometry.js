/* Display-only wheel odometry. No transport, command generation, or motor I/O.
 * Coordinate frame: X = initial forward, Y = initial left, yaw positive left.
 * BLE counts have no source timestamp/boot epoch. Loss of continuity therefore
 * invalidates the estimate until the operator establishes a new origin.
 */
export class WheelOdometry {
  constructor() {
    this.active = false;
    this.reason = '停车后确认地图起点';
    this.x = this.y = this.yaw = 0;
    this.travel = [0, 0, 0, 0];
    this.path = [];
    this.track = null;
    this.lastAt = -Infinity;
  }

  start({ counts, calibration, trackMm = null, session, now }) {
    if (!this.validCounts(counts) || !Number.isFinite(now) || !Number.isInteger(session) || session <= 0)
      throw new Error('需要有效的编码器基准与通信会话');
    if (!Array.isArray(calibration) || calibration.length !== 4 || calibration.some((c, i) =>
      c.wheel !== i || !c.confirmed || !Number.isInteger(c.countsPerRev) || c.countsPerRev < 1 || c.countsPerRev > 1000000 ||
      !Number.isInteger(c.diameterTenthMm) || c.diameterTenthMm < 1 || c.diameterTenthMm > 5000 || typeof c.encoderNegative !== 'boolean'))
      throw new Error('请先核对四轮表单的方向、每圈计数和轮径');
    if (trackMm !== null && (!Number.isFinite(trackMm) || trackMm < 50 || trackMm > 1000))
      throw new Error('左右轮中心距应为 50–1000 mm；未知请留空');
    this.scale = calibration.map(c => Math.PI * c.diameterTenthMm / 10 / c.countsPerRev * (c.encoderNegative ? -1 : 1));
    this.counts = [...counts];
    this.session = session;
    this.track = trackMm;
    this.x = this.y = this.yaw = 0;
    this.travel = [0, 0, 0, 0];
    this.path = trackMm === null ? [] : [{ x: 0, y: 0 }];
    this.lastAt = now;
    this.active = true;
    this.reason = trackMm === null ? '待轮距：仅累计轮轴路程' : '编码器估计 · 未经地面标定';
  }

  validCounts(counts) {
    return Array.isArray(counts) && counts.length === 4 && counts.every(n => Number.isInteger(n) && n >= -2147483648 && n <= 2147483647);
  }

  invalidate(reason) {
    if (!this.active) return;
    this.active = false;
    this.reason = `${reason}；停车后重新确认起点`;
  }

  check({ now, session, healthy }) {
    if (!this.active) return;
    if (!healthy) this.invalidate('连接或底盘反馈失效');
    else if (session !== this.session) this.invalidate('通信会话已改变');
    else if (!Number.isFinite(now) || now < this.lastAt || now - this.lastAt > 500) this.invalidate('编码器采样中断');
  }

  update({ counts, validMask, now, session, healthy }) {
    this.check({ now, session, healthy });
    if (!this.active) return false;
    if (validMask !== 15 || !this.validCounts(counts)) {
      this.invalidate('四轮编码器不完整');
      return false;
    }
    const dt = now - this.lastAt;
    const steps = counts.map((n, i) => ((n - this.counts[i]) | 0) * this.scale[i]);
    // UI receipt time is not a precise sample clock. This loose physical gate
    // catches gross resets/jumps only, never certifies all smaller resets.
    if (steps.some(d => Math.abs(d) > 30 + 1500 * dt / 1000)) {
      this.invalidate('计数跳变或复位疑似');
      return false;
    }
    this.counts = [...counts];
    this.lastAt = now;
    this.travel = this.travel.map((d, i) => d + steps[i]);
    if (this.track === null) return true;
    const right = (steps[0] + steps[2]) / 2;
    const left = (steps[1] + steps[3]) / 2;
    const distance = (left + right) / 2;
    const angle = (right - left) / this.track;
    // Exact constant-curvature integration, stable at zero curvature.
    const half = angle / 2;
    const chord = distance * (Math.abs(half) < 1e-8 ? 1 : Math.sin(half) / half);
    this.x += chord * Math.cos(this.yaw + half);
    this.y += chord * Math.sin(this.yaw + half);
    this.yaw = Math.atan2(Math.sin(this.yaw + angle), Math.cos(this.yaw + angle));
    const previous = this.path[this.path.length - 1];
    if (!previous || Math.hypot(this.x - previous.x, this.y - previous.y) >= 2) {
      this.path.push({ x: this.x, y: this.y });
      if (this.path.length > 1000) this.path.splice(0, 1);
    }
    return true;
  }
}
