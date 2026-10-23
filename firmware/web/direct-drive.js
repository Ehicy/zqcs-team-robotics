/* Fresh human intent prepares the existing wire protocol; connection never does.
 * Cancellation invalidates every continuation, including a late ARM status. */
export class DirectDrive {
  constructor({ controller, calibration, foreground = () => true, error = () => {}, changed = () => {} }) {
    Object.assign(this, { controller, calibration, foreground, error, changed });
    this.intent = { throttle: 0, steer: 0 }; this.job = null; this.epoch = 0; this.phase = 'IDLE';
  }
  current(job) {
    const c = this.controller;
    return this.job === job && job.epoch === this.epoch && this.foreground() &&
      c.state.connected && c.state.session === job.session && c.generation === job.generation &&
      c.stopGeneration === job.stopGeneration && !!(this.intent.throttle || this.intent.steer);
  }
  cancel(note = '已停止', sendStop = true) {
    const had = !!this.job || this.controller.state.armed || this.controller.state.armPending;
    this.epoch++; this.job = null; this.intent = { throttle: 0, steer: 0 }; this.phase = 'IDLE';
    this.controller.setIntent(0, 0);
    if (sendStop && had) this.controller.stop(note).catch(this.error);
    this.changed();
  }
  request(throttle, steer) {
    if (![throttle, steer].every(v => Number.isInteger(v) && v >= -1000 && v <= 1000)) throw new Error('方向输入越界');
    if (!throttle && !steer) { this.cancel('已松开，停止'); return true; }
    const c = this.controller;
    if (!this.foreground() || !c.state.connected || !c.state.uart || !c.state.session || c.state.fault) return false;
    this.intent = { throttle, steer };
    if (this.job) { this.tick(); return true; }
    if (c.servoOwner || c.servoMotion?.movingMask || c.state.active || c.state.remoteArmed || c.state.configBusy) {
      this.intent = { throttle: 0, steer: 0 }; this.error(new Error('请等待当前动作停稳，再按方向键')); return false;
    }
    const job = { epoch: ++this.epoch, session: c.state.session, generation: c.generation,
      stopGeneration: c.stopGeneration, started: c.now(), lastTick: c.now() };
    this.job = job; this.phase = 'PREPARING'; this.changed();
    void this.prepare(job); return true;
  }
  async prepare(job) {
    const c = this.controller;
    try {
      const values = this.calibration();
      const applied = c.appliedCalibration;
      if (!applied || JSON.stringify(values) !== JSON.stringify(applied) || !c.state.ready || !c.state.pidReady || c.state.calibratedMask !== 15)
        await c.configure(values);
      if (!this.current(job)) return;
      this.phase = 'READY'; this.tick();
    } catch (e) {
      if (!this.current(job)) return;
      this.cancel('参数准备失败，已停止'); this.error(e);
    }
  }
  tick() {
    const job = this.job, c = this.controller;
    if (!job) return;
    if (!this.current(job)) { this.cancel('控制已中止'); return; }
    const now = c.now(), gap = now - job.lastTick; job.lastTick = now;
    if (gap >= 150 || now - job.started >= 15000 && this.phase !== 'DRIVING' ||
        now - c.mainStatusAt >= 500 || c.state.fault || !c.state.uart) {
      this.cancel('状态过期或动作中断，已停止'); return;
    }
    if (this.phase === 'READY' && c.canArm()) {
      this.phase = 'ARMING'; this.changed();
      void c.arm().catch(e => { if (this.current(job)) { this.cancel('控制准备失败，已停止'); this.error(e); } });
    }
    if (this.phase === 'ARMING' && c.state.armed) this.phase = 'DRIVING';
    if (this.phase === 'DRIVING') {
      if (!c.state.armed) { this.cancel('控制已停止'); return; }
      c.setIntent(this.intent.throttle, this.intent.steer);
    }
    this.changed();
  }
}
