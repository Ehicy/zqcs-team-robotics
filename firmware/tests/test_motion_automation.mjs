import test from 'node:test';
import assert from 'node:assert/strict';
import {parseHTML} from 'linkedom';
import {MotionLibrary,pulseFromAngle,angleFromPulse,validateProgram,validateTrace} from '../web/motion-library.js';
import {ServoActions} from '../web/servo-actions.js';
import {installAutomation} from '../web/automation-panel.js';
import {TraceReplay} from '../web/teach-replay.js';
import {encodeCommand,decodeStatus,OP} from '../web/protocol.js';
const storage=()=>{const m=new Map();return {getItem:k=>m.get(k)??null,setItem:(k,v)=>m.set(k,v)};};
test('270-degree named positions and editable three-channel states persist without executing',()=>{
  assert.equal(pulseFromAngle(0,0),500);assert.equal(pulseFromAngle(0,135),1500);assert.equal(pulseFromAngle(0,270),2500);assert.equal(angleFromPulse(0,2000),202.5);
  const s=storage(),lib=new MotionLibrary(s);lib.save('positions','取物低位',{channel:0,pulseUs:600});lib.save('poses','收拢',{pulses:[1350,1400,1500]});
  lib.save('positions','取物低位',{channel:0,pulseUs:700});const again=new MotionLibrary(s);assert.equal(again.data.positions.length,1);assert.equal(again.data.positions[0].pulseUs,700);
  const exported=again.export(),other=new MotionLibrary(storage());other.import(exported);assert.deepEqual(other.data,again.data);
  other.remove('poses','收拢');assert.equal(other.data.poses.length,0);
});
test('malformed records and storage failure never replace the last valid library',()=>{
  const lib=new MotionLibrary(storage());assert.throws(()=>lib.save('positions','',{channel:0,pulseUs:1500}));assert.throws(()=>lib.save('positions','bad',{channel:1,pulseUs:500}));
  lib.save('positions','正常',{channel:0,pulseUs:1500});const before=lib.export();assert.throws(()=>lib.import('{"version":99}'));assert.equal(lib.export(),before);
  const broken=new MotionLibrary({getItem:()=>null,setItem:()=>{throw Error('quota');}});assert.throws(()=>broken.save('positions','test',{channel:0,pulseUs:1500}));assert.equal(broken.data.positions.length,0);
});
test('action graph rejects forward references, cycles and invalid angle windows',()=>{
  assert.throws(()=>validateProgram([{channel:0,pulseUs:1500,trigger:{after:0,event:'done',delayMs:0}}]));
  assert.throws(()=>validateProgram([{channel:1,pulseUs:500}]));
  assert.equal(validateProgram([{channel:0,pulseUs:500}])[0].trigger.after,null);
});
function actionFixture(){
  let time=100;const writes=[],c={state:{connected:true,uart:true,session:71},generation:1,stopGeneration:0,servoOwner:null,now:()=>time,
    assertStopped(){},servoAvailable:()=>true,servoStatus:{pulses:[1500,1500,1500]},servoStatusAt:100,
    servoMotion:{targets:[1500,1500,1500],movingMask:0},servoMotionAt:100,
    async servo(ch,pulse,owner,ref){assert.equal(owner,c.servoOwner);writes.push({ch,pulse,ref});},async stop(){c.stopGeneration++;}};
  const actions=new ServoActions(c);
  return {c,actions,writes,step:async(dt=50)=>{time+=dt;await actions.tick();},set:({pulses,targets,moving=0})=>{if(pulses)c.servoStatus.pulses=pulses;if(targets)c.servoMotion.targets=targets;c.servoMotion.movingMask=moving;c.servoStatusAt=c.servoMotionAt=time;}};
}
const block=(channel,pulseUs,after=null,event='start',delayMs=0)=>({channel,pulseUs,settleMs:0,trigger:{after,event,delayMs,thresholdUs:1550}});
test('sequential blocks require fresh PWM target arrival, not only command ACK',async()=>{
  const f=actionFixture();f.actions.start([block(0,1600),block(1,1550,0,'done',100)],[1500,1500,1500]);await f.actions.tick();
  assert.equal(f.writes.length,1);await f.step();assert.equal(f.writes.length,1);
  f.set({pulses:[1600,1500,1500],targets:[1600,1500,1500]});await f.step();assert.equal(f.writes.length,1);
  await f.step(100);assert.equal(f.writes.at(-1).ch,1);
});
test('parallel/start-delay and position-threshold triggers work without waiting for the parent endpoint',async()=>{
  const f=actionFixture();f.actions.start([block(0,1600),block(1,1550,0,'start',100),block(2,1800,0,'position')],[1500,1500,1500]);await f.actions.tick();
  f.set({pulses:[1550,1500,1500],targets:[1600,1500,1500],moving:1});await f.step(50);assert.equal(f.writes.at(-1).ch,2);
  await f.step(50);assert(f.writes.some(w=>w.ch===1));assert.equal(f.actions.steps[0].doneAt,null);
});
test('program cancellation and session change never renew an old target',async()=>{
  const f=actionFixture();f.actions.start([block(0,1600)],[1500,1500,1500]);await f.actions.tick();f.c.state.session++;await f.step();assert.equal(f.actions.active,false);const count=f.writes.length;await f.step(150);assert.equal(f.writes.length,count);
});
test('recorded servo targets can retarget one channel and start another without forcing a new reference',async()=>{
  const f=actionFixture();f.actions.start([block(0,1600)],[1500,1500,1500]);await f.actions.tick();
  f.actions.retarget([{channel:0,pulseUs:1700},{channel:1,pulseUs:1550}],[1500,1500,1500]);await f.step(150);
  assert(f.writes.some(w=>w.ch===0&&w.pulse===1700&&w.ref===0));assert(f.writes.some(w=>w.ch===1&&w.pulse===1550));assert.equal(f.c.servoOwner,f.actions);
});
test('replay coalesces duplicate servo targets and does not double-count recorded idle while arms move',async()=>{
  let time=0,busy=false;const events=[],drives=[];
  const replay=new TraceReplay({now:()=>time,drive:(l,r)=>drives.push([l,r]),stop:()=>{},servoBusy:()=>busy,servo:async e=>{events.push(e);busy=true;}});
  const point=(t,throttle,left=0)=>({t,left,right:left,throttle,steer:0,limit:200,missingSideMask:0});
  replay.start({calibration:Array.from({length:4},()=>({cpr:1000,diameterMm:65,direction:1})),trackWidthMm:165,samples:[point(0,0),point(600,1000),point(1000,0,80)],servos:[{t:0,channel:0,pulseUs:1500},{t:25,channel:0,pulseUs:1600},{t:300,channel:1,pulseUs:1550}]},{session:71,generation:1});
  const status={session:71,generation:1,odometry:{sideTravelMm:[0,0],observable:true,missingSideMask:0,confidence:[1,1,1,1]}};
  await replay.tick(status);assert.equal(events[0].length,1);assert.equal(events[0][0].pulseUs,1600);
  for(let i=0;i<7;i++){time+=100;await replay.tick(status);}
  assert.equal(events.length,2);assert.equal(replay.clock,600);assert(drives.every(v=>v[0]===0&&v[1]===0));
  busy=false;time+=100;await replay.tick(status);assert.equal(drives.at(-1)[0],200);
});
test('full arm pulse range is advertised explicitly and old firmware cannot report out-of-range pulses',()=>{
  for(const pulseUs of [500,2500])assert.equal(encodeCommand(OP.SERVO_TARGET,{session:71,sequence:1,channel:0,pulseUs,referenceUs:1500}).length,20);
  const b=new Uint8Array(20),v=new DataView(b.buffer);b[0]=1;b[1]=5;b[2]=1;b[3]=159;v.setUint32(4,71,true);
  v.setUint16(8,500,true);[1000,2000,25].forEach((p,i)=>v.setUint16(14+2*i,p,true));
  assert.equal(decodeStatus(b).armFullRange,true);b[3]=143;assert.throws(()=>decodeStatus(b));
});
test('offline panel saves position, edits combination states and builds named blocks without touching hardware',async()=>{
  const {window,document}=parseHTML(`<html><body><div id="workshop"></div><input id="mapTrack" value="165">${[0,1,2].map(ch=>`<input id="servoReference${ch}" value="${ch===0?135:ch===1?90:45}"><input id="servoAngle${ch}" value="${ch===0?135:ch===1?90:45}">`).join('')}</body></html>`);
  Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option'))?.value||'';},set(value){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===String(value));}});
  const old={document:globalThis.document,window:globalThis.window,localStorage:globalThis.localStorage};Object.assign(globalThis,{document,window,localStorage:storage()});
  try{
    let calls=0;const c={state:{connected:false,session:0},now:()=>0,async servo(){calls++;}};
    const panel=installAutomation({root:document.getElementById('workshop'),controller:c,telemetry:()=>({encoder:null,encoderAt:-Infinity}),calibration:()=>[],intent:()=>({throttle:0,steer:0})});
    const $=id=>document.getElementById(id);$('positionName').value='<任意中文位置>';$('positionAngle').value='202.5';await $('savePosition').onclick();assert.equal(panel.library.data.positions[0].pulseUs,2000);assert.equal($('positionList').textContent,'<任意中文位置>');assert.equal(document.querySelector('#positionList img'),null);
    $('poseName').value='收拢';await $('savePose').onclick();await $('poseToBlocks').onclick();assert.equal($('actionBlocks').children.length,3);
    $('programName').value='依次收拢';await $('saveProgram').onclick();assert.equal(panel.library.data.programs[0].steps[1].trigger.after,0);
    assert.equal(calls,0);assert.equal(panel.isRunning(),false);
  }finally{Object.assign(globalThis,old);}
});
