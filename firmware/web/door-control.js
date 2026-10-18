/* Operator-started stepped door movement. The ESP stays the regular bridge.
 * This is open-loop position PWM, not an angle sensor or a limit switch. */
export class DoorControl {
  constructor(controller, { now = () => controller.now(), changed = () => {}, channel = 2 } = {}) {
    if(!Number.isInteger(channel) || channel<0 || channel>2) throw new Error('舵机通道无效');
    this.channel=channel; this.label=channel===2?'门':`舵机${channel+1}`;
    this.controller=controller; this.now=now; this.changed=changed;
    this.active=false; this.inFlight=false; this.message='先启用第三路 1500μs，再微调并校准门端点';
  }
  start(target, confirmed = false) {
    const c=this.controller;
    if(this.active || this.inFlight) throw new Error('门还在移动或等待上一条回执');
    if(!confirmed) throw new Error('请确认门行程无阻挡，并已校准开关端点');
    this.validateTarget(target);
    c.assertStopped();
    if(!c.servoAvailable(this.channel) || !c.servoStatus.pulses[this.channel]) throw new Error('这一路未就绪，请先启用1500μs');
    if(c.servoOwner) throw new Error('其他舵机操作正在运行');
    this.target=target; this.session=c.state.session; this.generation=c.generation;
    this.stopGeneration=c.stopGeneration; this.lastTick=this.lastWrite=this.started=this.now();
    this.active=true; c.servoOwner=this; this.message=`${this.label}分步移向 ${target}μs`; this.changed(); c.notify();
  }
  validateTarget(target) {
    const min=this.channel===2?1000:1300, max=this.channel===2?2000:1700;
    if(!Number.isInteger(target) || target<min || target>max) throw new Error(`脉宽须为${min}–${max}μs整数`);
  }
  targetTo(target, confirmed = false) {
    this.validateTarget(target);
    if(!confirmed) throw new Error('请先确认供电、支撑与行程');
    if(!this.active) return this.start(target,confirmed);
    this.target=target; this.message=`${this.label}目标更新为 ${target}μs`; this.changed();
  }
  cancel(message='门移动已停止；保持最后脉宽') {
    this.active=false; this.message=message;
    if(this.controller.servoOwner===this) this.controller.servoOwner=null;
    this.changed(); this.controller.notify();
  }
  async tick() {
    if(!this.active) return;
    const c=this.controller, time=this.now();
    if(c.generation!==this.generation || c.state.session!==this.session ||
       c.stopGeneration!==this.stopGeneration || time-this.lastTick>=400 || time-this.started>=15000) {
      this.cancel('门移动已取消（停止、断联、会话变化或调度超时）；不会续跑'); return;
    }
    this.lastTick=time;
    if(this.inFlight) return;
    if(!c.servoAvailable(this.channel)) { this.cancel(`${this.label}状态失效或底盘未锁定，已取消`); return; }
    const old=c.servoStatus.pulses[this.channel];
    if(!old) { this.cancel('PWM已释放；不会自动重新启用'); return; }
    if(old===this.target) { this.cancel(`${this.label}目标 ${this.target}μs 已确认，实际位置请观察`); return; }
    if(time-this.lastWrite<150) return;
    const next=old+Math.sign(this.target-old)*Math.min(25,Math.abs(this.target-old));
    this.inFlight=true; this.lastWrite=time;
    try { await c.servo(this.channel,next,this); }
    catch(e) { this.cancel(`${this.label}移动已中止：${e.message}`); }
    finally { this.inFlight=false; this.changed(); }
  }
}
