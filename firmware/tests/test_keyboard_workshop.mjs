import test from 'node:test';
import assert from 'node:assert/strict';
import {parseHTML} from 'linkedom';
import {KeyTimeline,NodeRecorder,WorkshopStore,WORKSHOP_KEY,relativeEncoderTargets,validateBlocks,validateNodeTrace} from '../web/keyboard-workshop.js';
import {installAutomation} from '../web/automation-panel.js';
import {PA_CHASSIS_CALIBRATION} from '../web/protocol.js';
const storage=()=>{const m=new Map();return {map:m,getItem:k=>m.get(k)??null,setItem:(k,v)=>m.set(k,v)};};
const row=(key='f',blocks=[{channel:0,atMs:0,angle:135}])=>({key,blocks});
const cal=PA_CHASSIS_CALIBRATION.map(v=>({cpr:v.countsPerRev,diameterMm:v.diameterTenthMm/10,direction:v.encoderNegative?-1:1,motorDirection:v.motorNegative?-1:1,maxSpeedMmS:v.maxMmS}));
const point=counts=>({counts,validMask:15});
const trace=(overrides={})=>({version:2,calibration:cal,trackWidthMm:165,startEncoder:point([0,0,0,0]),endEncoder:point([0,0,0,0]),samples:[{t:0,left:0,right:0,throttle:0,steer:0,limit:200,missingSideMask:0},{t:100,left:0,right:0,throttle:0,steer:0,limit:200,missingSideMask:0}],servos:[],keyEvents:[],directServos:[],...overrides});
const flush=()=>new Promise(r=>setImmediate(r));
test('numbered blocks use one key t=0 and independent millisecond deadlines, not predecessor completion',()=>{
  let time=123,sends=[];const timeline=new KeyTimeline({now:()=>time,send:e=>sends.push({at:time,events:e})});
  timeline.trigger(row('f',[{channel:0,atMs:0,angle:180},{channel:1,atMs:17,angle:40},{channel:2,atMs:103,angle:30}]));
  timeline.tick();assert.equal(sends.length,1);time=139;timeline.tick();assert.equal(sends.length,1);
  time=140;timeline.tick();assert.equal(sends[1].events[0].channel,1);time=226;timeline.tick();assert.equal(sends[2].events[0].channel,2);
  assert.equal(timeline.active,false);
});
test('different function keys cannot trigger within 2 seconds; rejected key does not change pending actions',()=>{
  let time=0;const timeline=new KeyTimeline({now:()=>time});
  assert(timeline.trigger(row('f',[{channel:0,atMs:5000,angle:180}])));time=1999;
  assert.equal(timeline.trigger(row('g')),false);assert.equal(timeline.pending[0].due,5000);
  time=2000;assert(timeline.trigger(row('g')));assert.equal(timeline.pending.length,2,'the prior key retains its independent future deadline');
});
test('a fresh press of the same key replaces its unsent blocks and later same-channel targets supersede',()=>{
  let time=0;const sent=[],timeline=new KeyTimeline({now:()=>time,send:e=>sent.push(e)});
  timeline.trigger(row('f',[{channel:0,atMs:1000,angle:180}]));time=500;
  timeline.trigger(row('f',[{channel:0,atMs:1000,angle:150},{channel:0,atMs:1000,angle:170}]));
  time=1000;timeline.tick();assert.equal(sent.length,0);time=1500;timeline.tick();assert.equal(sent[0].length,1);
  assert.equal(sent[0][0].pulseUs,Math.round(500+170*2000/270));
});
test('loading/importing auto-saved new configuration never reads or destroys the legacy position library',()=>{
  const s=storage();s.map.set('crtc-motion-library-270-v1','old data');const lib=new WorkshopStore(s);
  assert.equal(s.map.has(WORKSHOP_KEY),false);lib.edit(d=>d.bindings[0].name='<中文功能>');
  const loaded=new WorkshopStore(s);assert.equal(loaded.data.bindings[0].name,'<中文功能>');assert.equal(s.map.get('crtc-motion-library-270-v1'),'old data');
  const before=loaded.export();assert.throws(()=>loaded.edit(d=>d.bindings[1].key='f'),/重复/);assert.equal(loaded.export(),before);
  assert.throws(()=>loaded.edit(d=>d.bindings[0].key='w'));assert.throws(()=>loaded.import('{"version":1}'));
  const other=new WorkshopStore(storage());other.import(before);assert.equal(other.export(),before);
});
test('recording retains down/up order, snapshots and short key taps; finish closes holds and forces zero intent',()=>{
  const r=new NodeRecorder();r.start({at:100,calibration:cal,trackWidthMm:165,startEncoder:point([10,20,30,40])});
  const sample=(at,throttle)=>({at,odometry:{sideTravelMm:[0,0],observable:true,estimated:false,missingSideMask:0},counts:[10,20,30,40],intent:{throttle,steer:0},limit:200});
  r.sample(sample(100,0),true);r.key('down','w',101);r.sample(sample(101,1000),true);r.key('up','w',109);r.sample(sample(109,0),true);
  const action=[{channel:0,atMs:3,angle:135}];r.key('down','f',110,{accepted:true,action});action[0].angle=270;r.key('down','g',120,{accepted:false});
  const result=r.finish(sample(130,1000),{counts:[12,23,34,45],validMask:7});
  assert.equal(result.keyEvents[0].t,1);assert.equal(result.keyEvents[1].t,9);assert.equal(result.keyEvents[2].action[0].angle,135);
  assert(result.keyEvents.some(e=>e.type==='up'&&e.key==='f'&&e.synthetic));assert.equal(result.samples.at(-1).throttle,0);
  assert.equal(result.samples.find(s=>s.t===1).throttle,1000);assert.equal(result.endEncoder.validMask,7);assert.equal(r.active,false);
});
test('four encoder endpoints are stored separately; relative replay targets handle signed counter wrap',()=>{
  const t=trace({startEncoder:point([2147483646,20,null,40]),endEncoder:{counts:[-2147483647,30,null,35],validMask:11}});
  assert.deepEqual(relativeEncoderTargets(t,point([100,200,300,400])),[103,210,null,395]);
  const valid=validateNodeTrace(t);assert.equal(valid.endEncoder.validMask,11);
  assert.throws(()=>validateNodeTrace({...t,endEncoder:{counts:[0],validMask:15}}));
  assert.throws(()=>validateBlocks([{channel:1,atMs:0,angle:181}]));assert.throws(()=>validateBlocks([{channel:0,atMs:1.5,angle:135}]));
});
function panelFixture(t){
  const {document,window}=parseHTML('<html><body><div id="workshop"></div><input id="mapTrack" value="165"><input id="servoReference0" value="135"><input id="servoReference1" value="90"><input id="servoReference2" value="90"></body></html>');
  Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option'))?.value||'';},set(v){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===String(v));}});
  const old={document:globalThis.document,window:globalThis.window,localStorage:globalThis.localStorage};Object.assign(globalThis,{document,window,localStorage:storage()});t.after(()=>Object.assign(globalThis,old));
  let time=0,panel,input={throttle:0,steer:0};const counts=[0,0,0,0],writes=[],drives=[];let stops=0;
  const c={state:{connected:true,uart:true,session:7,pwmSupported:true,limit:200,active:false,armed:false,configBusy:false},generation:1,stopGeneration:0,now:()=>time,
    get mainStatusAt(){return time;},servoOwner:null,servoStatus:{pulses:[1500,1500,1500]},servoStatusAt:0,servoMotion:{targets:[1500,1500,1500],movingMask:0},servoMotionAt:0,
    assertStopped(){if(this.state.active)throw Error('moving');},servoAvailable:()=>true,
    async servo(channel,pulseUs,owner,referenceUs){writes.push({channel,pulseUs,referenceUs});this.servoStatus.pulses[channel]=pulseUs;this.servoMotion.targets[channel]=pulseUs;this.servoStatusAt=this.servoMotionAt=time;},
    stop(){stops++;this.stopGeneration++;this.state.active=this.state.armed=false;panel?.onControllerLock();return Promise.resolve();},
    packet:(op,args)=>({bytes:{op,...args}}),queue:{offer:async b=>drives.push(b)}};
  panel=installAutomation({root:document.getElementById('workshop'),controller:c,now:()=>time,telemetry:()=>({encoder:{counts:[...counts],validMask:15},encoderAt:time}),calibration:()=>PA_CHASSIS_CALIBRATION.map(v=>({...v})),intent:()=>({...input}),takeover:()=>{input={throttle:0,steer:0};panel?.clearInput();}});
  const click=async selector=>{document.querySelector(selector).dispatchEvent(new window.Event('click',{bubbles:true}));await flush();};
  const step=async(ms=25)=>{time+=ms;c.servoStatusAt=c.servoMotionAt=time;panel.tick();await flush();};
  return {panel,c,document,window,counts,writes,drives,click,step,time:v=>time=v,input:v=>input=v,stops:()=>stops};
}
test('real row/block dialog edits 0.003s and full servo targets, auto saves, with no hardware output',async t=>{
  const f=panelFixture(t);await f.click('[data-block-row="key0"][data-block-index="0"]');
  assert(f.document.getElementById('blockDialog').hasAttribute('open'));
  assert.equal(f.document.getElementById('blockAfter').disabled,true,'absolute block must ignore dependency validation');
  assert.equal(Number(f.document.getElementById('blockAfter').max),1);
  f.document.getElementById('blockTrigger').value='start';f.document.getElementById('blockTrigger').onchange();
  assert.equal(f.document.getElementById('blockAfter').disabled,false);
  f.document.getElementById('blockTrigger').value='absolute';f.document.getElementById('blockTrigger').onchange();
  f.document.getElementById('blockSeconds').value='0.003';f.document.getElementById('blockAngleExact').value='270';
  f.document.getElementById('blockForm').onsubmit({preventDefault(){}});
  assert.equal(f.panel.library.data.bindings[0].blocks[0].atMs,3);assert.equal(f.panel.library.data.bindings[0].blocks[0].angle,270);
  assert.equal(f.writes.length,0);assert.equal(f.drives.length,0);assert.equal(f.stops(),0);
  assert.equal(f.document.querySelectorAll('#positionList,#poseList,#programList').length,0);
});
test('node record/end/reset buttons store all four final counts, key snapshots and no reset of physical counters',async t=>{
  const f=panelFixture(t);await f.click('[data-record-node="node1"]');assert(f.panel.recorder.active);
  f.time(10);f.input({throttle:1000,steer:0});f.panel.keyEvent('down',{key:'w'});
  f.time(20);f.panel.keyEvent('down',{key:'f'});f.panel.keyEvent('down',{key:'f',repeat:true});
  f.time(100);f.counts.splice(0,4,-10,11,-5,6);f.panel.receive({counts:f.counts,validMask:15},100);f.panel.tick();await flush();
  f.time(200);await f.click('[data-finish-node="node1"]');
  const recorded=f.panel.library.data.nodes[0].trace;
  assert.deepEqual(recorded.endEncoder.counts,[-10,11,-5,6]);assert.equal(recorded.keyEvents.filter(e=>e.type==='down'&&e.key==='f').length,1);
  assert(recorded.keyEvents.some(e=>e.key==='f'&&e.action));assert.equal(recorded.samples.at(-1).throttle,0);
  const saved=recorded.keyEvents.find(e=>e.action).action[0].angle;f.panel.library.edit(d=>d.bindings[0].blocks[0].angle=270);
  assert.equal(recorded.keyEvents.find(e=>e.action).action[0].angle,saved);
  assert.equal(f.document.querySelectorAll('.node-counts output').length,4);
  const before=[...f.counts];await f.click('[data-reset-node="node1"]');assert.equal(f.panel.library.data.nodes[0].trace,null);assert.deepEqual(f.counts,before);
});
test('replay uses the recorded function snapshot, not the edited binding, and cancellation discards future blocks',async t=>{
  const f=panelFixture(t),action=[{channel:0,atMs:3,angle:180}];
  // A recent different live key must not overwrite the original recording's
  // accepted/rejected decisions when starting a replay.
  f.panel.keyEvent('down',{key:'g'});f.panel.keyEvent('up',{key:'g'});
  f.panel.library.edit(d=>{d.bindings[0].blocks[0].angle=270;d.nodes[0].trace=trace({samples:[{t:0,left:0,right:0,throttle:0,steer:0,limit:200,missingSideMask:0},{t:1000,left:0,right:0,throttle:0,steer:0,limit:200,missingSideMask:0}],keyEvents:[{t:0,key:'f',type:'down',accepted:true,action},{t:1000,key:'f',type:'up'}]});});
  await f.click('[data-replay-node="node1"]');for(let i=0;i<400;i++)await f.step();
  assert(f.writes.some(w=>w.pulseUs===Math.round(500+180*2000/270)));assert(!f.writes.some(w=>w.pulseUs===2500));
  f.panel.cancel('手动接管');const n=f.writes.length;await f.step(100);assert.equal(f.writes.length,n);assert.equal(f.panel.timeline.active,false);
});
test('sequential nodes survive their internal STOP, while external STOP or a changed session never resumes',async t=>{
  const f=panelFixture(t);f.panel.library.edit(d=>{d.nodes[0].trace=trace();d.nodes.push({id:'node2',name:'节点2',trace:trace()});});
  await f.document.getElementById('replayAll').onclick();for(let i=0;i<20;i++)await f.step();
  assert.equal(f.panel.replay.active,false);assert.match(f.panel.replay.message,/全部节点/);assert(f.stops()>=2);
  await f.click('[data-replay-node="node1"]');f.c.state.session++;await f.step();assert.equal(f.panel.isRunning(),false);
  const n=f.drives.length;await f.step();assert.equal(f.drives.length,n);
});

test('manual arm key retains target across recoverable lock; STOP cancels it',async t=>{
  const f=panelFixture(t);f.panel.library.installArmPresets();
  f.panel.keyEvent('down',{key:'g'});await f.step();assert(f.panel.actions.active);
  f.panel.onControllerLock({recoverable:true,reason:'通信会话恢复'});f.c.state.session=8;f.c.stopGeneration++;
  f.c.servoStatus.session=8;f.c.servoMotion.session=8;
  for(let i=0;i<450;i++)await f.step();
  assert(f.writes.some(w=>w.channel===0&&w.pulseUs===1352));
  f.panel.onControllerLock({reason:'用户停止'});const count=f.writes.length;await f.step();assert.equal(f.writes.length,count);
});


test('shortcut slider defaults to 80us/s and changes live pacing without starting motion',async t=>{
  const f=panelFixture(t),slider=f.document.getElementById('shortcutSpeed');
  assert.equal(slider.value,'80');assert.equal(f.panel.actions.speedUsS,80);
  slider.value='20';slider.dispatchEvent(new f.window.Event('input'));
  assert.equal(f.panel.actions.speedUsS,20);
  assert.equal(f.document.getElementById('shortcutSpeedValue').textContent,'20 μs/s');
  assert.equal(f.writes.length,0);assert.equal(f.stops(),0);assert(!f.panel.actions.active);
  f.panel.keyEvent('down',{key:'f'});await f.step();
  slider.value='50';slider.dispatchEvent(new f.window.Event('input'));
  assert.equal(f.panel.actions.speedUsS,50);assert(f.panel.actions.active);
});
