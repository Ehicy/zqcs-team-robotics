import test from 'node:test';
import assert from 'node:assert/strict';
import { PwmDrive } from '../web/pwm-drive.js';
import { RemoteController, OP, decodeStatus } from '../web/protocol.js';
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
