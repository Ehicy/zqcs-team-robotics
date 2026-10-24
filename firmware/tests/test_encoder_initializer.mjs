import test from 'node:test';
import assert from 'node:assert/strict';
import {EncoderInitializer} from '../web/encoder-initializer.js';
import {RemoteController} from '../web/protocol.js';
function rig(){
 let time=0,at=-1,counts=[0,0,0,0],resetCount=0,completed=0,visible=true,noise=false,noMotion=false,newSamples=true;
 const writes=[];
 const c=new RemoteController({now:()=>time});c.attach(async bytes=>{writes.push(new Uint8Array(bytes));});
 let sess=17;
 function status(){c.ingest({kind:0,session:sess,uart:true,ready:true,pidReady:true,active:false,armed:false,pwmSupported:true,calibratedMask:15,reason:4},c.generation);c.servoStatus={encoderReset:true};}
 status();c.deviceSettings.calibration=()=>[{}, {}, {}, {}];
 const realStop=c.stop.bind(c);
 c.stop=note=>{const p=realStop(note);sess=(sess+0x9e3779b9)>>>0||1;return p;};
 c.resetEncoders=async()=>{resetCount++;counts=[0,0,0,0];};
 const x=new EncoderInitializer({controller:c,telemetry:()=>({encoder:{counts:[...counts],validMask:15,session:c.state.session},encoderAt:at}),foreground:()=>visible,completed:()=>completed++});
 async function tick(){
  time+=25;status();
  if(newSamples&&time%100===0){at=time;if(!noMotion&&['forward','reverse'].includes(x.job?.stage))counts=counts.map(v=>v+(x.job.stage==='forward'?3:-3));if(noise&&x.job?.stage==='settle')counts[0]+=3;}
  x.tick();await new Promise(r=>setImmediate(r));
 }
 return {c,x,tick,writes,set noMotion(v){noMotion=v;},set noise(v){noise=v;},set visible(v){visible=v;},set samples(v){newSamples=v;},get resetCount(){return resetCount;},get completed(){return completed;}};
}
test('one-click init with real RemoteController: expected STOP session rotations, both directions, fresh zero ACK samples',async()=>{
 const r=rig();assert.equal(r.writes.length,0);r.x.start();
 for(let i=0;i<240&&r.x.active;i++)await r.tick();
 assert.equal(r.completed,1,r.x.message);assert.equal(r.resetCount,1);
 const pwm=r.writes.filter(p=>p[1]===11).map(p=>new DataView(p.buffer,p.byteOffset,p.byteLength));
 assert(pwm.some(v=>v.getInt16(12,true)===1000));assert(pwm.some(v=>v.getInt16(12,true)===-1000));
 for(const v of pwm){assert.equal(v.getUint16(16,true),200);assert.equal(v.getUint16(18,true),200);}
 const n=r.writes.length;for(let i=0;i<30;i++)await r.tick();assert.equal(r.writes.length,n,'complete never restarts motion');
});
for(const mode of ['noMotion','noise'])test(`${mode}: no false success or reset`,async()=>{
 const r=rig();r[mode]=true;r.x.start();for(let i=0;i<240&&r.x.active;i++)await r.tick();assert.equal(r.completed,0);assert.equal(r.resetCount,0);assert(!r.x.active);
});
test('hidden page / external STOP cancels and does not resume',async()=>{
 for(const action of ['hidden','stop']){const r=rig();r.x.start();for(let i=0;i<16;i++)await r.tick();if(action==='hidden')r.visible=false;else await r.c.stop('external');await r.tick();assert(!r.x.active);assert.equal(r.completed,0);}
});
test('ACK alone does not complete: must see post-ACK zero samples',async()=>{
 const r=rig();r.x.start();for(let i=0;i<200&&r.x.job?.stage!=='verify';i++)await r.tick();assert.equal(r.resetCount,1);r.samples=false;
 for(let i=0;i<30;i++)await r.tick();assert.equal(r.completed,0);assert(!r.x.active);
});
