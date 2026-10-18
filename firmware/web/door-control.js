/* Latest-target owner and short lease renewer. ESP32, not the browser,
 * generates the rate/acceleration-limited PWM trajectory. */
export class DoorControl {
  constructor(controller, { now = () => controller.now(), changed = () => {}, channel = 2 } = {}) {
    if(!Number.isInteger(channel) || channel<0 || channel>2) throw new Error('舵机通道无效');
    this.channel=channel; this.label=channel===2?'门':`舵机${channel+1}`;
    this.controller=controller; this.now=now; this.changed=changed;
    this.active=false; this.inFlight=false; this.message='直接拖动目标；首次接管使用起点参考';
  }
  validateTarget(target) {
    const min=this.channel===2?1000:1300, max=this.channel===2?2000:1700;
    if(!Number.isInteger(target) || target<min || target>max) throw new Error(`脉宽须为${min}–${max}μs整数`);
  }
  start(target, referenceUs=0) {
    const c=this.controller;
    if(this.active || this.inFlight) throw new Error('上一条舵机操作尚未结束');
    this.validateTarget(target); c.assertStopped();
    if(!c.servoAvailable(this.channel)) throw new Error('需缓启动固件、底盘锁定与新鲜状态');
    if(c.servoOwner) throw new Error('其他舵机操作正在运行');
    if(!c.servoStatus.pulses[this.channel]) this.validateTarget(referenceUs);
    else if(referenceUs) throw new Error('已启用时不能重设起点');
    this.referenceUs=referenceUs; this.sent=false;
    this.target=target; this.session=c.state.session; this.generation=c.generation;
    this.stopGeneration=c.stopGeneration; this.lastTick=this.started=this.now(); this.lastWrite=-Infinity;
    this.active=true; c.servoOwner=this; this.message=`${this.label}限速移向 ${target}μs`; this.changed(); c.notify();
  }
  targetTo(target, referenceUs=0) {
    this.validateTarget(target);
    if(!this.active) return this.start(target,referenceUs);
    this.target=target; this.message=`${this.label}目标更新为 ${target}μs`; this.changed();
  }
  cancel(message='跟随已停止；保持最后指令') {
    this.active=false; this.message=message;
    if(this.controller.servoOwner===this) this.controller.servoOwner=null;
    this.changed(); this.controller.notify();
  }
  async tick() {
    if(!this.active) return;
    const c=this.controller, time=this.now();
    if(c.generation!==this.generation || c.state.session!==this.session || c.stopGeneration!==this.stopGeneration ||
       time-this.lastTick>=400 || time-this.started>=15000 || this.sent && time-this.lastWrite>=400) {
      this.cancel('跟随已取消（停止、断联、会话变化或超时）；不会续跑'); return;
    }
    this.lastTick=time;
    if(this.inFlight) return;
    if(!c.servoAvailable(this.channel)) { this.cancel('状态失效或底盘未锁定，已取消'); return; }
    const old=c.servoStatus.pulses[this.channel], motion=c.servoMotion;
    if(this.sent && motion && c.servoMotionAt>=this.lastWrite && c.servoStatusAt>=this.lastWrite &&
       motion.targets[this.channel]===this.target && old===this.target && !(motion.movingMask&(1<<this.channel))) {
      this.cancel(`指令已到 ${this.target}μs，实际位置请观察`); return;
    }
    // Wait for first PWM telemetry; ACK is not a position/PWM status update.
    if(this.sent && !old) return;
    if(time-this.lastWrite<150) return;
    this.inFlight=true; this.lastWrite=time; const target=this.target;
    try {
      await c.servo(this.channel,target,this,this.sent?0:this.referenceUs);
      this.sent=true; this.referenceUs=0;
    } catch(e) { this.cancel(`跟随中止：${e.message}`); }
    finally { this.inFlight=false; this.changed(); }
  }
}
