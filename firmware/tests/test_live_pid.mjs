import test from 'node:test';
import assert from 'node:assert/strict';
import {parseHTML} from 'linkedom';
import {LivePidWriter} from '../web/live-pid.js';
import {installPidPanel} from '../web/pid-panel.js';
import {installPidTest} from '../web/pid-test.js';

const original={kpMilli:1000,kiMilli:2000,kdMilli:0};
function fixture(){
  let time=0;const writes=[],errors=[],completed=[];
  const controller={generation:1,state:{connected:true,session:7,uart:true,phase:3,pidReady:true},
    now:()=>time,get mainStatusAt(){return time;},deviceSettings:{calibration:()=>[1,2,3,4]},
    configurePid:async gains=>{writes.push({...gains});},canArm:()=>false};
  const writer=new LivePidWriter({controller,direct:true,error:e=>errors.push(e.message),written:(g,s)=>completed.push([g,s])});
  writer.accept(original);
  return {controller,writer,writes,errors,completed,time:t=>time=t};
}
test('load/readback never writes; drag coalesces to latest gains and release submits automatically',async()=>{
  const f=fixture();await f.writer.tick();assert.equal(f.writes.length,0);
  f.writer.edit({...original,kpMilli:1100});f.time(80);
  f.writer.edit({...original,kpMilli:1200});f.time(250);await f.writer.tick();assert.equal(f.writes.length,0);
  await f.writer.release();assert.deepEqual(f.writes,[{...original,kpMilli:1200}]);
  assert.match(f.writer.message,/C3 已接受/);
  assert.equal(f.writer.accept(original),false,'late old readback must not overwrite the accepted slider value');
  assert.equal(f.writer.accept(f.writes[0]),true);
  f.writer.edit(f.writes[0]);f.time(2000);await f.writer.tick();assert.equal(f.writes.length,1);
});
test('moving drafts remain editable, preserve only the latest and write after stop with no stop/drive commands',async()=>{
  const f=fixture();f.controller.state.active=true;f.controller.state.armed=true;
  f.writer.edit({...original,kdMilli:5});f.time(500);await f.writer.tick();
  assert.equal(f.writes.length,0);assert.match(f.writer.message,/停稳/);
  f.writer.edit({...original,kdMilli:25});f.time(800);
  f.controller.state.active=false;f.controller.state.armed=false;await f.writer.tick();
  assert.deepEqual(f.writes,[{...original,kdMilli:25}]);
});
test('pending writes are serialized, rate-limited, and changing a slider during ACK keeps the new value',async()=>{
  const f=fixture();let resolve;
  f.controller.configurePid=gains=>{f.writes.push({...gains});return new Promise(r=>resolve=r);};
  f.writer.edit({...original,kpMilli:1200});f.time(200);const first=f.writer.tick();
  assert.equal(f.writer.busy,true);
  f.writer.edit({...original,kpMilli:1300});f.writer.edit({...original,kpMilli:1400});
  assert.equal(f.writer.accept(original),false);
  await f.writer.tick();assert.equal(f.writes.length,1);resolve();await first;
  assert.equal(f.completed[0][1].pending,true);
  f.time(600);await f.writer.tick();assert.equal(f.writes.length,1);
  f.controller.configurePid=async gains=>f.writes.push({...gains});f.time(700);await f.writer.tick();
  assert.deepEqual(f.writes.map(g=>g.kpMilli),[1200,1400]);
});
test('disconnect/session changes drop unsent values; old ACK cannot mark a new connection saved',async()=>{
  for(const mode of ['disconnect','session','generation']){
    const f=fixture();f.writer.edit({...original,kpMilli:1200});
    if(mode==='disconnect')f.controller.state.connected=false;
    if(mode==='session')f.controller.state.session++;
    if(mode==='generation')f.controller.generation++;
    f.time(400);await f.writer.tick();assert.equal(f.writes.length,0,mode);assert.equal(f.writer.pending,null);
  }
  const f=fixture();let resolve;f.controller.configurePid=g=>{f.writes.push(g);return new Promise(r=>resolve=r);};
  f.writer.edit({...original,kpMilli:1200});f.time(250);const task=f.writer.tick();
  f.controller.generation++;resolve();await task;assert.equal(f.completed.length,0);assert.equal(f.writer.lastKey,null);
});

test('a failed ACK is visible and never loops retries; dragging or retry explicitly submits again',async()=>{
  const f=fixture();f.controller.configurePid=async g=>{f.writes.push(g);throw Error('ACK超时');};
  f.writer.edit({...original,kdMilli:9});f.time(250);await f.writer.tick();
  assert.equal(f.writer.failed,true);assert.match(f.writer.message,/未写入/);
  for(let i=1;i<10;i++){f.time(i*1000);await f.writer.tick();}assert.equal(f.writes.length,1);
  f.controller.configurePid=async g=>f.writes.push(g);await f.writer.retry();
  assert.equal(f.writes.length,2);assert.equal(f.writer.failed,false);
});
test('invalid or all-zero edits cancel an older pending value instead of writing a stale draft',async()=>{
  const f=fixture();f.writer.edit({...original,kpMilli:1200});
  assert.throws(()=>f.writer.edit({kpMilli:0,kiMilli:0,kdMilli:0}),/不能同时/);
  f.time(1000);await f.writer.release();assert.equal(f.writes.length,0);assert.equal(f.writer.accept(original),false);
  f.writer.edit({...original,kpMilli:1300});await f.writer.release();assert.equal(f.writes.length,1);
});
test('offline preview and parameter loading do not silently write upon reconnect',async()=>{
  const f=fixture();f.controller.state.connected=false;f.writer.edit({...original,kpMilli:5000});
  f.controller.state.connected=true;f.controller.generation++;f.time(1000);await f.writer.tick();
  assert.equal(f.writes.length,0);assert.equal(f.writer.pending,null);assert(f.writer.accept(original));
});
test('PID panel has three sliders, numeric feedback and exact edits; input writes without save button',async t=>{
  const {document,window}=parseHTML('<section id="pid"></section>'),root=document.getElementById('pid'),f=fixture();
  const panel=installPidPanel({root,controller:f.controller,direct:true});t.after(()=>panel.dispose());
  panel.set(original);assert.equal(root.querySelectorAll('input[type="range"]').length,3);
  assert.equal(f.writes.length,0);assert.equal(root.querySelector('#writePid').hidden,true);
  const kp=root.querySelector('#pidKp');kp.value='1234';kp.dispatchEvent(new window.Event('input'));
  assert.equal(root.querySelector('#pidKpValue').textContent,'1.234');assert.equal(root.querySelector('#pidKpExact').value,'1.234');
  panel.set(original);assert.equal(kp.value,'1234','readback cannot overwrite an edited value');
  kp.dispatchEvent(new window.Event('change'));await new Promise(r=>setImmediate(r));
  assert.deepEqual(f.writes,[{...original,kpMilli:1234}]);
  const exact=root.querySelector('#pidKdExact');exact.value='0.015';exact.dispatchEvent(new window.Event('input'));
  assert.equal(root.querySelector('#pidKd').value,'15');assert.equal(panel.read().kdMilli,15);
  exact.value='';exact.dispatchEvent(new window.Event('input'));f.time(1000);await panel.writer.tick();
  assert.equal(f.writes.length,1);assert.match(root.querySelector('#pidWriteStatus').textContent,/未写入/);
  assert.throws(()=>panel.read(),/精确值/);
  f.controller.state.active=true;panel.render();assert.equal(kp.disabled,false);
});
test('shuttle UI uses editable live sliders, defaults to 100%, and dragging while idle does not move',()=>{
  const {document,window}=parseHTML('<section id="test"></section>'),root=document.getElementById('test'),f=fixture();
  const panel=installPidTest({root,controller:f.controller,direct:true,telemetry:()=>({}),calibration:()=>Array(4).fill({maxMmS:300}),foreground:()=>true,error:e=>{throw e;},log:()=>{}});
  assert.equal(root.querySelectorAll('input[type="range"]').length,3);
  assert.equal(root.querySelector('#pidPercent').value,'100');
  const distance=root.querySelector('#pidDistance');distance.value='500';distance.dispatchEvent(new window.Event('input'));
  assert.equal(root.querySelector('#pidDistanceValue').textContent,'500 mm');assert.equal(panel.runner.active,false);
  assert.equal(f.writes.length,0);
  Object.assign(panel.runner,{phase:'FORWARD',config:Array(4).fill({maxMmS:300,diameterTenthMm:650}),allowOverspeed:true});
  const percent=root.querySelector('#pidPercent');percent.value='65';percent.dispatchEvent(new window.Event('input'));
  assert.equal(panel.runner.limit,195);assert.equal(percent.disabled,false);assert.equal(panel.runner.distance,500);
});
