import {validateTrace} from './motion-library.js';
import {mixWheels} from './dashboard-input.js';
const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
export class TeachRecorder{
  constructor(){this.active=false;this.trace=null;}
  start({now,calibration,trackWidthMm}){this.started=now;this.trace={version:1,calibration,trackWidthMm,samples:[],servos:[]};this.active=true;}
  sample({now,odometry,counts,intent,limit,force=false}){
    if(!this.active)return;
    const t=now-this.started,last=this.trace.samples.at(-1);
    if(t>1800000||this.trace.samples.length>=20000){this.active=false;return;}
    if(last&&(t<=last.t||!force&&t-last.t<50))return;
    const [left,right]=odometry?.sideTravelMm||[last?.left||0,last?.right||0];
    this.trace.samples.push({t,left,right,counts:counts?[...counts]:null,estimated:!odometry?.observable||odometry.estimated,missingSideMask:odometry?.missingSideMask??3,
      throttle:intent.throttle,steer:intent.steer,limit});
  }
  servo(event,now){if(this.active){const t=now-this.started,previous=this.trace.servos.at(-1),e={...event,t};
    if(previous&&previous.channel===e.channel&&t-previous.t<300)this.trace.servos[this.trace.servos.length-1]=e;
    else if(this.trace.servos.length<2000)this.trace.servos.push(e);
  }}
  finish(sample){if(sample)this.sample({...sample,force:true});this.active=false;return validateTrace(this.trace);}
}
/* Relative left/right wheel-travel follower with recorded PWM feedforward.
 * Encoder corrections are always evaluated; missing sides use feedforward,
 * explicitly marked estimated. This is not a map/path planner or FTC port. */
export class TraceReplay{
  constructor({drive,stop,servo,servoBusy=()=>false,canServo=()=>true,concurrentMotion=()=>false,now=()=>performance.now(),changed=()=>{}}){Object.assign(this,{drive,stop,servo,servoBusy,canServo,concurrentMotion,now,changed});this.active=false;this.message='未复刻';}
  start(trace,{odometry,session,generation,kp=1.5,toleranceMm=8,scale=1}={}){
    if(this.active)throw Error('已经在复刻');
    this.trace=validateTrace(trace);
    if(!Number.isFinite(scale)||scale<=0||scale>1||!Number.isFinite(kp)||kp<0||kp>20||!Number.isFinite(toleranceMm)||toleranceMm<1||toleranceMm>100)throw Error('复刻参数无效');
    this.baseline=[...(odometry?.sideTravelMm||[0,0])];this.recordOrigin=[this.trace.samples[0].left,this.trace.samples[0].right];
    this.session=session;this.generation=generation;this.kp=kp;this.toleranceMm=toleranceMm;this.scale=scale;
    this.clock=0;this.lastTick=this.started=this.now();this.nextServo=0;this.index=0;this.finishAt=null;this.active=true;this.error=[0,0];this.sideOffsets=[0,0];this.previousMask=0;this.hadGap=false;this.estimated=false;this.message='相对编码器复刻中';this.changed();
  }
  cancel(reason='复刻停止',sendStop=true){const had=this.active;this.active=false;this.message=reason;if(had&&sendStop)this.stop();this.changed();}
  async tick({odometry,session,generation,connected=true}={}){
    if(!this.active||this.busy)return;
    const time=this.now(),dt=time-this.lastTick;this.lastTick=time;
    if(!connected||session!==this.session||generation!==this.generation||dt>400||time-this.started>1800000){this.cancel('复刻中断：连接、会话或调度改变');return;}
    this.busy=true;
    try{
      const events=this.trace.servos;
      if(this.nextServo<events.length&&events[this.nextServo].t<=this.clock){
        if(!this.concurrentMotion())this.drive(0,0);
        if(!this.canServo())return;
        const group=[],t=events[this.nextServo].t;
        while(this.nextServo<events.length&&events[this.nextServo].t<=t+50)group.push(events[this.nextServo++]);
        await this.servo([...new Map(group.map(e=>[e.channel,e])).values()]);
      }
      if(this.servoBusy()&&!this.concurrentMotion()){
        this.drive(0,0);
        const nextMotion=this.trace.samples.find(s=>s.t>=this.clock&&(s.throttle||s.steer));
        this.clock=Math.min(nextMotion?.t??this.trace.samples.at(-1).t,this.clock+dt*this.scale);
        this.message='机械臂复现中 · 底盘等待';return;
      }
      const samples=this.trace.samples,last=samples.at(-1);
      while(this.index+1<samples.length&&samples[this.index+1].t<=this.clock)this.index++;
      const a=samples[this.index],b=samples[Math.min(samples.length-1,this.index+1)],f=b.t===a.t?0:clamp((this.clock-a.t)/(b.t-a.t),0,1);
      const reference=[a.left+(b.left-a.left)*f-this.recordOrigin[0],a.right+(b.right-a.right)*f-this.recordOrigin[1]];
      const travel=odometry?.sideTravelMm||this.baseline;
      const mask=(odometry?.missingSideMask??3)|(a.missingSideMask??(a.estimated?3:0))|(b.missingSideMask??(b.estimated?3:0));
      const recovered=this.previousMask&~mask;
      for(let i=0;i<2;i++)if(recovered&(1<<i))this.sideOffsets[i]=reference[i]-(travel[i]-this.baseline[i]);
      this.previousMask=mask;this.hadGap||=!!mask;
      this.error=reference.map((v,i)=>v-(travel[i]-this.baseline[i])-this.sideOffsets[i]);
      this.estimated=this.hadGap||!!odometry?.estimated||!!a.estimated;
      const ff=mixWheels(a,a.limit),confidence=[Math.max(odomOrZero(odometry,1),odomOrZero(odometry,3)),Math.max(odomOrZero(odometry,0),odomOrZero(odometry,2))];
      const correction=this.error.map((e,i)=>(mask&(1<<i))?0:clamp(e*this.kp*confidence[i],-300,300));
      const lag=Math.max(0,...this.error.map((e,i)=>(mask&(1<<i))?0:e*Math.sign(i===0?ff.left:ff.right)));
      const clockScale=lag>100?Math.max(0.25,100/lag):1;
      let left=clamp(ff.left*this.scale*clockScale+correction[0],-1000,1000),right=clamp(ff.right*this.scale*clockScale+correction[1],-1000,1000);
      if(this.clock>=last.t){
        if(this.servoBusy()) {this.drive(0,0);this.message='底盘时序结束，等待机械臂完成';return;}
        if(this.nextServo>=events.length&&(mask||this.error.every(e=>Math.abs(e)<=this.toleranceMm))){this.cancel(mask||odometry?.pathComplete===false||this.estimated?'复刻时序结束（有估计区段，位置未验证）':'复刻完成（编码器相对里程到达）');return;}
        left=clamp(correction[0],-300,300);right=clamp(correction[1],-300,300);
        this.finishAt??=time;if(time-this.finishAt>10000){this.cancel('复刻结束：末端误差未在10秒内收敛');return;}
      }
      this.drive(Math.round(left),Math.round(right));
      // Slow the reference clock when tracking lags; never invent position.
      this.clock=Math.min(last.t,this.clock+dt*this.scale*clockScale);
      this.message=`${this.estimated?'降级估计':'编码器纠偏'} · ${(this.clock/1000).toFixed(1)}s · 左/右误差 ${this.error.map(e=>e.toFixed(0)).join('/')}mm`;
      this.changed();
    }catch(e){this.cancel('复刻中止：'+e.message);}finally{this.busy=false;}
  }
}
function odomOrZero(odom,i){return odom?.confidence?.[i]??(odom?.observable?1:0);}
