import test from 'node:test';
import assert from 'node:assert/strict';
import { DoorControl } from '../web/door-control.js';
function fixture(channel=2) {
  let time=0,available=true;const writes=[];
  const c={state:{session:71},generation:1,stopGeneration:0,servoOwner:null,
    now:()=>time,notify(){},assertStopped(){},servoStatus:{pulses:[1500,1500,1500]},servoStatusAt:0,
    servoMotion:{targets:[1500,1500,1500],movingMask:0},servoMotionAt:0,servoAvailable:()=>available,
    async servo(ch,pulse,owner,reference){assert.equal(ch,channel);assert.equal(owner,c.servoOwner);writes.push({pulse,reference});}};
  const door=new DoorControl(c,{channel});
  return {c,door,writes,step:async(ms=50)=>{time+=ms;await door.tick();},unavailable:()=>{available=false;},
    telemetry(pulse,target,moving){c.servoStatus.pulses[channel]=pulse;c.servoStatusAt=time;c.servoMotion.targets[channel]=target;c.servoMotion.movingMask=moving?1<<channel:0;c.servoMotionAt=time;}};
}
test('no auto-enable or confirmation checkbox; cold start needs a real reference value',()=>{
  const f=fixture();assert.equal(f.door.active,false);assert.equal(f.writes.length,0);
  assert.throws(()=>f.door.start(2001));f.c.servoStatus.pulses[2]=0;
  assert.throws(()=>f.door.start(2000));assert.throws(()=>f.door.start(2000,true));
  f.door.start(2000,1100);assert.equal(f.door.active,true);
});
test('sends full latest target and renews lease, not browser PWM microsteps or optimistic arrival',async()=>{
  const f=fixture();f.door.start(2000);await f.step();
  assert.deepEqual(f.writes,[{pulse:2000,reference:0}]);assert.equal(f.c.servoStatus.pulses[2],1500);
  f.telemetry(1501,2000,true);await f.step(150);assert.equal(f.writes.at(-1).pulse,2000);
  f.door.targetTo(1000);f.door.targetTo(1800);await f.step(150);assert.equal(f.writes.at(-1).pulse,1800);
  f.telemetry(1800,1800,false);await f.step();assert.equal(f.door.active,false);assert.equal(f.c.servoOwner,null);
});
test('initial reference is sent once and ACK alone cannot complete a move',async()=>{
  const f=fixture();f.c.servoStatus.pulses[2]=0;f.door.start(2000,1100);await f.step();
  assert.deepEqual(f.writes[0],{pulse:2000,reference:1100});await f.step(150);assert.equal(f.writes.length,1);
  f.telemetry(1100,2000,true);await f.step();assert.deepEqual(f.writes[1],{pulse:2000,reference:0});
});
test('STOP, session, disconnect, stale telemetry and scheduler gaps cancel without resuming',async()=>{
  for(const change of[f=>f.c.stopGeneration++,f=>f.c.generation++,f=>f.c.state.session++,f=>f.unavailable()]){
    const f=fixture();f.door.start(2000);await f.step();const n=f.writes.length;change(f);await f.step();
    assert.equal(f.door.active,false);await f.step();assert.equal(f.writes.length,n);
  }
  const f=fixture();f.door.start(2000);await f.step(400);assert.equal(f.writes.length,0);assert.equal(f.door.active,false);
});
test('cancel while awaiting ACK never renews even after a late ACK',async()=>{
  const f=fixture();let done;f.c.servo=async()=>new Promise(resolve=>{done=resolve;});
  f.door.start(2000);const task=f.step();await Promise.resolve();assert.equal(f.door.inFlight,true);
  f.door.cancel();done();await task;await f.step();assert.equal(f.door.active,false);assert.equal(f.c.servoOwner,null);
});
test('arm sliders coalesce drag targets and retain independent pulse bounds',async()=>{
  for(const channel of[0,1]){
    const f=fixture(channel);assert.throws(()=>f.door.targetTo(1000));f.door.targetTo(1700);f.door.targetTo(1325);f.door.targetTo(1600);
    await f.step();assert.equal(f.writes[0].pulse,1600);assert.equal(f.c.servoOwner,f.door);
    f.telemetry(1600,1600,false);await f.step();assert.equal(f.door.active,false);
  }
});


test('staggered PWM/trajectory telemetry completes slider without endless same-target renewals',async()=>{
  const f=fixture();f.door.start(2000);await f.step(0);
  for(let ms=50;ms<=2000&&f.door.active;ms+=50){
    // Both streams are fresh, but neither can consistently follow every 150ms renewal.
    if(ms%300===100){f.c.servoStatus.pulses[2]=2000;f.c.servoStatusAt=ms-50;}
    if(ms%300===250){f.c.servoMotion.targets[2]=2000;f.c.servoMotionAt=ms-50;}
    await f.step(50);
  }
  assert.equal(f.door.active,false);assert.match(f.door.message,/指令已到/);
  const count=f.writes.length;await f.step(300);assert.equal(f.writes.length,count);
});
