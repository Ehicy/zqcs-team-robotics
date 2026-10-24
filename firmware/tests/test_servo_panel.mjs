import test from 'node:test';
import assert from 'node:assert/strict';
import {parseHTML} from 'linkedom';
import {RemoteController,OP} from '../web/protocol.js';
import {installServoPanel} from '../web/servo-panel.js';
const flush=()=>new Promise(resolve=>setImmediate(resolve));
test('connected servo panel directly accepts slider targets with chosen cold reference and coalesces dragging',async()=>{
  const {document,window}=parseHTML('<div id="servoPanel"></div>');
  const prior={document:globalThis.document,window:globalThis.window,setInterval:globalThis.setInterval};
  Object.assign(globalThis,{document,window,setInterval:()=>0});
  try {
    let time=100;const writes=[],c=new RemoteController({now:()=>time});let generation;
    generation=c.attach(async bytes=>{
      writes.push(bytes.slice());const v=new DataView(bytes.buffer);
      c.ingest({kind:3,wheel:254,result:0,session:71,sourceSequence:v.getUint32(8,true)},generation);
    });
    c.ingest({kind:0,session:71,uart:true,ready:true,pidReady:true,phase:2,armed:false,active:false,
      fault:false,faultCode:0,reason:1,calibratedMask:15,left:0,right:0},generation);
    const pulses=[0,0,0],targets=[0,0,0];
    c.ingest({kind:5,session:71,available:true,positionMask:7,slew:true,pulses},generation);
    c.ingest({kind:6,session:71,targets,movingMask:0,errorMask:0},generation);
    const panel=installServoPanel(c);
    assert.equal(document.querySelectorAll('input[type="checkbox"]').length,0);
    const reference=document.getElementById('servoReference0'),slider=document.getElementById('servoAngle0');
    assert.equal(slider.disabled,false);
    assert.equal(reference.getAttribute('min'),'108');assert.equal(reference.getAttribute('max'),'162');
    assert.equal(reference.value,'135');assert.equal(slider.value,'135');
    assert.equal(slider.getAttribute('step'),'0.5');
    assert.match(document.querySelector('legend').textContent,/270°位置型/);
    reference.value='108';reference.dispatchEvent(new window.Event('input'));
    slider.value='148.5';slider.dispatchEvent(new window.Event('input'));await flush();
    assert.equal(writes.length,1);assert.equal(writes[0][1],OP.SERVO_TARGET);
    assert.equal(new DataView(writes[0].buffer).getUint16(16,true),1300);
    assert.equal(new DataView(writes[0].buffer).getUint16(14,true),1600);
    assert.equal(c.servoStatus.pulses[0],0);assert.equal(panel.runners[0].active,true);
    c.ingest({kind:5,session:71,available:true,positionMask:7,slew:true,pulses:[1300,0,0]},generation);
    slider.value='162';slider.dispatchEvent(new window.Event('input'));
    slider.value='121.5';slider.dispatchEvent(new window.Event('input'));await flush();
    time+=150;await panel.runners[0].tick();
    assert.equal(writes.length,2);assert.equal(new DataView(writes[1].buffer).getUint16(14,true),1400);
    assert.equal(new DataView(writes[1].buffer).getUint16(16,true),0);
    window.dispatchEvent(new window.Event('blur'));await flush();
    assert.equal(writes.at(-1)[1],OP.STOP);assert.equal(panel.runners[0].active,false);
    const count=writes.length;
    for(const [pulse,angle] of [[1300,108],[1500,135],[1700,162]]) {
      c.ingest({kind:5,session:71,available:true,positionMask:7,slew:true,pulses:[pulse,1500,1500]},generation);
      panel.render();
      assert.equal(slider.value,String(angle));
      assert.equal(document.getElementById('servoAngleValue0').textContent,`${angle.toFixed(1)}°`);
      assert.equal(document.getElementById('servoAngle1').value,'90');
      assert.equal(document.getElementById('servoAngle2').value,'90');
    }
    assert.equal(writes.length,count,'rendering pulse readback must not emit commands');
  } finally {Object.assign(globalThis,prior);}
});

test('C3 sliders use their own saved servo settings without waiting for wheel or PID packets',async()=>{
  const {document,window}=parseHTML('<html><body data-hardware-profile="c3-pa"><div id="servoPanel"></div></body></html>');
  const prior={document:globalThis.document,window:globalThis.window,setInterval:globalThis.setInterval};
  Object.assign(globalThis,{document,window,setInterval:()=>0});
  try {
    let time=100;const writes=[],c=new RemoteController({now:()=>time,deviceStorage:true});let generation;
    generation=c.attach(async bytes=>{
      writes.push(bytes.slice());const v=new DataView(bytes.buffer);
      c.ingest({kind:3,wheel:254,result:0,session:c.state.session,sourceSequence:v.getUint32(8,true)},generation);
    });
    const status=session=>{
      c.ingest({kind:0,session,uart:true,ready:true,pidReady:true,phase:3,armed:false,active:false,
        fault:false,calibratedMask:0,left:0,right:0},generation);
      c.ingest({kind:5,session,available:true,positionMask:7,slew:true,armFullRange:true,pulses:[1500,1651,1500]},generation);
      c.ingest({kind:6,session,targets:[1500,1651,1500],movingMask:0,errorMask:0},generation);
    };
    const settings=(channel,session=71)=>c.ingest({kind:9,session,revision:13,channel,
      minUs:500,maxUs:2500,referenceUs:1500,calibrated:false,reversed:false,doorClosed:500,doorOpen:2500},generation);
    status(71);const panel=installServoPanel(c);
    for(let i=0;i<3;i++) {
      settings(i);panel.render();
      const slider=document.getElementById(`servoAngle${i}`);
      assert.equal(slider.disabled,false,'own C3 settings suffice, no wheel/PID packets required');
      assert.equal(c.deviceSettings.calibration(),null);
      assert.equal(slider.getAttribute('min'),'0');
      assert.equal(slider.getAttribute('max'),i===0?'270':'180');
      slider.value=i===0?'148.5':'108';slider.dispatchEvent(new window.Event('input'));await flush();
      assert.equal(writes.length,i+1,'drag must actually send, not snap back with a wheel-settings error');
      assert.equal(writes.at(-1)[1],OP.SERVO_TARGET);assert.equal(writes.at(-1)[12],i);
      assert.equal(new DataView(writes.at(-1).buffer).getUint16(14,true),i===0?1600:1700);
      assert.equal(new DataView(writes.at(-1).buffer).getUint16(16,true),0,'already enabled: never re-home');
      panel.runners[i].cancel();
    }
    time+=25;status(72);panel.render();
    assert.equal(writes.length,3,'new session must not replay old slider intent');
    assert.equal(document.getElementById('servoAngle1').disabled,true,'wait only for this session/channel settings');
    settings(0,72);panel.render();
    assert.equal(document.getElementById('servoAngle0').disabled,false);
    assert.equal(document.getElementById('servoAngle1').disabled,true,'another servo is not a substitute');
    settings(1,72);panel.render();
    assert.equal(document.getElementById('servoAngle1').disabled,false);
    assert.equal(writes.length,3,'settings readback does not actuate');
    time+=501;panel.render();assert.equal(document.getElementById('servoAngle1').disabled,true);
  } finally {Object.assign(globalThis,prior);}
});
