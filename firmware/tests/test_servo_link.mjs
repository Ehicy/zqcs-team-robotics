import test from 'node:test';
import assert from 'node:assert/strict';
import {parseHTML} from 'linkedom';
import {RemoteController} from '../web/protocol.js';
import {installServoPanel} from '../web/servo-panel.js';
const flush=()=>new Promise(resolve=>setImmediate(resolve));

test('drag stays enabled through pending ACKs; status starvation reproduces the grey slider and fair status avoids it',async()=>{
  for(const legacy of [true,false]){
    const {document,window}=parseHTML('<div id="servoPanel"></div>');
    const prior={document:globalThis.document,window:globalThis.window,setInterval:globalThis.setInterval};
    Object.assign(globalThis,{document,window,setInterval:()=>0});
    try{
      let time=0,pending=null;const writes=[];
      const c=new RemoteController({now:()=>time});
      const generation=c.attach(async bytes=>{writes.push(bytes.slice());pending={kind:3,wheel:254,result:0,session:71,sourceSequence:new DataView(bytes.buffer).getUint32(8,true)};});
      const status=kind=>{
        if(kind===0)c.ingest({kind:0,session:71,uart:true,ready:true,pidReady:true,armed:false,active:false,calibratedMask:15},generation);
        if(kind===5)c.ingest({kind:5,session:71,available:true,slew:true,armFullRange:true,positionMask:7,pulses:[1500,1500,1500]},generation);
        if(kind===6)c.ingest({kind:6,session:71,targets:[2000,1500,1500],movingMask:1,errorMask:0},generation);
      };
      [0,5,6].forEach(status);
      const panel=installServoPanel(c),runner=panel.runners[0],slider=document.getElementById('servoAngle0');
      slider.value='202.5';slider.dispatchEvent(new window.Event('input'));await flush();panel.render();
      assert(runner.inFlight);assert(c.state.configBusy);
      assert.equal(slider.disabled,false,'own pending ACK must not disable/recreate a held slider');
      slider.value='216';slider.dispatchEvent(new window.Event('input'));await flush();
      assert.equal(runner.target,2100,'drag while ACK pending keeps the latest target');
      assert.equal(writes.length,1,'not a backlog of intermediate slider commands');
      // 40ms owner/notification cadence: old ACKs consume slots. New schedule
      // mirrors test_servo_link.c, where its bounds are tested in real C code.
      const kinds=legacy?[0,1,2,5,6,0,7,0,8,0,9]:[0,1,5,6,2,0,7,5,6,8,9];
      let index=0,greyAt=null;
      for(time=40;time<=4000;time+=40){
        if(legacy&&pending){const p=pending;pending=null;c.ingest(p,generation);}
        else {status(kinds[index]);index=(index+1)%kinds.length;}
        if(!legacy&&pending){const p=pending;pending=null;c.ingest(p,generation);}
        await flush();void runner.tick();await flush();panel.render();
        if(slider.disabled){greyAt=time;break;}
      }
      if(legacy){assert.notEqual(greyAt,null);assert.match(document.getElementById('servoControlHint0').textContent,/回传已超过500ms/);}
      else {
        assert.equal(greyAt,null);assert(runner.active);assert(writes.length>=20);
        assert.equal(new DataView(writes[1].buffer).getUint16(14,true),2100);
        // No telemetry: ACKs alone cannot disguise a genuinely lost link.
        time+=501;await runner.tick();panel.render();assert(slider.disabled);
        assert.match(document.getElementById('servoControlHint0').textContent,/回传已超过500ms/);
        const count=writes.length;
        if(pending){const p=pending;pending=null;c.ingest(p,generation);await flush();}
        [0,5,6].forEach(status);panel.render();
        assert(!slider.disabled);await runner.tick();assert.equal(writes.length,count,'new telemetry does not replay an interrupted target');
        c.state.active=true;panel.render();assert(slider.disabled);
        assert.match(document.getElementById('servoControlHint0').textContent,/当前 C3 固件要求底盘停稳/);
      }
      c.disconnect();await flush();
    }finally{Object.assign(globalThis,prior);}
  }
});
