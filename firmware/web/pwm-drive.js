import { OP } from './protocol.js?v=portable-20261024';
// Turning (including W+A/W+D) has its own setting. Do not silently clamp it
// to the straight-driving slider when the operator selects a higher value.
export function manualPwmLimit(intent,travelLimit,turnLimit){
  return intent.steer?turnLimit:travelLimit;
}
/* Manual duty control owns only fresh held input. No ARM, calibration,
 * PID transaction, feedback-health gate, or fault-clear operation. */
export class PwmDrive {
  constructor({ controller, foreground=()=>true, error=()=>{}, changed=()=>{}, turnLimit=()=>1000 }) {
    Object.assign(this,{controller,foreground,error,changed,turnLimit});
    this.job=null; this.intent={throttle:0,steer:0}; this.phase='IDLE'; this.lastSend=-Infinity;
  }
  request(throttle,steer) {
    if(![throttle,steer].every(v=>Number.isInteger(v)&&v>=-1000&&v<=1000)) throw new Error('方向输入越界');
    if(!throttle&&!steer){
      const had=!!this.job;this.cancel('已松开，底盘停止',false);
      if(had)this.controller.stopDrive('已松开，底盘停止').catch(this.error);
      return true;
    }
    const c=this.controller;
    if(!this.foreground()||!c.state.connected||!c.state.uart||!c.state.session) return false;
    if(!c.state.pwmSupported){ this.error(new Error('请烧入 C3 直控新版固件')); return false; }
    this.intent={throttle,steer};
    if(!this.job) {
      this.job={session:c.state.session,generation:c.generation,stopGeneration:c.stopGeneration,lastTick:c.now()};
      c.state.note='按住方向行驶，松开停止';
    }
    this.phase='DRIVING'; this.tick(); this.changed(); return true;
  }
  cancel(note='已停止',sendStop=true) {
    const had=!!this.job;
    this.job=null; this.intent={throttle:0,steer:0}; this.phase='IDLE'; this.lastSend=-Infinity;
    if(sendStop&&had) this.controller.stop(note).catch(this.error);
    this.changed();
  }
  tick() {
    const c=this.controller,j=this.job;
    if(!j)return;
    if(!this.foreground()||!c.state.connected||!c.state.uart||j.session!==c.state.session||
       j.generation!==c.generation||j.stopGeneration!==c.stopGeneration){this.cancel();return;}
    const now=c.now(),gap=now-j.lastTick; j.lastTick=now;
    // Ignore scheduling gaps BEFORE this press, but never renew an old held
    // command after an interruption. Check before offering another PWM packet.
    if(gap>=150){this.cancel('按住期间页面调度中断，请重新按方向键');return;}
    if(now-this.lastSend<50)return; this.lastSend=now;
    c.queue.offer(c.packet(OP.PWM,{...this.intent,limit:manualPwmLimit(this.intent,c.state.limit,this.turnLimit()),ttl:200}).bytes,
      {kind:'drive',expiresAt:now+100}).catch(()=>{});
  }
}
