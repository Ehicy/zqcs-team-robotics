/* Finite, operator-started encoder-distance test. No BLE writer of its own.
 * The existing controller owns ARM/DRIVE, ACKs, leases and fail-stop behavior. */
export class PidShuttle {
  constructor({ controller, telemetry, calibration, now = () => performance.now(), changed = () => {} }) {
    Object.assign(this, { controller, telemetry, calibration, now, changed });
    this.phase = 'IDLE'; this.results = []; this.message = '尚未开始'; this.travel = 0;
  }
  get active() { return ['ARMING', 'FORWARD', 'REVERSE', 'SETTLING'].includes(this.phase); }
  snapshot() { return { phase: this.phase, active: this.active, message: this.message, travelMm: this.travel,
    distanceMm: this.distance, speedMmS: this.limit, leg: this.leg, legs: this.legs, results: this.results.map(x => ({ ...x })) }; }
  feedback() {
    const t = this.telemetry(), c = this.controller, time = this.now();
    if (!c.state.connected || !c.state.uart || !c.state.pidReady || !c.state.ready || c.state.fault ||
        c.state.configBusy || time-c.mainStatusAt >= 350 ||
        !t.encoder || t.encoder.validMask !== 15 || time-t.encoderAt >= 300 ||
        !t.speed || t.speed.validMask !== 15 || time-t.speedAt >= 300 || t.speed.faultCode)
      throw new Error('链路、PID 或四轮反馈未就绪／已过期，测试中止');
    return t;
  }
  stopped(t) { return t.speed.measured.every(v => v === 0) && t.speed.targets.every(v => v === 0); }
  updateSettings({ distanceMm, cycles, speedPercent }) {
    if (!Number.isInteger(distanceMm) || distanceMm < 50 || distanceMm > 3000 ||
        !Number.isInteger(cycles) || cycles < 1 || cycles > 20 ||
        !Number.isInteger(speedPercent) || speedPercent < 1 || speedPercent > 100)
      throw new Error('距离50–3000mm，往复1–20次，速度比例1–100%');
    if (!this.active) return;
    const limit=Math.max(1, Math.floor(Math.min(...this.config.map(v => v.maxMmS))*speedPercent/100));
    const rated=Math.min(...this.config.map(v=>260*Math.PI*(v.diameterTenthMm/10)/60));
    if(limit>rated&&!this.allowOverspeed)throw new Error('目标超过额定参考，本次未启用超额定测试');
    this.distance = distanceMm; this.legs = cycles * 2;
    this.limit = limit;
    // The current leg keeps its encoder origin. Reducing distance below the
    // travelled value requests settling on the next tick; it never restarts.
    this.changed();
  }
  async start({ distanceMm = 300, cycles = 2, speedPercent = 100, overspeed = false, spaceConfirmed = false } = {}) {
    if (this.active) throw new Error('测试已在运行');
    if (!Number.isInteger(distanceMm) || distanceMm < 50 || distanceMm > 3000 ||
        !Number.isInteger(cycles) || cycles < 1 || cycles > 20 ||
        !Number.isInteger(speedPercent) || speedPercent < 1 || speedPercent > 100) throw new Error('距离50–3000mm，往复1–20次，速度比例1–100%');
    if (!spaceConfirmed) throw new Error('请先确认前后有停车余量、车辆落地且有人可操作物理断电');
    this.controller.assertStopped(); const t = this.feedback();
    if (!this.stopped(t)) throw new Error('请等待四轮目标和实测轮速均为零');
    const config = this.calibration(), applied = this.controller.appliedCalibration;
    const keys = ['countsPerRev', 'diameterTenthMm', 'encoderNegative', 'motorNegative', 'confirmed', 'maxMmS', 'kpMilli', 'kiMilli', 'kdMilli'];
    if (!applied || config.length !== 4 || config.some((v,i) => !v.confirmed || keys.some(k => v[k] !== applied[i][k])))
      throw new Error('当前表单尚未逐包确认，请先写入统一 PID 和四轮配置');
    this.limit = Math.max(1, Math.floor(Math.min(...config.map(v => v.maxMmS))*speedPercent/100));
    const rated = Math.min(...config.map(v => 260*Math.PI*(v.diameterTenthMm/10)/60));
    if (this.limit > rated && !overspeed) throw new Error('目标超过260rpm额定参考，请明确勾选超额定测试确认');
    this.config = config.map(v => ({ ...v })); this.allowOverspeed=overspeed; this.distance = distanceMm; this.legs = cycles*2; this.leg = 0;
    this.oldLimit = this.controller.state.limit; this.session = this.controller.state.session;
    this.results = []; this.phase = 'ARMING'; this.travel = 0; this.started = this.now();
    this.message = '等待显式解锁回执'; this.changed();
    try { this.controller.setLimit(this.limit); await this.controller.arm(); }
    catch (e) { this.stop(e.message, 'ERROR'); throw e; }
  }
  begin(t) {
    this.leg++; this.direction = this.leg%2 ? 1 : -1;
    this.phase = this.direction > 0 ? 'FORWARD' : 'REVERSE'; this.origin = t.encoder.counts.slice();
    this.legStarted = this.lastProgressAt = this.now(); this.travel = this.lastProgress = 0;
    this.stopSince = null; this.lastTick = this.now(); this.lastTravel = 0;
    this.message = `${this.direction>0?'前进':'后退'}，第 ${this.leg}/${this.legs} 段`;
    this.controller.setLimit(this.limit); this.controller.setIntent(this.direction*1000,0);
  }
  cancel(message = '测试已取消') {
    if (!this.active) return;
    this.phase = 'CANCELLED'; this.message = message;
    if (this.oldLimit !== undefined) this.controller.setLimit(this.oldLimit);
    this.changed();
  }
  stop(message = '测试停止并锁定', phase = 'CANCELLED') {
    if (!this.active) return;
    this.phase = phase; this.message = message;
    this.controller.stop(message).catch(() => {});
    if (this.oldLimit !== undefined) this.controller.setLimit(this.oldLimit);
    this.changed();
  }
  tick() {
    if (!this.active) return;
    try {
      const t = this.feedback(), time = this.now(), c = this.controller;
      if (c.state.session !== this.session) throw new Error('会话已变化，测试中止且不会续跑');
      if (this.phase === 'ARMING') {
        if (time-this.started >= 1500) throw new Error('解锁超时');
        if (c.state.armed) this.begin(t);
        this.changed(); return;
      }
      if (!c.state.armed) throw new Error('控制已锁定，测试中止');
      if (time-this.lastTick >= 150) throw new Error('页面测试调度中断');
      const mm = t.encoder.counts.map((v,i) => {
        const delta = (v-this.origin[i])|0, cfg = this.config[i];
        return delta*(cfg.encoderNegative?-1:1)*Math.PI*(cfg.diameterTenthMm/10)/cfg.countsPerRev*this.direction;
      });
      this.travel = mm.reduce((a,b) => a+b,0)/4;
      if (!mm.every(Number.isFinite) || Math.abs(this.travel-this.lastTravel) > this.limit*.35+30 ||
          this.travel < -10 || Math.max(...mm)-Math.min(...mm) > Math.max(60,this.distance*.35))
        throw new Error('编码器跳变、方向错误或四轮行程差过大');
      this.lastTick = time; this.lastTravel = this.travel;
      if (this.phase === 'SETTLING') {
        c.setIntent(0,0);
        if (this.stopped(t)) { if (this.stopSince === null) this.stopSince = time; }
        else this.stopSince = null;
        if (time-this.settleStarted >= 5000) throw new Error('五秒内未确认停稳');
        if (this.stopSince !== null && time-this.stopSince >= 400) {
          this.results.push({ leg: this.leg, direction: this.direction, travelMm: this.travel,
            overshootMm: this.travel-this.distance, elapsedMs: time-this.legStarted });
          if (this.leg >= this.legs) this.stop('往复测试完成，已停止并锁定', 'DONE');
          else this.begin(t);
        }
      } else {
        if (this.travel > this.lastProgress+1) { this.lastProgress=this.travel; this.lastProgressAt=time; }
        if (time-this.lastProgressAt >= 1500) throw new Error('电机没有有效行程进展');
        if (time-this.legStarted > Math.min(60000,this.distance/Math.max(this.limit,10)*3000+6000)) throw new Error('单段行驶超时');
        const remaining = this.distance-this.travel;
        if (remaining <= 5) {
          c.setIntent(0,0); this.phase='SETTLING'; this.settleStarted=time; this.stopSince=null;
          this.message='等待四轮停稳后再换向';
        } else {
          // 500mm/s² matches the configured acceleration. This is a soft
          // approach, not a physical stopping-distance or position guarantee.
          c.setLimit(Math.max(1,Math.min(this.limit,Math.floor(Math.sqrt(2*500*remaining)*.8))));
          c.setIntent(this.direction*1000,0);
        }
      }
      this.changed();
    } catch (e) { this.stop(e.message, 'ERROR'); }
  }
}

export function installPidTest({ root, controller, telemetry, calibration, foreground, error, log, direct = false }) {
  root.innerHTML = `<h2>3 · PID 往复测试</h2><p class="hint">写入上方两组参数后，设置距离和次数。一次往复 = 前进 → 停稳 → 后退 → 停稳。默认速度上限100%；接近端点自动减速，短距离不一定达到上限。行程来自编码器，打滑和滑行会造成位置误差。</p>
    <div class="config-grid pid-slider-grid"><label>单程距离 mm<output id="pidDistanceValue" for="pidDistance">300 mm</output><input id="pidDistance" type="range" min="50" max="3000" value="300" step="10" aria-label="单程距离 mm"></label>
    <label>往复次数<output id="pidCyclesValue" for="pidCycles">2 次</output><input id="pidCycles" type="range" min="1" max="20" value="2" step="1" aria-label="往复次数"></label>
    <label>速度上限比例 %<output id="pidPercentValue" for="pidPercent">100%</output><input id="pidPercent" type="range" min="1" max="100" value="100" step="1" aria-label="速度上限比例 %"></label></div>
    <p id="pidTestLiveStatus" class="hint" role="status">距离、次数、速度拖动即作用于当前测试；未开始时只预设，不会自动开车。PID 系数在上方滑块调整，停稳后自动写入。</p>
    <p id="pidTestSpeedPreview" class="hint"></p><p class="hint">额定参考260rpm：65mm轮约885mm/s。额定值只作参考；最大目标轮速可在四轮配置调至1000mm/s，闭环PWM上限100%，不等于硬件限流。</p>
    <label class="confirmation" ${direct?'hidden':''}><input id="pidSpace" type="checkbox">已确认前后空间、停车余量及物理断电，车辆落地且有人看护</label>
    <label class="confirmation" ${direct?'hidden':''}><input id="pidOverspeed" type="checkbox">允许本次超过额定参考目标；已核对电机、驱动、电池与升压器能力</label>
    <div class="row"><button id="pidTestStart" type="button" class="primary">开始往复测试</button><button id="pidTestStop" type="button" class="danger">停止测试</button></div>
    <p id="pidTestState" role="status" class="hint">尚未开始</p><div id="pidTestResults" class="hint"></div>
    <details><summary>如何根据车辆行为调整 PID</summary><ul>
    <li>先用短距离、低速验证方向和停车，再提高速度。检查供电、编码器、轮胎及机械阻力，PID不能弥补接错线或驱动过载。</li>
    <li>先设 Ki=0、Kd=0，逐步提高 Kp：响应迟缓且误差大可稍增；车身抖动、速度反复波动则减小。每次只改一项。</li>
    <li>再少量增加 Ki 消除持续偏慢；加速后冲过头、来回摆或松开负载后冲高，应减小 Ki。输出长期顶住100%时先查目标是否可达，勿靠加积分强推。</li>
    <li>最后从 Kd=0 小步增加，观察是否减轻轮速波动；若出现高频抖动、噪声或输出跳动，减小 Kd并检查编码器。D项基于测速而非目标，不能保证减少端点滑行距离。</li>
    <li>前后都偏一个方向优先查轮径、方向、地面与轮胎差异；本模块统一系数、四轮独立计算，不自动生成“最优PID”。端点超程是编码器行程加滑行的结果，不单独代表PID超调。</li>
    <li>PID 滑块自动合并最新一组系数并写入，行驶中改系数会待停稳后发送；ACK只确认设备接受，底盘同步需看回读。距离/次数/速度无需写Flash，拖动即改变本次测试；缩短距离到已走行程以内会进入停车阶段，不会重设编码器起点。额定以上目标允许设置，但不能据此判定电机/驱动/升压器能长期承受。</li></ul></details>`;
  const field = id => root.querySelector(`#${id}`);
  const runner = new PidShuttle({ controller, telemetry, calibration, changed: render });
  let preparing = false, prepareId = 0;
  function render() {
    const s=runner.snapshot();
    for(const [id,unit]of [['pidDistance',' mm'],['pidCycles',' 次'],['pidPercent','%']])field(id+'Value').textContent=field(id).value+unit;
    try { const max=Math.min(...calibration().map(v=>v.maxMmS)); field('pidTestSpeedPreview').textContent=`当前最高目标 ${max}mm/s，本次上限 ${Math.floor(max*Number(field('pidPercent').value)/100)}mm/s（目标值，非实测速度）`; } catch { field('pidTestSpeedPreview').textContent='请先填写有效四轮配置'; }
    const message=direct?s.message.replaceAll('解锁','控制准备').replaceAll('并锁定','').replaceAll('锁定','停止'):s.message;
    field('pidTestState').textContent=`${s.phase} · ${message} · 本段 ${Math.round(s.travelMm)} / ${s.distanceMm??'—'} mm · 上限 ${s.speedMmS??'—'} mm/s`;
    const readyToPrepare=controller.state.connected&&controller.state.uart&&controller.state.session&&!controller.state.fault&&!controller.state.configBusy&&!controller.state.armed&&!controller.state.remoteArmed&&!controller.state.active&&!controller.servoOwner&&!controller.servoMotion?.movingMask;
    field('pidTestStart').disabled=preparing || s.active || !(direct?readyToPrepare:controller.canArm()); field('pidTestStop').disabled=!s.active&&!preparing;
    if(preparing) field('pidTestState').textContent='准备中 · 正在应用当前参数';
    for (const id of ['pidDistance','pidCycles','pidPercent']) field(id).disabled=preparing;
    for (const id of ['pidSpace','pidOverspeed']) field(id).disabled=s.active||preparing;
    field('pidTestResults').textContent=s.results.map(v=>`第${v.leg}段${v.direction>0?'前进':'后退'}：${v.travelMm.toFixed(1)}mm，端点偏差${v.overshootMm.toFixed(1)}mm`).join('；');
  }
  for(const id of ['pidDistance','pidCycles','pidPercent'])field(id).addEventListener('input',()=>{
    try {
      runner.updateSettings({distanceMm:Number(field('pidDistance').value),cycles:Number(field('pidCycles').value),speedPercent:Number(field('pidPercent').value)});
      field('pidTestLiveStatus').textContent=runner.active?'距离、次数、速度已更新；下一次控制周期生效，保持当前编码器起点':'已更新测试预设；点击开始才会行驶';
    }catch(e){error(e);}render();
  });
  field('pidTestStart').onclick=async()=>{
    try {
      if(!foreground()) throw new Error('测试需要页面处于前台');
      if(direct) {
        const id=++prepareId,session=controller.state.session,generation=controller.generation,stopGeneration=controller.stopGeneration;
        preparing=true;render();
        const valid=()=>id===prepareId&&foreground()&&controller.state.connected&&controller.state.session===session&&controller.generation===generation&&controller.stopGeneration===stopGeneration;
        try {
          await controller.configure(calibration());
          const deadline=controller.now()+2000;
          while(valid()&&!controller.canArm()&&controller.now()<deadline) await new Promise(resolve=>setTimeout(resolve,25));
          if(!valid())return;
          if(!controller.canArm())throw new Error('底盘参数或停稳状态未就绪');
        } finally {if(id===prepareId)preparing=false;}
      }
      await runner.start({ distanceMm:Number(field('pidDistance').value), cycles:Number(field('pidCycles').value),
        speedPercent:Number(field('pidPercent').value), spaceConfirmed:direct||field('pidSpace').checked, overspeed:direct||field('pidOverspeed').checked });
      log('用户启动有限次数的PID往复测试');
    } catch(e) { error(e); }
    render();
  };
  const cancelPreparation=()=>{prepareId++;preparing=false;};
  field('pidTestStop').onclick=()=>{const wasPreparing=preparing;cancelPreparation();if(wasPreparing)controller.stop('测试准备已取消').catch(error);runner.stop('测试已停止');render();}; render();
  return { runner, render, cancelPreparation, get preparing(){return preparing;}, tick(){ if(preparing&&!foreground()){cancelPreparation();controller.stop('测试准备已取消').catch(error);} if(runner.active && !foreground()) runner.stop('页面不在前台'); runner.tick(); render(); } };
}
