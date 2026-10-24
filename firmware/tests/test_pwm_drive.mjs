import test from 'node:test';
import assert from 'node:assert/strict';
import { PwmDrive } from '../web/pwm-drive.js';
import { RemoteController, OP, decodeStatus } from '../web/protocol.js';
import { intentFromKeys } from '../web/dashboard-input.js';
function fixture(){
  let now=0,front=true; const writes=[];
  const c=new RemoteController({now:()=>now});
  c.attach(async bytes=>writes.push(new Uint8Array(bytes)));
  Object.assign(c.state,{session:1,uart:true,pwmSupported:true,ready:false,pidReady:false,fault:true,calibratedMask:0});
  const drive=new PwmDrive({controller:c,foreground:()=>front}); c.setLimit(1000);
  return {c,drive,writes,time:v=>now=v,background:()=>front=false};
}
const flush=()=>new Promise(r=>setImmediate(r));
test('fresh PWM intent sends 100% without ARM or configuration, regardless of encoder/PID readiness',async()=>{
  const f=fixture(); assert.equal(f.writes.length,0);
  assert.equal(f.drive.request(1000,0),true); await flush();
  assert.equal(f.writes.length,1); const v=new DataView(f.writes[0].buffer);
  assert.equal(v.getUint8(1),OP.PWM); assert.equal(v.getUint16(16,true),1000);
  f.time(55); f.drive.tick(); await flush(); assert.equal(f.writes.length,2);
  f.drive.request(0,0); await flush(); assert.equal(f.writes.at(-1)[1],OP.STOP);
  const count=f.writes.length; f.time(120); f.drive.tick(); await flush(); assert.equal(f.writes.length,count);
});
test('session change, foreground loss and disconnect never replay an old held sample',async()=>{
  for(const stop of [f=>f.c.state.session++,f=>f.background(),f=>f.c.disconnect()]){
    const f=fixture(); f.drive.request(1000,0); await flush(); stop(f); f.time(55); f.drive.tick(); await flush();
    assert.equal(f.drive.job,null); assert.equal(f.writes.filter(b=>b[1]===OP.PWM).length,1);
  }
});
test('main status advertises direct PWM explicitly',()=>{
  const p=new Uint8Array(20); p[0]=1;p[2]=65;
  assert.equal(decodeStatus(p).pwmSupported,true);
});

test('W/A/D wire axes are unchanged by missing or abnormal wheel feedback',async()=>{
  for(let mask=0;mask<16;mask++) for(const [key,throttle,steer] of [['w',1000,0],['a',0,-1000],['d',0,1000]]){
    const f=fixture();f.c.setLimit(200);f.drive.turnLimit=()=>200;
    Object.assign(f.c.state,{speedValidMask:mask,faultCode:2,calibratedMask:0});
    f.c.deviceSettings.wheels=[null,null,null,null];
    const intent=intentFromKeys([key]);
    assert(f.drive.request(intent.throttle,intent.steer));await flush();
    assert.equal(f.writes.length,1);
    const v=new DataView(f.writes[0].buffer);
    assert.equal(v.getUint8(1),OP.PWM);
    assert.equal(v.getInt16(12,true),throttle);
    assert.equal(v.getInt16(14,true),steer);
    assert.equal(v.getUint16(16,true),200);
  }
});


test('separate manual ceilings: W/S 23%, A/D and combined turning 15%, preserves tank signs',async()=>{
  for(const keys of [['w'],['s'],['a'],['d'],['w','a'],['s','d']]){
    const f=fixture();f.c.setLimit(230);f.drive.turnLimit=()=>150;
    const intent=intentFromKeys(keys);f.drive.request(intent.throttle,intent.steer);await flush();
    let v=new DataView(f.writes.at(-1).buffer);
    assert.equal(v.getUint16(16,true),intent.steer?150:230);
    assert.equal(v.getInt16(12,true),intent.throttle);assert.equal(v.getInt16(14,true),intent.steer);
    f.c.setLimit(100);f.time(55);f.drive.tick();await flush();
    v=new DataView(f.writes.at(-1).buffer);assert.equal(v.getUint16(16,true),intent.steer?150:100,'turning retains its independent setting');
  }
});
