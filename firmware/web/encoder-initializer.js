import {OP} from './protocol.js?v=softpower-v8';
const delta=(a,b)=>(a-b)|0;
export class EncoderInitializer {
  constructor({controller,telemetry,foreground=()=>true,takeover=()=>{},completed=()=>{},changed=()=>{},error=()=>{}}){
    Object.assign(this,{controller,telemetry,foreground,takeover,completed,changed,error});this.job=null;this.message='点击后20%输出前后各450ms，实际位移不固定；请留出空间。';
  }
  get active(){return !!this.job;}
  start(){
    const c=this.controller;
    if(this.active)return;
    if(!this.foreground()||!c.state.connected||!c.state.uart||!c.state.pwmSupported||!c.servoStatus?.encoderReset)throw Error('请连接支持直控和编码器清零的双板');
    if(!c.deviceSettings.calibration())throw Error('等待 C3 参数回读');
    if(c.servoOwner||c.servoMotion?.movingMask||c.state.configBusy||c.state.active||c.state.armed||c.state.remoteArmed)throw Error('请先停止当前动作或参数写入');
    this.takeover();
    const now=c.now();
    this.job={stage:'prepare',at:now,lastTick:now,lastSend:-Infinity,generation:c.generation,session:c.state.session,stopGeneration:c.stopGeneration,lastEncoderAt:-Infinity,forwardMoved:0,reverseMoved:0};
    this.stopLeg('准备微动：先停止');
  }
  stopLeg(note){
    const j=this.job,c=this.controller;if(!j)return;
    // STOP advances the firmware session by exactly this amount. Accept only
    // this one expected transition; unrelated STOP/link/reboot aborts the job.
    let next=(c.state.session+0x9e3779b9)>>>0;if(!next)next=1;
    const promise=c.stop(note);j.stopGeneration=c.stopGeneration;
    j.awaitStop={next,at:c.now()};
    Promise.resolve(promise).catch(e=>{if(this.job===j)this.fail(e.message);});
    this.message=note;this.changed();
  }
  cancel(note='初始化已取消',sendStop=true){
    if(!this.job)return;this.job=null;this.message=note;
    if(sendStop)this.controller.stop(note).catch(this.error);this.changed();
  }
  fail(note){this.cancel(`${note}；未判定初始化成功`,true);}
  send(throttle,force=false){
    const j=this.job,c=this.controller,now=c.now();if(!j||!force&&now-j.lastSend<50)return;
    j.lastSend=now;
    c.queue.offer(c.packet(OP.PWM,{throttle,steer:0,limit:200,ttl:200}).bytes,{kind:'drive',expiresAt:now+100})
      .catch(e=>{if(this.job===j)this.fail(e.message);});
  }
  tick(){
    const j=this.job,c=this.controller;if(!j)return;
    const now=c.now(),gap=now-j.lastTick;j.lastTick=now;
    if(!this.foreground()||!c.state.connected||!c.state.uart||c.generation!==j.generation||gap>=150){this.cancel('连接或页面状态改变，初始化已中止');return;}
    if(j.awaitStop){
      if(c.state.session===j.awaitStop.next&&c.state.reason===4&&c.stopGeneration===j.stopGeneration+1){
        j.session=c.state.session;j.stopGeneration=c.stopGeneration;j.awaitStop=null;
        j.at=j.stableAt=now;j.lastEncoderAt=-Infinity;
      }else if(c.state.session===j.session&&c.stopGeneration===j.stopGeneration&&now-j.awaitStop.at<1500)return;
      else {this.fail('停止应答不匹配或超时');return;}
    }
    if(c.state.session!==j.session||c.stopGeneration!==j.stopGeneration){this.cancel('手动操作或会话改变，初始化已中止');return;}
    if(now-c.mainStatusAt>=500){this.fail('主控状态过期');return;}
    const {encoder:e,encoderAt:at}=this.telemetry();
    if(!e||e.session!==j.session||e.validMask!==15||now-at>=400){
      if(['prepare','coast','settle'].includes(j.stage)&&now-j.at<400)return;
      this.fail('缺少四轮新鲜编码器数据');return;
    }
    const fresh=at!==j.lastEncoderAt;if(fresh)j.lastEncoderAt=at;
    if(j.stage==='prepare'){
      if(now-j.at<200||c.state.active)return;
      j.stage='forward';j.at=now;j.baseline=[...e.counts];this.message='低输出向前微动';this.changed();
    }
    if(j.stage==='forward'||j.stage==='reverse'){
      if(fresh)for(let i=0;i<4;i++)if(Math.abs(delta(e.counts[i],j.baseline[i]))>=2)j[j.stage==='forward'?'forwardMoved':'reverseMoved']|=1<<i;
      if(now-j.at>=450){
        const forward=j.stage==='forward';j.stage=forward?'coast':'settle';j.at=now;j.stableAt=now;j.stableCounts=[...e.counts];
        this.stopLeg(forward?'前段结束，停止250ms':'微动结束，观察静止计数');return;
      }
      this.send(j.stage==='forward'?1000:-1000);return;
    }
    if(j.stage==='coast'){
      if(now-j.at>=250&&!c.state.active){j.stage='reverse';j.at=now;j.baseline=[...e.counts];this.message='低输出向后微动';this.changed();}return;
    }
    if(j.stage==='settle'||j.stage==='verify'){
      if(now-j.at>3000){this.fail('静止计数仍漂移或清零未确认');return;}
      if(fresh){
        if(j.stage==='verify')j.verifySamples++;
        const moved=e.counts.some((v,i)=>Math.abs(delta(v,j.stableCounts[i]))>2);
        if(moved||c.state.active){j.stableAt=now;j.stableCounts=[...e.counts];}
      }
      if(c.state.active||now-j.stableAt<(j.stage==='verify'?400:800))return;
      if(j.stage==='verify'){
        if(!fresh||j.verifySamples<3||e.counts.some(v=>Math.abs(v)>2))return;
        this.job=null;this.message='初始化完成：四轮有微动反馈，清零后静止计数通过本次检查';this.completed();this.changed();return;
      }
      if(j.forwardMoved!==15||j.reverseMoved!==15){this.fail('有轮未检测到前后微动，未自动清零；可检查接线/输出或单独点清零');return;}
      if(!c.deviceSettings.calibration()||!c.servoStatus?.encoderReset)return;
      j.stage='reset';j.at=now;this.message='静止计数通过检查，正在清零';this.changed();
      c.resetEncoders().then(()=>{
        if(this.job!==j)return;
        j.stage='verify';j.at=j.stableAt=c.now();j.stableCounts=[0,0,0,0];j.verifySamples=0;j.lastEncoderAt=this.telemetry().encoderAt;
        this.message='清零已应答，等待新的零点样本';this.changed();
      }).catch(e=>{if(this.job===j)this.fail(e.message);});
    }
    if(j.stage==='reset'&&now-j.at>5000)this.fail('清零应答超时');
  }
}
