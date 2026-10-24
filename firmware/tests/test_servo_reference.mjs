import test from 'node:test';
import assert from 'node:assert/strict';
import {parseHTML} from 'linkedom';
import {RemoteController,OP} from '../web/protocol.js';
import {installServoPanel} from '../web/servo-panel.js';
const flush=()=>new Promise(resolve=>setImmediate(resolve));
test('capture initial arm pose stores exact PWM in C3 without targets, release, or limit edits',async()=>{
  const {document,window}=parseHTML('<html><body data-hardware-profile="c3-pa"><div id="servoPanel"></div></body></html>');
  const prior={document:globalThis.document,window:globalThis.window,setInterval:globalThis.setInterval};
  Object.assign(globalThis,{document,window,setInterval:()=>0});
  try{
    let time=100;const writes=[],c=new RemoteController({now:()=>time,deviceStorage:true});
    const generation=c.attach(async bytes=>{
      writes.push(bytes.slice());c.ingest({kind:3,wheel:254,result:0,session:71,sourceSequence:new DataView(bytes.buffer).getUint32(8,true)},generation);
    });
    c.ingest({kind:0,session:71,uart:true,ready:true,pidReady:true,armed:false,active:false,calibratedMask:15},generation);
    c.ingest({kind:5,session:71,available:true,slew:true,armFullRange:true,positionMask:7,pulses:[1359,2500,0]},generation);
    c.ingest({kind:6,session:71,targets:[1359,2500,0],movingMask:0,errorMask:0},generation);
    const settings=(channel,referenceUs,revision=13)=>c.ingest({kind:9,session:71,revision,channel,minUs:500,maxUs:2500,referenceUs,calibrated:false,reversed:false,doorClosed:500,doorOpen:2500},generation);
    const refs=[1574,1500,1500];refs.forEach((v,i)=>settings(i,v));
    const panel=installServoPanel(c);
    for(const [i,pulse] of [[0,1359],[1,2500]]){
      const button=document.getElementById(`servoCaptureReference${i}`);
      assert(!button.disabled);assert(document.getElementById(`servoReference${i}`).disabled,'no need to release holding torque to record reference');
      button.dispatchEvent(new window.Event('click'));await flush();
      assert.equal(writes.length,i+1);const b=writes.at(-1),v=new DataView(b.buffer);
      assert.equal(b[1],OP.SERVO_SETTINGS);assert.equal(b[12],i);
      assert.equal(v.getUint16(14,true),500);assert.equal(v.getUint16(16,true),2500);
      assert.equal(v.getUint16(18,true),pulse);assert.equal(b[13],0,'do not turn recording a reference into a claim that mechanical limits were calibrated');
      assert.deepEqual(c.servoStatus.pulses,[1359,2500,0]);
      refs[i]=pulse;refs.forEach((v,ch)=>settings(ch,v,14+i));panel.render();
      assert.equal(Number(document.getElementById(`servoReference${i}`).value),(pulse-500)*(i===0?270:180)/2000);
      assert.match(document.getElementById('servoMessage').textContent,/未发送运动指令/);
    }
    assert(document.getElementById('servoCaptureReference2').disabled);
    await assert.rejects(c.captureServoReference(2),/尚未启用/);
    c.servoMotion.movingMask=1;await assert.rejects(c.captureServoReference(0),/停止变化/);
    c.servoMotion.movingMask=0;time+=501;await assert.rejects(c.captureServoReference(0));
    assert.equal(writes.length,2,'released/stale/moving states never generate a reference or motion command');
    c.disconnect();
  }finally{Object.assign(globalThis,prior);}
});
