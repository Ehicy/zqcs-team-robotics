import test from 'node:test';
import assert from 'node:assert/strict';
import { PidShuttle } from '../web/pid-test.js';
import { OP, DEFAULT_CALIBRATION, encodeCommand, decodeStatus } from '../web/protocol.js';
function fixture({ max = 300 } = {}) {
  let time=0, stale=false;
  const config=DEFAULT_CALIBRATION.map(c=>({...c,confirmed:true,maxMmS:max})), commands=[];
  const encoder={validMask:15,counts:[0,0,0,0]},speed={validMask:15,faultCode:0,measured:[0,0,0,0],targets:[0,0,0,0]};
  const controller={state:{connected:true,uart:true,pidReady:true,ready:true,fault:false,configBusy:false,armed:false,session:7,limit:80},
    mainStatusAt:0,appliedCalibration:config.map(c=>({...c})),
    assertStopped(){if(this.state.armed)throw new Error('armed');},
    async arm(){this.state.armed=true;commands.push('ARM');},
    setLimit(v){this.state.limit=v;},setIntent(a,b){commands.push([a,b]);speed.targets.fill(a?100:0);return this.state.armed;},
    stop(){this.state.armed=false;commands.push('STOP');return Promise.resolve();}};
  const telemetry=()=>({encoder,encoderAt:stale?time-301:time,speed,speedAt:time});
  const r=new PidShuttle({controller,telemetry,calibration:()=>config,now:()=>time});
  const advance=(ms=50)=>{time+=ms;controller.mainStatusAt=time;r.tick();};
  const move=mm=>{for(let i=0;i<4;i++)encoder.counts[i]+=Math.round(mm/(Math.PI*65)*config[i].countsPerRev)*(config[i].encoderNegative?-1:1);advance();};
  return {r,controller,config,encoder,speed,commands,advance,move,stale:()=>{stale=true;}};
}
const settings={distanceMm:50,cycles:1,speedPercent:100,spaceConfirmed:true};
test('no automatic motion; explicit confirmation, fresh stopped feedback and applied calibration required',async()=>{
  const h=fixture();h.advance();assert.deepEqual(h.commands,[]);
  await assert.rejects(h.r.start({...settings,spaceConfirmed:false}),/确认/);
  h.controller.appliedCalibration=null;await assert.rejects(h.r.start(settings),/逐包确认/);
  assert.deepEqual(h.commands,[]);
});
test('finite forward/stop/reverse/stop sequence ends locked and restores manual speed limit',async()=>{
  const h=fixture();await h.r.start(settings);assert.equal(h.r.limit,300);assert.equal(h.controller.state.limit,300);
  h.advance();assert.equal(h.r.phase,'FORWARD');
  for(let i=0;i<5;i++)h.move(10);
  assert.equal(h.r.phase,'SETTLING');assert.deepEqual(h.commands.at(-1),[0,0]);
  for(let i=0;i<10;i++)h.advance();assert.equal(h.r.phase,'REVERSE');
  for(let i=0;i<5;i++)h.move(-10);
  for(let i=0;i<10;i++)h.advance();assert.equal(h.r.phase,'DONE');
  assert.equal(h.r.results.length,2);assert.equal(h.controller.state.armed,false);assert.equal(h.controller.state.limit,80);
  assert(h.r.results.every(v=>Math.abs(v.travelMm-50)<1));
});
test('stale feedback, session change, scheduler gap and wrong encoder direction stop without resuming',async()=>{
  for(const failure of ['stale','session','gap','reverse']){
    const h=fixture();await h.r.start(settings);h.advance();
    if(failure==='stale')h.stale();
    if(failure==='session')h.controller.state.session++;
    if(failure==='reverse')h.move(-20);else h.advance(failure==='gap'?150:50);
    assert.equal(h.r.phase,'ERROR',failure);assert.equal(h.commands.at(-1),'STOP');
    const count=h.commands.length;h.advance();assert.equal(h.commands.length,count);
  }
});
test('manual cancel is permanent and finite-distance bounds reject malformed input',async()=>{
  const h=fixture();for(const settings2 of [{distanceMm:49},{distanceMm:3001},{cycles:0},{cycles:21},{speedPercent:0},{distanceMm:NaN}])
    await assert.rejects(h.r.start({...settings,...settings2}));
  await h.r.start(settings);h.r.stop('operator STOP');const n=h.commands.length;h.advance();assert.equal(h.commands.length,n);
});
test('rated speed is a reference, explicit over-rating up to configured 1000mm/s is allowed',async()=>{
  const h=fixture({max:1000});await assert.rejects(h.r.start(settings),/额定/);
  await h.r.start({...settings,overspeed:true});assert.equal(h.r.limit,1000);h.r.stop();
});
test('live distance/speed/cycle edits keep encoder origin, update next tick and still finish finite legs',async()=>{
  const h=fixture();await h.r.start({...settings,distanceMm:300,cycles:3});h.advance();h.move(20);h.move(20);
  const origin=[...h.r.origin];
  h.r.updateSettings({distanceMm:50,cycles:1,speedPercent:50});
  assert.deepEqual(h.r.origin,origin);assert.equal(h.r.limit,150);assert.equal(h.r.legs,2);
  h.advance();assert.equal(h.controller.state.limit,Math.min(150,h.controller.state.limit));
  h.move(10);assert.equal(h.r.phase,'SETTLING');
  for(let i=0;i<10;i++)h.advance();assert.equal(h.r.phase,'REVERSE');
  for(let i=0;i<5;i++)h.move(-10);for(let i=0;i<10;i++)h.advance();
  assert.equal(h.r.phase,'DONE');assert.equal(h.r.results.length,2);assert.equal(h.controller.state.limit,80);
});
test('live shortening below travelled distance settles without rebasing; malformed edits leave active settings intact',async()=>{
  const h=fixture();await h.r.start({...settings,distanceMm:300});h.advance();for(let i=0;i<8;i++)h.move(10);
  const origin=[...h.r.origin];
  assert.throws(()=>h.r.updateSettings({distanceMm:20,cycles:1,speedPercent:100}));assert.equal(h.r.distance,300);
  h.r.updateSettings({distanceMm:50,cycles:1,speedPercent:100});h.advance();
  assert.equal(h.r.phase,'SETTLING');assert.deepEqual(h.r.origin,origin);
  const n=h.commands.length;h.r.cancel();h.r.updateSettings({distanceMm:100,cycles:2,speedPercent:90});h.advance();assert.equal(h.commands.length,n);
});
test('global PID codec, PID-init status/state and extended speed bounds match C layout',()=>{
  const bytes=encodeCommand(OP.PID,{session:7,sequence:9,kpMilli:1234,kiMilli:567,kdMilli:25}),v=new DataView(bytes.buffer);
  assert.equal(bytes.length,20);assert.equal(bytes[12],0);assert.equal(bytes[13],0);
  assert.equal(v.getUint16(14,true),1234);assert.equal(v.getUint16(16,true),567);assert.equal(v.getUint16(18,true),25);
  assert.throws(()=>encodeCommand(OP.PID,{session:7,sequence:9,kpMilli:0,kiMilli:0,kdMilli:20}));
  const main=new Uint8Array(20);main[0]=1;main[2]=63;main[3]=(5<<4)|0;main[18]=15;
  new DataView(main.buffer).setUint16(16,1000,true);const s=decodeStatus(main);
  assert.equal(s.phase,5);assert.equal(s.pidReady,true);assert.equal(s.limit,1000);
  assert.doesNotThrow(()=>encodeCommand(OP.DRIVE,{session:7,sequence:9,throttle:1000,steer:0,limit:1000}));
  assert.throws(()=>encodeCommand(OP.DRIVE,{session:7,sequence:9,throttle:1000,steer:0,limit:1001}));
});
