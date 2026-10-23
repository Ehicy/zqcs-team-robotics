import {validateProgram,validatePulse} from './motion-library.js';
/* Non-blocking graph: independent servo trajectories share one serialized
 * BLE queue. Completion refers to PWM telemetry, never measured shaft angle. */
export class ServoActions{
  constructor(controller,{changed=()=>{},onTarget=()=>{},now=()=>controller.now()}={}){Object.assign(this,{controller,changed,onTarget,now});this.active=false;this.busy=false;this.message='尚未运行动作';this.steps=[];}
  start(steps,references){
    if(this.active||this.controller.servoOwner)throw Error('已有舵机动作在运行');
    this.controller.assertStopped();
    this.steps=validateProgram(steps).map(s=>({...s,startedAt:null,doneAt:null,reachedAt:null,lastWrite:-Infinity,sent:false}));
    this.references=[...references];this.started=this.lastTick=this.now();this.session=this.controller.state.session;this.generation=this.controller.generation;this.stopGeneration=this.controller.stopGeneration;
    this.active=true;this.controller.servoOwner=this;this.message='动作运行中（PWM指令到位判断）';this.changed();
  }
  cancel(reason='动作已停止，保持最后指令'){this.active=false;if(this.controller.servoOwner===this)this.controller.servoOwner=null;this.message=reason;this.changed();}
  retarget(events,references){
    if(!this.active)return this.start(events.map(e=>({channel:e.channel,pulseUs:e.pulseUs,trigger:{after:null,event:'start',delayMs:0}})),references);
    for(const e of events){validatePulse(e.channel,e.pulseUs);const old=this.steps.find(s=>s.channel===e.channel&&s.doneAt===null);
      if(old){old.pulseUs=e.pulseUs;old.reachedAt=null;old.lastWrite=-Infinity;}
      else{const s=validateProgram([{channel:e.channel,pulseUs:e.pulseUs,trigger:{after:null,event:'start',delayMs:0}}])[0];this.steps.push({...s,startedAt:null,doneAt:null,reachedAt:null,lastWrite:-Infinity,sent:false});}
    }
  }
  eligible(s,time){
    const t=s.trigger;if(t.after===null)return time-this.started>=t.delayMs;
    const parent=this.steps[t.after];let at=t.event==='done'?parent.doneAt:t.event==='start'?parent.startedAt:s.crossedAt;
    if(t.event==='position'&&parent.startedAt!==null&&at==null){const p=this.controller.servoStatus?.pulses[parent.channel],threshold=t.thresholdUs;
      if(p&&(parent.pulseUs>=parent.initialPulse?p>=threshold:p<=threshold))at=s.crossedAt=time;}
    return at!=null&&time-at>=t.delayMs;
  }
  async tick(){
    if(!this.active||this.busy)return;const c=this.controller,time=this.now();
    if(c.generation!==this.generation||c.state.session!==this.session||c.stopGeneration!==this.stopGeneration||time-this.lastTick>400||time-this.started>600000){this.cancel('动作中断：会话、调度或时限改变');return;}
    this.lastTick=time;
    if(!c.state.connected||!c.state.uart){this.cancel('动作中断：连接断开');return;}
    this.busy=true;
    try{
      for(const s of this.steps){
        if(s.doneAt!==null)continue;
        const pulse=c.servoStatus?.pulses[s.channel]||0,motion=c.servoMotion;
        if(s.startedAt===null){
          if(!this.eligible(s,time))continue;
          if(this.steps.some(other=>other!==s&&other.channel===s.channel&&other.startedAt!==null&&other.doneAt===null))throw Error('同一路舵机的积木发生时间重叠');
          s.initialPulse=pulse||this.references[s.channel];
        }
        if(s.sent&&pulse===s.pulseUs&&motion?.targets[s.channel]===s.pulseUs&&!(motion.movingMask&(1<<s.channel))&&c.servoStatusAt>=s.lastWrite&&c.servoMotionAt>=s.lastWrite){
          s.reachedAt??=time;if(time-s.reachedAt>=s.settleMs){s.doneAt=time;continue;}
        }else s.reachedAt=null;
        if(time-s.lastWrite<150||s.sent&&!pulse)continue;
        if(!c.servoAvailable(s.channel)){
          if(c.state.configBusy)continue;throw Error('舵机状态过期或底盘仍在运动');
        }
        const ref=pulse?0:this.references[s.channel];
        await c.servo(s.channel,s.pulseUs,this,ref);
        if(!this.active)return;
        if(!s.sent){s.startedAt=this.now();this.onTarget({channel:s.channel,pulseUs:s.pulseUs,referenceUs:ref});}
        s.sent=true;s.lastWrite=this.now();
      }
      if(this.steps.every(s=>s.doneAt!==null))this.cancel('动作完成：三线舵机无实测到位反馈');
    }catch(e){this.cancel(e.message);c.stop('舵机动作中止').catch(()=>{});}
    finally{this.busy=false;this.changed();}
  }
}
