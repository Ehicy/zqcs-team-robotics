import {servoBounds} from './protocol.js?v=portable-20261024';
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
    const [min,max]=servoBounds(this.channel,this.controller.servoStatus);
    if(!Number.isInteger(target) || target<min || target>max) throw new Error(`脉宽须为${min}–${max}μs整数`);
  }
  start(target, referenceUs=0) {
    const c=this.controller;
    if(this.active || this.inFlight) throw new Error('上一条舵机操作尚未结束');
    // 舵机跟随只依赖本路回读，不让缺失的车轮/PID包阻止滑块发送目标。
    this.validateTarget(target); if(!c.concurrentMotion?.())c.assertStopped(false,false);
    if(!c.servoAvailable(this.channel)) throw new Error(c.servoUnavailableReason?.(this.channel)||'舵机状态尚未就绪');
    if(c.servoOwner) throw new Error('其他舵机操作正在运行');
    if(!c.servoStatus.pulses[this.channel]) this.validateTarget(referenceUs);
    else if(referenceUs) throw new Error('已启用时不能重设起点');
    this.referenceUs=referenceUs; this.sent=false;
    this.target=target; this.session=c.state.session; this.generation=c.generation;
    this.stopGeneration=c.stopGeneration; this.lastTick=this.started=this.now(); this.lastWrite=-Infinity;this.targetSince=Infinity;
    this.active=true; c.servoOwner=this; this.message=`${this.label}限速移向 ${target}μs`; this.changed(); c.notify();
  }
  targetTo(target, referenceUs=0) {
    this.validateTarget(target);
    if(!this.active) return this.start(target,referenceUs);
    if(this.target!==target)this.targetSince=Infinity;
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
       time-this.lastTick>=400 || time-this.started>=60000 || this.sent && time-this.lastWrite>=400) {
      this.cancel('跟随已取消（停止、断联、会话变化或超时）；不会续跑'); return;
    }
    this.lastTick=time;
    if(this.inFlight) return;
    if(!c.servoAvailable(this.channel)) { this.cancel(`跟随暂停：${c.servoUnavailableReason?.(this.channel)||'状态失效或底盘正在控制'}`); return; }
    const old=c.servoStatus.pulses[this.channel], motion=c.servoMotion;
    if(this.sent && motion && c.servoMotionAt>=this.targetSince && c.servoStatusAt>=this.targetSince &&
       motion.targets[this.channel]===this.target && old===this.target && !(motion.movingMask&(1<<this.channel))) {
      this.cancel(`指令已到 ${this.target}μs，实际位置请观察`); return;
    }
    // Wait for first PWM telemetry; ACK is not a position/PWM status update.
    if(this.sent && !old) return;
    if(time-this.lastWrite<150) return;
    this.inFlight=true; this.lastWrite=time; const target=this.target;
    try {
      await c.servo(this.channel,target,this,this.sent?0:this.referenceUs);
      // Same-target renewals must not move the telemetry completion boundary.
      // Otherwise staggered PWM/trajectory packets can never both catch up.
      if(this.target===target&&this.targetSince===Infinity)this.targetSince=time;
      this.sent=true; this.referenceUs=0;
    } catch(e) { this.cancel(`跟随中止：${e.message}`); }
    finally { this.inFlight=false; this.changed(); }
  }
}
