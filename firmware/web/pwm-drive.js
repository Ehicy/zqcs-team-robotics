import { OP } from './protocol.js?v=bootquiet-v5';
/* Manual duty control owns only fresh held input. No ARM, calibration,
 * PID transaction, feedback-health gate, or fault-clear operation. */
export class PwmDrive {
  constructor({ controller, foreground=()=>true, error=()=>{}, changed=()=>{} }) {
    Object.assign(this,{controller,foreground,error,changed});
    this.job=null; this.intent={throttle:0,steer:0}; this.phase='IDLE'; this.lastSend=-Infinity;
  }
  request(throttle,steer) {
    if(![throttle,steer].every(v=>Number.isInteger(v)&&v>=-1000&&v<=1000)) throw new Error('方向输入越界');
    if(!throttle&&!steer){ this.cancel('已松开，停止'); return true; }
    const c=this.controller;
    if(!this.foreground()||!c.state.connected||!c.state.uart||!c.state.session) return false;
    if(!c.state.pwmSupported){ this.error(new Error('请烧入 C3 直控新版固件')); return false; }
    this.intent={throttle,steer};
    if(!this.job) this.job={session:c.state.session,generation:c.generation,stopGeneration:c.stopGeneration};
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
    const now=c.now(); if(now-this.lastSend<50)return; this.lastSend=now;
    c.queue.offer(c.packet(OP.PWM,{...this.intent,limit:c.state.limit,ttl:200}).bytes,
      {kind:'drive',expiresAt:now+100}).catch(()=>{});
  }
}
