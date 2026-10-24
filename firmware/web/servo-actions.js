import {validateProgram,validatePulse} from './motion-library.js';
/* Non-blocking graph: independent servo trajectories share one serialized
 * BLE queue. Completion refers to PWM telemetry, never measured shaft angle. */
export class ServoActions{
  constructor(controller,{changed=()=>{},onTarget=()=>{},now=()=>controller.now(),speedUsS=Infinity}={}){if(!(speedUsS>0))throw Error('舵机速度必须大于0');Object.assign(this,{controller,changed,onTarget,now,speedUsS});this.active=false;this.busy=false;this.message='尚未运行动作';this.steps=[];}
  start(steps,references){
    if(this.active||this.controller.servoOwner)throw Error('已有舵机动作在运行');
    if(!this.controller.concurrentMotion?.())this.controller.assertStopped(false,false);
    this.steps=validateProgram(steps).map(s=>({...s,startedAt:null,doneAt:null,reachedAt:null,lastWrite:-Infinity,targetSince:Infinity,sent:false,rampAt:null,lastTarget:null}));
    this.references=[...references];this.started=this.lastTick=this.now();this.session=this.controller.state.session;this.generation=this.controller.generation;this.stopGeneration=this.controller.stopGeneration;
    this.run=(this.run||0)+1;this.pausedAt=null;this.retryAt=0;
    this.active=true;this.controller.servoOwner=this;this.message='持续跟随设定目标（PWM指令到位判断）';this.changed();
  }
  cancel(reason='动作已停止，保持最后指令'){this.run=(this.run||0)+1;this.active=false;this.pausedAt=null;if(this.controller.servoOwner===this)this.controller.servoOwner=null;this.message=reason;this.changed();}
  // 仅通信恢复可续跑；STOP、继电器关闭、手动接管仍调用 cancel。
  // 不保存到浏览器/Flash，刷新页面或主控释放 PWM 后不会自动回中。
  pause(reason='连接暂时中断'){
    if(!this.active)return;
    this.run=(this.run||0)+1;this.pausedAt??=this.now();this.message=`保留目标，等待恢复：${reason}`;this.changed();
  }
  retarget(events,references){
    if(!this.active)return this.start(events.map(e=>({channel:e.channel,pulseUs:e.pulseUs,trigger:{after:null,event:'start',delayMs:0}})),references);
    for(const e of events){validatePulse(e.channel,e.pulseUs);const old=this.steps.find(s=>s.channel===e.channel&&s.doneAt===null);
      if(old){old.pulseUs=e.pulseUs;old.reachedAt=null;old.lastWrite=-Infinity;old.targetSince=Infinity;old.rampAt=null;old.lastTarget=null;}
      else{const s=validateProgram([{channel:e.channel,pulseUs:e.pulseUs,trigger:{after:null,event:'start',delayMs:0}}])[0];this.steps.push({...s,startedAt:null,doneAt:null,reachedAt:null,lastWrite:-Infinity,targetSince:Infinity,sent:false,rampAt:null,lastTarget:null});}
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
    if(this.pausedAt!==null){
      if(!c.state.connected||!c.state.uart||!c.servoStatus||!c.servoMotion||
         c.servoStatus.session!==c.state.session||time-c.servoStatusAt>=500||time-c.servoMotionAt>=500)return;
      // 重连只接续仍有 PWM 的通道。上电静默/释放后不能假定旧参考就是轴位置。
      if(this.steps.some(s=>s.doneAt===null&&!c.servoStatus.pulses[s.channel])){this.cancel('PWM已释放或主控已重启，请确认起点后重新按功能键');return;}
      const delay=time-this.pausedAt;this.started+=delay;
      for(const s of this.steps){for(const key of ['startedAt','doneAt','crossedAt'])if(s[key]!=null)s[key]+=delay;s.reachedAt=null;s.lastWrite=-Infinity;s.targetSince=Infinity;s.rampAt=null;s.lastTarget=null;}
      this.session=c.state.session;this.generation=c.generation;this.stopGeneration=c.stopGeneration;this.pausedAt=null;
    }
    if(c.generation!==this.generation||c.state.session!==this.session||c.stopGeneration!==this.stopGeneration){this.cancel('动作中断：控制已被其他操作接管');return;}
    this.lastTick=time;
    if(!c.state.connected||!c.state.uart){this.message='保留目标，等待通信恢复';this.changed();return;}
    if(time<this.retryAt)return;
    const run=this.run;this.busy=true;this.message='持续跟随设定目标';
    try{
      for(const s of this.steps){
        if(s.doneAt!==null)continue;
        const time=this.now();
        const pulse=c.servoStatus?.pulses[s.channel]||0,motion=c.servoMotion;
        if(s.startedAt===null){
          if(!this.eligible(s,time))continue;
          if(this.steps.some(other=>other!==s&&other.channel===s.channel&&other.startedAt!==null&&other.doneAt===null))throw Error('同一路舵机的积木发生时间重叠');
          s.initialPulse=pulse||this.references[s.channel];
        }
        // 相同目标的150ms续租不能刷新“新目标时间”，否则180ms错峰回传
        // 永远追不上 lastWrite，小臂到位判定不断重置，大臂一直等不到启动。
        const sampleAt=Math.min(c.servoStatusAt,c.servoMotionAt),fresh=time-sampleAt<500;
        if(s.sent&&fresh&&sampleAt>=s.targetSince&&pulse===s.pulseUs&&motion?.targets[s.channel]===s.pulseUs&&!(motion.movingMask&(1<<s.channel))){
          s.reachedAt??=sampleAt;if(sampleAt-s.reachedAt>=s.settleMs)s.doneAt=time;
          continue;
        }else s.reachedAt=null;
        if(time-s.lastWrite<150)continue;
        if(!c.servoAvailable(s.channel)){
          this.message=`保留目标，恢复后继续：${c.servoUnavailableReason?.(s.channel)||'等待舵机状态'}`;continue;
        }
        if(s.sent&&!pulse){this.cancel('PWM已释放，请确认起点后重新按功能键');return;}
        const ref=pulse?0:this.references[s.channel];
        const sentAt=this.now(),goal=s.pulseUs;
        let target=goal;
        if(Number.isFinite(this.speedUsS)){
          // 快捷动作单独放慢，手动滑块/固件速度不变。小目标由C3原有
          // 加减速轨迹平滑跟随；断线期间不积攒路程，恢复后不跳到终点。
          const restart=s.rampAt===null||sentAt-s.rampAt>500;
          const base=restart?(pulse||s.initialPulse):s.lastTarget;
          const dt=s.rampAt===null?0:Math.min(150,Math.max(0,sentAt-s.rampAt));
          const step=Math.floor(this.speedUsS*dt/1000);
          target=base+Math.sign(goal-base)*Math.min(Math.abs(goal-base),step);
        }
        s.lastWrite=sentAt;
        await c.servo(s.channel,target,this,ref);
        if(!this.active||this.run!==run)return;
        if(!s.sent){s.startedAt=this.now();this.onTarget({channel:s.channel,pulseUs:s.pulseUs,referenceUs:ref});}
        s.sent=true;if(s.pulseUs===goal){s.rampAt=sentAt;s.lastTarget=target;}
        if(s.pulseUs===goal&&target===goal&&s.targetSince===Infinity)s.targetSince=sentAt;
      }
      if(this.steps.every(s=>s.doneAt!==null))this.cancel('动作完成：三线舵机无实测到位反馈');
    }catch(e){
      if(this.active&&this.run===run){
        if(/超时|过期|等待|连接|会话|忙|停稳|中断|状态失效/.test(e.message)){
          this.retryAt=this.now()+250;this.message=`保留目标，正在重试：${e.message}`;
        }else {this.cancel(e.message);c.stop('舵机动作中止').catch(()=>{});}
      }
    }
    finally{this.busy=false;this.changed();}
  }
}
