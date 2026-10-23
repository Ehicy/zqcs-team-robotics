import test from 'node:test';
import assert from 'node:assert/strict';
import {RobustOdometry} from '../web/robust-odometry.js';
import {TeachRecorder,TraceReplay} from '../web/teach-replay.js';

// A synthetic circumference of 1000 mm and CPR=1000 makes one count = 1 mm.
const calibration = () => Array.from({length:4}, () => ({cpr:1000, diameterMm:1000/Math.PI, direction:1}));
const make = (overrides={}) => new RobustOdometry({wheelCalibration:calibration(),
  trackWidthMm:200, maxWheelAccelMmS2:100000, pairToleranceMmS:30,
  pairRelativeTolerance:0.1, ...overrides});
const near = (actual, expected, epsilon=1e-8) => assert.ok(Math.abs(actual-expected)<epsilon,
  `expected ${expected}, got ${actual}`);
const seed = (o, counts=[0,0,0,0]) => o.update({counts,timestampMs:0});

test('baseline sends no movement; four consistent wheels drive a relative straight line', () => {
  const o=make(); let s=seed(o,[100,100,100,100]);
  assert.equal(s.integrated,false); near(s.xMm,0);
  s=o.update({counts:[120,120,120,120],timestampMs:100});
  near(s.xMm,20); near(s.yMm,0); near(s.headingRad,0);
  assert.equal(s.validMask,15); assert.equal(s.observable,true); assert.equal(s.pathComplete,true);
});

test('constant-curvature SE(2) integration and counterclockwise sign', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[100,0,100,0],timestampMs:100});
  near(s.headingRad,0.5); near(s.xMm,100*Math.sin(0.5)); near(s.yMm,100*(1-Math.cos(0.5)));
});

test('in-place rotation changes heading without inventing translation', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[20,-20,20,-20],timestampMs:100});
  near(s.xMm,0); near(s.yMm,0); near(s.headingRad,0.2);
});

test('one absent encoder does not freeze its three healthy peers', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[10,10,0,10],validMask:11,timestampMs:100});
  near(s.xMm,10); assert.equal(s.validMask,11); assert.equal(s.observable,true);
  assert.equal(s.confidence[2],0); assert.equal(s.degraded,true);
});

test('all four one-encoder omissions and four cross-side double omissions remain observable', () => {
  const masks=[14,13,11,7,3,9,6,12];
  for (const validMask of masks) {
    const o=make(); seed(o);
    const s=o.update({counts:[12,12,12,12],validMask,timestampMs:100});
    assert.equal(s.observable,true,`mask ${validMask}`); near(s.xMm,12);
  }
});

test('same-side double failure explicitly loses observability, does not use command as real motion', () => {
  for (const validMask of [5,10,0]) {
    const o=make(); seed(o);
    const s=o.update({counts:[20,20,20,20],validMask,timestampMs:100,
      commandedWheelMmS:[200,200,200,200]});
    assert.equal(s.observable,false); assert.equal(s.pathComplete,false); near(s.xMm,0);
    assert.equal(s.reason,'side-unobservable');
  }
});

test('speed outlier and counter reset rebase locally; good wheels still integrate', () => {
  const o=make({maxWheelSpeedMmS:1000}); seed(o,[100,100,100,100]);
  let s=o.update({counts:[1000000,110,110,110],timestampMs:100});
  assert.equal(s.validMask,14); assert.equal(s.wheelReasons[0],'speed-outlier'); near(s.xMm,10);
  s=o.update({counts:[0,120,120,120],timestampMs:200});
  assert.equal(s.wheelReasons[0],'rebase'); near(s.xMm,20);
  s=o.update({counts:[10,130,130,130],timestampMs:300});
  assert.equal(s.validMask,15); near(s.xMm,30);
});

test('returning invalid counter discards the outage, not an accumulated jump', () => {
  const o=make(); seed(o);
  let s=o.update({counts:[10,10,0,10],validMask:11,timestampMs:100});
  near(s.xMm,10);
  s=o.update({counts:[20,20,50000,20],validMask:15,timestampMs:200});
  near(s.xMm,20); assert.equal(s.wheelReasons[2],'rebase');
  s=o.update({counts:[30,30,50010,30],timestampMs:300});
  near(s.xMm,30); assert.equal(s.validMask,15);
});

test('signed 32-bit wrap and uint32 representation are both continuous', () => {
  for (const [start,end] of [[2147483642,-2147483644],[4294967290,4],[-6,4]]) {
    const o=make(); seed(o,[start,start,start,start]);
    const s=o.update({counts:[end,end,end,end],timestampMs:100}); near(s.xMm,10);
  }
});

test('per-wheel CPR, diameter and direction normalize x4/x2 counters', () => {
  const wheelCalibration=calibration();
  wheelCalibration[0].direction=-1; wheelCalibration[2].cpr=500; wheelCalibration[3].cpr=500;
  const o=make({wheelCalibration}); seed(o);
  const s=o.update({counts:[-10,10,5,5],timestampMs:100}); near(s.xMm,10);
});

test('long sample gap is not silently recorded as a complete accurate path', () => {
  const o=make({maxGapMs:300}); seed(o);
  let s=o.update({counts:[1000,1000,1000,1000],timestampMs:500});
  near(s.xMm,0); assert.equal(s.pathComplete,false); assert.equal(s.reason,'sample-gap');
  s=o.update({counts:[1010,1010,1010,1010],timestampMs:600});
  near(s.xMm,10); assert.equal(s.observable,true); assert.equal(s.pathComplete,false);
  assert.equal(s.estimated,true); assert.equal(s.degraded,true);
});

test('duplicate and reordered timestamps never change count baselines', () => {
  const o=make(); seed(o);
  o.update({counts:[10,10,10,10],timestampMs:100});
  let s=o.update({counts:[500,500,500,500],timestampMs:100});
  assert.equal(s.reason,'stale-timestamp'); near(s.xMm,10);
  o.update({counts:[500,500,500,500],timestampMs:50});
  s=o.update({counts:[20,20,20,20],timestampMs:200}); near(s.xMm,20);
});

test('motion evidence rejects frozen count, and genuine pulses automatically restore feedback', () => {
  const o=make({stuckAfterMs:200}); seed(o);
  let s;
  for(let n=1;n<=4;n++) s=o.update({counts:[n*20,n*20,0,n*20],timestampMs:n*100});
  assert.equal(s.validMask,11); assert.equal(s.wheelReasons[2],'motion-unconfirmed');
  const before={xMm:s.xMm,yMm:s.yMm};
  s=o.update({counts:[100,100,20,100],timestampMs:500});
  assert.equal(s.validMask,15); assert.ok(s.confidence[2]>0 && s.confidence[2]<1);
  near(Math.hypot(s.xMm-before.xMm,s.yMm-before.yMm),20);
});

test('zero counts without motion evidence are valid stationary samples, not proof of connected cables', () => {
  const o=make(); seed(o); let s;
  for(let n=1;n<=10;n++) s=o.update({counts:[0,0,0,0],timestampMs:n*100});
  near(s.xMm,0); assert.equal(s.validMask,15); assert.equal(s.observable,true);
});

test('all frozen counts with sustained motion request become unconfirmed, not fabricated motion', () => {
  const o=make({stuckAfterMs:200}); seed(o); let s;
  for(let n=1;n<=4;n++) s=o.update({counts:[0,0,0,0],timestampMs:n*100,
    commandedWheelMmS:[100,100,100,100]});
  near(s.xMm,0); assert.equal(s.validMask,0); assert.equal(s.observable,false);
});

test('unresolvable same-side disagreement is labelled ambiguous, not confidently voted away', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[10,10,60,10],timestampMs:100});
  assert.equal(s.ambiguous,true); assert.equal(s.estimated,true); assert.equal(s.degraded,true);
  assert.equal(s.wheelReasons[0],'pair-ambiguous'); assert.equal(s.wheelReasons[2],'pair-ambiguous');
});

test('a one-wheel abrupt acceleration outlier cannot change healthy side estimate', () => {
  const o=make({maxWheelAccelMmS2:100}); seed(o);
  o.update({counts:[10,10,10,10],timestampMs:100});
  const s=o.update({counts:[100,20,20,20],timestampMs:200});
  assert.equal(s.validMask,14); assert.equal(s.wheelReasons[0],'acceleration-outlier'); near(s.xMm,20);
});

test('NaN only rejects its wheel; malformed API arguments fail explicitly', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[NaN,10,10,10],timestampMs:100}); near(s.xMm,10);
  assert.equal(s.validMask,14);
  assert.throws(()=>o.update({counts:[1],timestampMs:100}),/RF, LF/);
  assert.throws(()=>o.update({counts:[1,1,1,1],timestampMs:NaN}),/timestampMs/);
  assert.throws(()=>make({trackWidthMm:0}),/trackWidthMm/);
});

test('reset starts a new relative origin, preserving no old trajectory gap or baseline', () => {
  const o=make(); seed(o);
  o.update({counts:[10,10,10,10],timestampMs:100});
  o.reset({headingRad:Math.PI/2});
  o.update({encoders:[100,100,100,100],timestampMs:200});
  const s=o.update({encoders:[110,110,110,110],timestampMs:300});
  near(s.xMm,0); near(s.yMm,10); assert.equal(s.pathComplete,true);
});

test('snapshot arrays do not alias internal confidence or subsequent output', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[10,10,10,10],timestampMs:100}); s.confidence[0]=99;
  const next=o.update({counts:[20,20,20,20],timestampMs:200}); assert.ok(next.confidence[0]<=1);
});

test('side and wheel travel outputs expose the selected measured displacement and speeds', () => {
  const o=make(); seed(o);
  const s=o.update({counts:[20,10,20,10],timestampMs:100});
  assert.deepEqual(s.sideTravelMm,[10,20]); assert.deepEqual(s.sideVelocityMmS,[100,200]);
  assert.deepEqual(s.wheelTravelMm,[20,10,20,10]); assert.equal(s.missingSideMask,0);
  const lost=o.update({counts:[40,0,40,0],validMask:5,timestampMs:200});
  assert.deepEqual(lost.sideTravelMm,[10,40]); assert.equal(lost.sideVelocityMmS[0],null);
  assert.equal(lost.missingSideMask,1);
});

test('A and D steering agree with actual firmware RF/LF/RR/LR order', () => {
  // remote_control.c: left=throttle+steer, right=throttle-steer.
  for (const [counts,heading] of [[[10,-10,10,-10],0.1],[[-10,10,-10,10],-0.1]]) {
    const o=make(); seed(o);
    const s=o.update({counts,timestampMs:100}); near(s.headingRad,heading);
  }
});

test('persistent equally plausible disagreement stays explicitly estimated', () => {
  const o=make(); seed(o); let s;
  for(let n=1;n<=10;n++) s=o.update({counts:[10*n,10*n,60*n,10*n],timestampMs:n*100});
  assert.equal(s.ambiguous,true); assert.equal(s.estimated,true);
  assert.ok(s.confidence[0]<=0.25 && s.confidence[2]<=0.25);
  assert.equal(s.validMask,15,'two plausible sensors cannot identify a true culprit');
});

test('history can reject a locally abrupt pair discrepancy but does not declare a proven hardware fault', () => {
  const o=make(); seed(o);
  o.update({counts:[10,10,10,10],timestampMs:100});
  const s=o.update({counts:[20,20,70,20],timestampMs:200});
  assert.equal(s.wheelReasons[2],'pair-outlier'); assert.equal(s.validMask,11);
  assert.equal(s.degraded,true); near(s.xMm,20);
});

test('full-side recovery resumes only new distance and keeps the prior interval marked missing', () => {
  const o=make(); seed(o);
  o.update({counts:[20,0,20,0],validMask:5,timestampMs:100});
  let s=o.update({counts:[30,10000,30,10000],timestampMs:200});
  assert.equal(s.observable,false); near(s.xMm,0);
  s=o.update({counts:[40,10010,40,10010],timestampMs:300});
  assert.equal(s.observable,true); near(s.xMm,10); near(s.headingRad,0);
  assert.equal(s.pathComplete,false); assert.equal(s.estimated,true);
});

test('negative signed wrap and two briefly masked wheels recover independently', () => {
  const o=make(); seed(o,[-2147483644,-2147483644,-2147483644,-2147483644]);
  const s=o.update({counts:[2147483642,2147483642,0,0],validMask:3,timestampMs:100});
  near(s.xMm,-10); assert.equal(s.observable,true);
  const next=o.update({counts:[2147483632,2147483632,-2147483000,-2147483000],timestampMs:200});
  near(next.xMm,-20); assert.equal(next.validMask,3);
});

test('near-zero curvature remains finite and matches the straight-line limit', () => {
  const o=make({trackWidthMm:1e12}); seed(o);
  const s=o.update({counts:[11,10,11,10],timestampMs:100});
  near(s.xMm,10.5); assert.ok(Number.isFinite(s.yMm)); near(s.headingRad,1e-12);
});

// Integration regressions: a bad/missing odometry sample must not silently
// change the recorded driving duration or become a false distance reference.
function straightTrace(direction=1, missingSideMask=0) {
  return {version:1,calibration:calibration(),trackWidthMm:200,servos:[],samples:[
    {t:0,left:0,right:0,throttle:1000*direction,steer:0,limit:200,estimated:!!missingSideMask,missingSideMask},
    {t:1000,left:200*direction,right:200*direction,throttle:0,steer:0,limit:200,estimated:!!missingSideMask,missingSideMask},
  ]};
}
function replayBench(trace=straightTrace(), options={}) {
  let time=0; const writes=[],stops=[];
  const replay=new TraceReplay({now:()=>time,drive:(left,right)=>writes.push({left,right,time}),
    stop:()=>stops.push(time),servo:()=>{}});
  replay.start(trace,{session:1,generation:1,odometry:{sideTravelMm:[0,0]},...options});
  return {replay,writes,stops,
    tick:async (at,odometry)=>{time=at;await replay.tick({odometry,session:1,generation:1});}};
}
const measuredTravel=(left,right,extra={})=>({sideTravelMm:[left,right],missingSideMask:0,
  observable:true,confidence:[1,1,1,1],estimated:false,pathComplete:true,...extra});

test('replay regression: fully missing feedback preserves original feedforward clock, not 2.3 seconds', async () => {
  const b=replayBench();
  for(let time=100;time<=1000;time+=100) await b.tick(time,null);
  near(b.replay.clock,1000); assert.equal(b.replay.estimated,true);
  assert.ok(b.writes.every(w=>w.left===200&&w.right===200));
  // Completion is evaluated on the next scheduler tick, not several seconds
  // later because reference error was computed from unobserved distance.
  await b.tick(1100,null);
  assert.equal(b.replay.active,false); assert.deepEqual(b.stops,[1100]);
  assert.match(b.replay.message,/位置未验证/);
});

test('replay regression: forward and backward overshoot do not slow the reference clock', async () => {
  for(const direction of [1,-1]) {
    const b=replayBench(straightTrace(direction));
    await b.tick(100,measuredTravel(direction*200,direction*200));
    near(b.replay.clock,100);
  }
});

test('replay regression: trusted lag scales feedforward and reference clock by the same factor', async () => {
  for(const direction of [1,-1]) {
    const b=replayBench(straightTrace(direction),{kp:0});
    await b.tick(100,measuredTravel(direction*-200,direction*-200));
    near(b.replay.clock,50);
    assert.equal(b.writes.at(-1).left,direction*100);
    assert.equal(b.writes.at(-1).right,direction*100);
  }
});

test('recorder preserves missing side metadata, including fully absent observations', () => {
  const r=new TeachRecorder(); r.start({now:0,calibration:calibration(),trackWidthMm:200});
  r.sample({now:0,odometry:measuredTravel(0,0,{missingSideMask:1,estimated:true}),
    intent:{throttle:1000,steer:0},limit:200});
  const trace=r.finish({now:23,odometry:null,intent:{throttle:0,steer:0},limit:200});
  assert.equal(trace.samples[0].missingSideMask,1);assert.equal(trace.samples[0].estimated,true);
  assert.equal(trace.samples[1].missingSideMask,3);assert.equal(trace.samples[1].estimated,true);
  assert.equal(trace.samples[1].t,23,'finish must not synthesize extra elapsed time');
});

test('replay regression: a missing side in the original recording cannot become a precise correction target', async () => {
  const trace=straightTrace(1,1);
  // The recorded left travel was frozen by missing data, although left PWM
  // continued. During playback, its valid sensor must not correct toward zero.
  trace.samples[1].left=0;
  const b=replayBench(trace);
  await b.tick(100,measuredTravel(300,0));
  assert.equal(b.writes.at(-1).left,200);
  near(b.replay.clock,100); assert.equal(b.replay.estimated,true);
});

test('replay regression: an incomplete observed history never announces precisely verified arrival', async () => {
  const b=replayBench(); b.replay.clock=1000;
  await b.tick(100,measuredTravel(200,200,{pathComplete:false}));
  assert.equal(b.replay.active,false);
  assert.match(b.replay.message,/位置未验证/); assert.doesNotMatch(b.replay.message,/相对里程到达/);
});

test('replay recovery: missing live-side feedforward travel is not driven a second time after feedback returns', async () => {
  const b=replayBench();
  for(let time=100;time<=500;time+=100) {
    await b.tick(time,measuredTravel(0,(time/100-1)*20,
      {missingSideMask:1,estimated:true,pathComplete:false}));
  }
  // The wheel physically travelled during the outage. Its resumed odometry
  // deliberately omits that unobserved interval, so only 20 mm is reported.
  await b.tick(600,measuredTravel(20,100,{estimated:true,pathComplete:false}));
  near(b.replay.error[0],0); near(b.replay.error[1],0);
  assert.equal(b.writes.at(-1).left,200,'recovery must not boost PWM to repeat an already-driven gap');
  await b.tick(700,measuredTravel(40,120,{estimated:true,pathComplete:false}));
  near(b.replay.error[0],0); near(b.replay.error[1],0);
  assert.equal(b.writes.at(-1).left,200,'later measured increments must remain correctly aligned');
});

test('replay recovery: a frozen recorded-side reference does not drive backward when source observations resume', async () => {
  const trace=straightTrace();
  trace.samples=[
    {t:0,left:0,right:0,throttle:1000,steer:0,limit:200,missingSideMask:0,estimated:false},
    {t:200,left:0,right:40,throttle:1000,steer:0,limit:200,missingSideMask:1,estimated:true},
    {t:400,left:40,right:80,throttle:1000,steer:0,limit:200,missingSideMask:0,estimated:false},
    {t:1000,left:160,right:200,throttle:0,steer:0,limit:200,missingSideMask:0,estimated:false},
  ];
  const b=replayBench(trace);
  for(let time=100;time<=400;time+=100) {
    await b.tick(time,measuredTravel((time/100-1)*20,(time/100-1)*20));
  }
  await b.tick(500,measuredTravel(80,80));
  near(b.replay.error[0],0); near(b.replay.error[1],0);
  assert.equal(b.writes.at(-1).left,200,'source-mask recovery must not undo distance travelled by feedforward');
  await b.tick(600,measuredTravel(100,100));
  near(b.replay.error[0],0); near(b.replay.error[1],0);
  assert.equal(b.writes.at(-1).left,200);
  for(let time=700;time<=1100;time+=100) {
    await b.tick(time,measuredTravel((time/100-1)*20,(time/100-1)*20));
  }
  assert.equal(b.replay.active,false);
  assert.match(b.replay.message,/位置未验证/,'the recovered source gap must remain historically estimated');
});
