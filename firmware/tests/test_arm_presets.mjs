import test from 'node:test';
import assert from 'node:assert/strict';
import {ARM_PRESETS as CURRENT_ARM_PRESETS,programFromBlocks,WorkshopStore,KeyTimeline,validateBlocks} from '../web/keyboard-workshop.js';
import {ServoActions} from '../web/servo-actions.js';

// Historical dependent programs exercise the generic scheduler.
const ARM_PRESETS=[
  {key:'f',name:'初始位',blocks:[{channel:1,atMs:0,angle:180},{channel:0,atMs:500,angle:116,trigger:{after:0,event:'start',delayMs:500}}]},
  {key:'g',name:'抓取',blocks:[{channel:1,atMs:0,angle:123.4},{channel:0,atMs:500,angle:83.6,trigger:{after:0,event:'start',delayMs:500}}]},
  {key:'h',name:'回收放置',blocks:[{channel:1,atMs:0,angle:151.7},{channel:0,atMs:0,angle:235,trigger:{after:0,event:'done',delayMs:0}},{channel:1,atMs:0,angle:180,trigger:{after:0,event:'done',delayMs:0}}]},
  // J is a standalone small-arm hook; it never schedules the large arm or door.
  {key:'j',name:'小臂回勾',blocks:[{channel:1,atMs:0,angle:151.7}]}
];

function fixture(){
  let time=0;const writes=[],c={state:{session:7,connected:true,uart:true},generation:1,stopGeneration:0,servoOwner:null,
    now:()=>time,assertStopped(){},servoAvailable:()=>true,servoStatus:{pulses:[1359,2500,0]},servoStatusAt:0,
    servoMotion:{targets:[1359,2500,0],movingMask:0},servoMotionAt:0,
    async servo(channel,pulseUs,owner,referenceUs){writes.push({at:time,channel,pulseUs,referenceUs});},async stop(){}};
  const a=new ServoActions(c);
  return {a,c,writes,step:async(ms=25)=>{time+=ms;await a.tick();},
    report(channel,pulse,moving=false){c.servoStatus.pulses[channel]=pulse;c.servoMotion.targets[channel]=pulse;c.servoMotion.movingMask=moving?1<<channel:0;c.servoStatusAt=c.servoMotionAt=time;}};
}
test('F/G preset sends small arm first, waits 500ms from accepted target, preserves decimal angles',async()=>{
  for(const [key,small,big] of [['f',2500,1359],['g',1871,1119]]){
    const f=fixture(),preset=ARM_PRESETS.find(p=>p.key===key);
    f.a.start(programFromBlocks(preset.blocks),[1359,2500,1500]);await f.a.tick();
    assert.equal(f.writes[0].channel,1);assert.equal(f.writes[0].pulseUs,small);
    for(let i=0;i<19;i++)await f.step();
    assert(!f.writes.some(w=>w.channel===0));await f.step();
    const target=f.writes.find(w=>w.channel===0);assert.equal(target.at,500);assert.equal(target.pulseUs,big);
    assert(!f.writes.some(w=>w.channel===2));
  }
});
test('H hooks first, waits for PWM arrival/stability, then both placement targets, no final re-hook',async()=>{
  const f=fixture();f.a.start(programFromBlocks(ARM_PRESETS[2].blocks),[1359,2500,1500]);await f.a.tick();
  assert.equal(f.writes[0].channel,1);assert.equal(f.writes[0].pulseUs,2186);
  for(let i=0;i<40;i++)await f.step();
  assert(f.writes.every(w=>w.channel===1&&w.pulseUs===2186),'ACK and elapsed time alone do not start placement');
  // Keep returning new settled PWM samples, rather than inventing shaft feedback.
  for(let i=0;i<20&&!f.writes.some(w=>w.channel===0);i++){f.report(1,2186);await f.step();}
  const big=f.writes.find(w=>w.channel===0),small=f.writes.find(w=>w.channel===1&&w.pulseUs===2500);
  assert(big&&small);assert.equal(big.at,small.at);assert.equal(big.pulseUs,2241);
  const after=f.writes.indexOf(big);assert(!f.writes.slice(after).some(w=>w.pulseUs===2186));
  assert(!f.writes.some(w=>w.channel===2));assert.equal(f.a.steps.length,3);
});
test('preset installation and v3 export preserve dependencies/nodes; dispatching uses a program, not simultaneous catch-up',()=>{
  let raw=null;const storage={getItem:()=>raw,setItem:(k,v)=>{raw=v;}};
  const lib=new WorkshopStore(storage),nodes=structuredClone(lib.data.nodes);lib.installArmPresets();
  assert.equal(lib.data.version,3);assert.deepEqual(lib.data.nodes,nodes);assert.equal(lib.data.bindings.length,3);
  assert.deepEqual(lib.data.bindings.map(({id,...row})=>row),CURRENT_ARM_PRESETS);

  const loaded=new WorkshopStore(storage);assert.deepEqual(loaded.data,lib.data);
  const plans=[],flat=[],timeline=new KeyTimeline({now:()=>1000,send:e=>flat.push(e),sendProgram:p=>plans.push(p)});
  timeline.trigger(ARM_PRESETS[2],0);timeline.tick();
  assert.equal(plans.length,1);assert.equal(flat.length,0);
  assert.deepEqual(plans[0].slice(1).map(s=>s.trigger.after),[0,0]);
  assert.throws(()=>validateBlocks([{channel:0,atMs:0,angle:100,trigger:{after:0,event:'done',delayMs:0}}]));
  timeline.trigger(loaded.data.bindings[0],3000);timeline.cancel();timeline.tick();assert.equal(plans.length,1);
});

test('H completes with staggered 180ms PWM/motion telemetry, despite 150ms same-target renewals',async()=>{
  const f=fixture();f.a.start(programFromBlocks(ARM_PRESETS[2].blocks),[1359,2500,1500]);
  await f.a.tick();
  for(let ms=25;ms<=4000&&f.a.active;ms+=25){
    const last=new Map(f.writes.map(w=>[w.channel,w.pulseUs]));
    if(ms%175===0){for(const [ch,p] of last)f.c.servoStatus.pulses[ch]=p;f.c.servoStatusAt=ms-25;}
    if(ms%175===50){for(const [ch,p] of last)f.c.servoMotion.targets[ch]=p;f.c.servoMotionAt=ms-25;}
    await f.step();
  }
  assert(f.writes.some(w=>w.channel===0&&w.pulseUs===2241),'large arm must follow the hook');
  assert.equal(f.a.active,false,'same-target renewals must not restart the settle timer');
  assert.match(f.a.message,/完成/);
});

test('temporary telemetry loss retains targets and resumes without emitting STOP',async()=>{
  const f=fixture();let ready=true,stops=0;
  f.c.servoAvailable=()=>ready;f.c.servoUnavailableReason=()=>ready?'':'舵机 PWM 状态回传已超过500ms';f.c.stop=async()=>{stops++;};
  f.a.start(programFromBlocks(ARM_PRESETS[1].blocks),[1359,2500,1500]);await f.a.tick();
  ready=false;for(let i=0;i<40;i++)await f.step();
  assert(f.a.active);assert.equal(stops,0);assert.equal(f.writes.length,1);
  ready=true;f.report(1,2000,true);await f.step();
  assert(f.writes.some(w=>w.channel===0&&w.pulseUs===1119));
  f.a.cancel('用户停止');const count=f.writes.length;await f.step();assert.equal(f.writes.length,count);
});

test('explicit recoverable pause resumes the same targets in a new session; STOP cancels permanently',async()=>{
  const f=fixture();f.a.start(programFromBlocks(ARM_PRESETS[1].blocks),[1359,2500,1500]);await f.a.tick();
  f.a.pause('串口会话恢复');f.c.state.session=8;f.c.stopGeneration++;
  f.c.servoStatus.session=8;f.c.servoMotion.session=8;
  f.report(1,2200,true);await f.step();
  assert.equal(f.a.active,true);assert.equal(f.a.session,8);
  for(let i=0;i<22;i++){f.report(1,2000,true);await f.step();}
  assert(f.writes.some(w=>w.channel===0&&w.pulseUs===1119));
  f.a.cancel('用户停止');const count=f.writes.length;
  f.c.state.session=9;f.a.pause('迟到的恢复回调');await f.step();assert.equal(f.writes.length,count);
});

test('reconnect never substitutes a saved reference after PWM was released or power cycled',async()=>{
  const f=fixture();f.a.start(programFromBlocks(ARM_PRESETS[1].blocks),[1359,2500,1500]);await f.a.tick();
  f.a.pause('蓝牙恢复');f.c.servoStatus.session=f.c.state.session;f.c.servoStatus.pulses=[0,0,0];
  const count=f.writes.length;await f.step();assert.equal(f.a.active,false);assert.equal(f.writes.length,count);
  assert.match(f.a.message,/PWM已释放/);
});

test('late acknowledgement from an interrupted run cannot change a newly requested program',async()=>{
  const f=fixture();let resolve;f.c.servo=()=>new Promise(r=>{resolve=r;});
  f.a.start(programFromBlocks(ARM_PRESETS[1].blocks),[1359,2500,1500]);const pending=f.a.tick();
  f.a.cancel('停止');f.a.start(programFromBlocks(ARM_PRESETS[0].blocks),[1359,2500,1500]);
  resolve();await pending;assert(f.a.steps.every(s=>!s.sent));assert.equal(f.a.steps[0].pulseUs,2500);
});


test('J independently hooks the small arm; retries after stale telemetry without large-arm or door output',async()=>{
  const f=fixture(),preset=ARM_PRESETS.find(p=>p.key==='j');
  assert.deepEqual(preset.blocks,[{channel:1,atMs:0,angle:151.7}]);
  const timeline=new KeyTimeline({now:f.c.now,send:events=>f.a.retarget(events,[1359,2500,1500])});
  timeline.trigger(preset);timeline.tick();await f.a.tick();
  assert.deepEqual(f.writes.map(w=>[w.channel,w.pulseUs]),[[1,2186]]);
  f.c.servoAvailable=()=>false;for(let i=0;i<40;i++)await f.step();
  assert(f.a.active);f.c.servoAvailable=()=>true;f.report(1,2300,true);await f.step();
  assert(f.writes.length>1);
  for(let i=0;i<20&&f.a.active;i++){f.report(1,2186);await f.step();}
  assert.equal(f.a.active,false);assert.match(f.a.message,/完成/);
  assert(f.writes.every(w=>w.channel===1&&w.pulseUs===2186));
});


test('shortcut 500us/s ramp is independent per channel, completes final targets without premature completion',async()=>{
  const f=fixture();f.a.speedUsS=500;f.c.servoStatus.pulses=[1000,2200,1500];f.c.servoMotion.targets=[1000,2200,1500];
  f.a.start([0,1,2].map(channel=>({channel,pulseUs:[2000,1200,1700][channel]})),[1000,2200,1500]);
  await f.a.tick();assert.deepEqual(f.writes.map(w=>w.pulseUs),[1000,2200,1500]);assert(f.a.active);
  for(let i=0;i<200&&f.a.active;i++){
    for(const w of f.writes.slice(-3))f.report(w.channel,w.pulseUs);
    await f.step();
  }
  assert.equal(f.a.active,false);assert.match(f.a.message,/完成/);
  for(let ch=0;ch<3;ch++){
    const sent=f.writes.filter(w=>w.channel===ch);assert.equal(sent.at(-1).pulseUs,[2000,1200,1700][ch]);
    for(let i=1;i<sent.length;i++)assert(Math.abs(sent[i].pulseUs-sent[i-1].pulseUs)<=(sent[i].at-sent[i-1].at)*.5);
  }
});

test('slow shortcuts never catch up a communication gap in one jump and STOP stops ramp updates',async()=>{
  const f=fixture();f.a.speedUsS=500;
  f.a.start([{channel:1,pulseUs:1500}],[1359,2500,1500]);await f.a.tick();
  f.report(1,2500);await f.step(150);assert.equal(f.writes.at(-1).pulseUs,2425);
  f.c.servoAvailable=()=>false;await f.step(2000);assert(f.a.active);
  f.c.servoAvailable=()=>true;f.report(1,2450);await f.step(25);
  assert.equal(f.writes.at(-1).pulseUs,2375,'resume no more than 75us from returned PWM');
  f.a.retarget([{channel:1,pulseUs:2500}],[1359,2500,1500]);f.report(1,2400);await f.step(25);
  assert.equal(f.writes.at(-1).pulseUs,2400,'new target starts from returned PWM, not stale ramp position');
  f.a.cancel('用户停止');const count=f.writes.length;await f.step(1000);assert.equal(f.writes.length,count);
});


test('slow H still finishes hook before placement and keeps the final angles',async()=>{
  const f=fixture();f.a.speedUsS=500;
  f.a.start(programFromBlocks(ARM_PRESETS.find(p=>p.key==='h').blocks),[1359,2500,1500]);
  await f.a.tick();
  for(let i=0;i<300&&f.a.active;i++){
    for(const [channel,pulse] of new Map(f.writes.map(w=>[w.channel,w.pulseUs])))f.report(channel,pulse);
    await f.step();
  }
  assert.equal(f.a.active,false);assert.match(f.a.message,/完成/);
  const hook=f.writes.find(w=>w.channel===1&&w.pulseUs===2186);
  const big=f.writes.find(w=>w.channel===0);assert(big.at>=hook.at+300);
  assert.equal(f.writes.filter(w=>w.channel===0).at(-1).pulseUs,2241);
  assert.equal(f.writes.filter(w=>w.channel===1).at(-1).pulseUs,2500);
  assert(!f.writes.some(w=>w.channel===2));
});


test('200us/s shortcut profile caps each channel at 30us per 150ms and preserves H ordering',async()=>{
  const f=fixture();f.a.speedUsS=200;
  f.a.start(programFromBlocks(ARM_PRESETS.find(p=>p.key==='h').blocks),[1359,2500,1500]);await f.a.tick();
  for(let i=0;i<600&&f.a.active;i++){
    for(const [channel,pulse] of new Map(f.writes.map(w=>[w.channel,w.pulseUs])))f.report(channel,pulse);
    await f.step();
  }
  assert.equal(f.a.active,false);assert.match(f.a.message,/完成/);
  const hook=f.writes.find(w=>w.channel===1&&w.pulseUs===2186),big=f.writes.find(w=>w.channel===0);
  assert(big.at>=hook.at+300,'placement still waits for completed hook');
  for(const ch of [0,1]){
    const sent=f.writes.filter(w=>w.channel===ch);
    for(let i=1;i<sent.length;i++){
      assert(Math.abs(sent[i].pulseUs-sent[i-1].pulseUs)<=30);
      assert(Math.abs(sent[i].pulseUs-sent[i-1].pulseUs)<=(sent[i].at-sent[i-1].at)*.2);
    }
  }
  assert.equal(f.writes.filter(w=>w.channel===0).at(-1).pulseUs,2241);
  assert.equal(f.writes.filter(w=>w.channel===1).at(-1).pulseUs,2500);
  assert(!f.writes.some(w=>w.channel===2));
});


test('50us/s shortcut is four times slower than 200us/s and retains final targets on every channel',async()=>{
  const f=fixture();f.a.speedUsS=50;
  f.c.servoStatus.pulses=[1000,2200,1500];f.c.servoMotion.targets=[1000,2200,1500];
  f.a.start([0,1,2].map(channel=>({channel,pulseUs:[1200,2000,1700][channel]})),[1000,2200,1500]);
  await f.a.tick();
  for(let i=0;i<500&&f.a.active;i++){
    for(const [ch,p] of new Map(f.writes.map(w=>[w.channel,w.pulseUs])))f.report(ch,p);
    await f.step();
  }
  assert.equal(f.a.active,false);assert.match(f.a.message,/完成/);
  for(const ch of [0,1,2]){
    const sent=f.writes.filter(w=>w.channel===ch);
    assert.equal(sent.at(-1).pulseUs,[1200,2000,1700][ch]);
    assert(sent.at(-1).at>=4000,'200us travel must take at least 4 seconds');
    for(let i=1;i<sent.length;i++){
      assert(Math.abs(sent[i].pulseUs-sent[i-1].pulseUs)<=7);
      assert(Math.abs(sent[i].pulseUs-sent[i-1].pulseUs)<=(sent[i].at-sent[i-1].at)*.05);
    }
  }
});
