import test from 'node:test';
import assert from 'node:assert/strict';
import { DoorControl } from '../web/door-control.js';
function fixture() {
  let time=0, available=true, busy=false;
  const writes=[];
  const c={state:{session:71},generation:1,stopGeneration:0,servoOwner:null,
    now:()=>time,notify(){},assertStopped(){if(busy)throw new Error('busy');},
    servoStatus:{pulses:[1500,1500,1500]},servoAvailable:()=>available,
    async servo(channel,pulse,owner){ assert.equal(channel,2); assert.equal(owner,c.servoOwner);
      writes.push(pulse); c.servoStatus.pulses[channel]=pulse; }};
  const door=new DoorControl(c);
  return {c,door,writes,step:async(ms=150)=>{time+=ms;await door.tick();}, unavailable:()=>{available=false;},busy:()=>{busy=true;}};
}
test('door never auto-enables and requires explicit calibrated target confirmation',()=>{
  const f=fixture(); assert.equal(f.door.active,false); assert.equal(f.writes.length,0);
  assert.throws(()=>f.door.start(2000)); assert.throws(()=>f.door.start(2001,true));
  f.c.servoStatus.pulses[2]=0; assert.throws(()=>f.door.start(2000,true));
});
test('door opens/closes in <=25us steps with independent channel ownership',async()=>{
  const f=fixture(); f.door.start(2000,true);
  for(let i=0;i<21;i++)await f.step();
  assert.equal(f.door.active,false);assert.equal(f.writes.length,20);
  assert.equal(f.writes.at(-1),2000); assert.equal(f.c.servoOwner,null);
  assert.deepEqual(f.c.servoStatus.pulses,[1500,1500,2000]);
  f.door.start(1000,true); for(let i=0;i<41;i++)await f.step();
  assert.equal(f.writes.at(-1),1000);assert.equal(f.door.active,false);
});
test('STOP, session, disconnect, stale telemetry and page gaps cancel without resuming',async()=>{
  for(const change of [f=>f.c.stopGeneration++,f=>f.c.generation++,f=>f.c.state.session++,f=>f.unavailable()]) {
    const f=fixture();f.door.start(2000,true);await f.step();const n=f.writes.length;
    change(f);await f.step();assert.equal(f.door.active,false);await f.step();assert.equal(f.writes.length,n);
  }
  const f=fixture();f.door.start(2000,true);await f.step(400);assert.equal(f.writes.length,0);assert.equal(f.door.active,false);
});
test('cancel while awaiting ACK never schedules another command',async()=>{
  const f=fixture();let done;f.c.servo=async()=>new Promise(resolve=>{done=resolve;});
  f.door.start(2000,true);const task=f.step();await Promise.resolve();
  assert.equal(f.door.inFlight,true);f.door.cancel();done();await task;
  assert.equal(f.door.inFlight,false);assert.equal(f.door.active,false);assert.equal(f.c.servoOwner,null);
});
test('arm sliders coalesce drag targets and keep pulse limits on channels0/1',async()=>{
  for(const channel of [0,1]) {
    const f=fixture();const writes=[];
    f.c.servo=async(ch,pulse,owner)=>{ assert.equal(ch,channel);assert.equal(owner,f.c.servoOwner);writes.push(pulse);f.c.servoStatus.pulses[ch]=pulse; };
    const r=new DoorControl(f.c,{channel});
    assert.throws(()=>r.targetTo(1000,true));r.targetTo(1700,true);
    r.targetTo(1325,true);r.targetTo(1600,true);
    /* Use a fresh clock injected via the shared fixture controller. */
    for(let i=0;i<5;i++) { await f.step(); await r.tick(); }
    assert.equal(writes[0],1525);assert.equal(writes.at(-1),1600);
    assert.equal(r.active,false);assert.equal(f.c.servoOwner,null);
  }
});
