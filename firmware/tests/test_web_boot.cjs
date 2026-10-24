/* Offline DOM boot of both real ES-module entrypoints. No browser/BLE/network. */
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const {parseHTML}=require(process.env.WIRING_DOM_LIB||'linkedom');
async function boot(name,entry,initialStore=null){
 const base=path.resolve(__dirname,'../web'),{window,document}=parseHTML(fs.readFileSync(path.join(base,name),'utf8'));
 Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option'))?.value||'';},set(v){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===String(v));}});
 window.isSecureContext=true;document.hasFocus=()=>true;Object.defineProperty(document,'hidden',{value:false,configurable:true});
 const storageReads=[],timers=[],store=initialStore||new Map();
 const globals={window,document,console,URL,Blob,performance,TextEncoder,TextDecoder,Uint8Array,ArrayBuffer,DataView,
   navigator:{bluetooth:{}},location:{hostname:'localhost',protocol:'http:'},isSecureContext:true,
   setInterval:(f,ms)=>{timers.push({f,ms});return timers.length;},clearInterval:()=>{},setTimeout:()=>0,clearTimeout:()=>{},
   requestAnimationFrame:()=>0,cancelAnimationFrame:()=>{},ResizeObserver:class{observe(){}disconnect(){}},
   localStorage:{getItem:k=>{storageReads.push(k);return store.get(k)??null;},setItem:(k,v)=>store.set(k,v)}};
 const context=vm.createContext(globals),cache=new Map();
 function module(file){
  file=file.split(/[?#]/)[0];
  if(cache.has(file))return cache.get(file);
  // Expose the real wiring only inside this offline VM, not in the shipped page.
  const source=fs.readFileSync(file,'utf8')+(path.basename(file)==='dashboard.js'?'\nexport {controller,automation,connect,sameCar,initializer};':'');
  const m=new vm.SourceTextModule(source,{context,identifier:file});cache.set(file,m);return m;
 }
 const main=module(path.join(base,entry));
 await main.link((specifier,ref)=>module(path.resolve(path.dirname(ref.identifier),specifier)));await main.evaluate({timeout:5000});
 assert.equal(document.getElementById('pidKp').value,'1000');assert.equal(document.getElementById('pidKi').value,'2000');assert.equal(document.getElementById('pidKd').value,'0');
 assert(document.getElementById('writePid').disabled);
 assert.equal(document.querySelectorAll('#pidPanel input[type="range"]').length,3);
 assert(document.getElementById('writePid').hidden);
 assert(document.getElementById('servoValue2'));assert(document.getElementById('doorOpenButton').disabled);
 assert.equal(document.querySelectorAll('input.servo-angle[type="range"]').length,3);
 assert.equal(document.querySelectorAll('#servoSetupConfirmed,#doorConfirmed').length,0);
 for(let i=0;i<3;i++)assert(document.getElementById('servoReference'+i));
 assert.equal(document.getElementById('servoReference0').value,'135');
 assert.equal(document.getElementById('servoAngle0').getAttribute('min'),entry==='dashboard.js'?'0':'108');
 assert.equal(document.getElementById('servoAngle0').getAttribute('max'),entry==='dashboard.js'?'270':'162');
 assert(document.querySelector('#servoPanel legend').textContent.includes('270°位置型'));
 for(let i=0;i<3;i++)assert(document.getElementById('servoAngle'+i).disabled);
 assert.equal(document.getElementById('doorClosed').value,'500');assert.equal(document.getElementById('doorOpen').value,'2500');
 assert.equal(document.querySelectorAll('[id$="_kp"],[id$="_ki"]').length,0);
 assert.equal(document.querySelectorAll('[id$="_max"][max="1000"]').length,4);
 for(const id of ['pidKp','pidKi','pidKd','writePid'])assert.equal(document.querySelectorAll('#'+id).length,1);
 if(entry==='dashboard.js'){
  assert(!storageReads.some(k=>k.startsWith('crtc-c3-pa-encoder')));
  assert(document.getElementById('rememberButton').hidden);
  assert.equal(document.getElementById('pidPercent').value,'100');assert(document.getElementById('pidTestStart').disabled);
  assert.equal(document.querySelectorAll('#pidTestPanel input[type="range"]').length,3);
  assert(document.querySelector('#pidTestPanel details').textContent.includes('Kp'));
  assert.equal(window.crtcAgent.status().state.connected,false);
  assert.equal(document.getElementById('wheel2_motor').value,'1');
  assert(document.getElementById('automationPanel'));
  assert.equal(window.crtcMotion.status().replaying,false);
  if(initialStore){assert.equal(document.getElementById('wheel2_diameter').value,'65');assert.equal(document.getElementById('wheel2_cpr').value,'884');}
  else {await keyboardIntegration({window,document,main,timers});await bluetoothIntegration({window,document,main});}
 }
 console.log('PASS offline DOM boot:',name,'(PID controls, no automatic movement, no BLE calls)');
 return store;
}
async function keyboardIntegration({window,document,main,timers}){
 const {controller:c,automation:a}=main.namespace;
 let time=100;const writes=[];
 c.now=()=>time;c.queue.setWriter(async bytes=>writes.push(new Uint8Array(bytes)));
 c.queue.now=c.acks.now=c.now;c.lastTick=time;
 Object.assign(c.state,{connected:true,uart:true,session:71,pwmSupported:true});
 c.mainStatusAt=time;
 const flush=()=>new Promise(r=>setImmediate(r));
 const click=async selector=>{document.querySelector(selector).dispatchEvent(new window.Event('click',{bubbles:true}));await flush();};
 const key=(type,value,target=document.body,props={})=>{
  time+=5;const event=new window.Event(type,{bubbles:true,cancelable:true});
  Object.assign(event,{key:value,repeat:false,ctrlKey:false,metaKey:false,altKey:false,...props});target.dispatchEvent(event);
 };
 const controlTick=timers.find(t=>t.ms===25).f;
 // A fresh press after an idle scheduling gap must survive the next interval.
 // Previously PWM was sent first, then the old global 150ms check erased W.
 time+=1000;key('keydown','w');await flush();controlTick();await flush();
 assert.equal(window.__crtcDashboard.intent().throttle,1000,'first press after idle is not cleared by an earlier timer gap');
 for(let i=0;i<80;i++){
  time+=25;
  c.ingest({kind:0,session:71,uart:true,ready:true,pidReady:true,armed:false,active:true,pwmSupported:true,calibratedMask:15,left:100,right:100},c.generation);
  controlTick();await flush();
 }
 assert.equal(window.__crtcDashboard.intent().throttle,1000,'status notifications do not erase a held key');
 assert(writes.filter(b=>b[1]===11).every(b=>new DataView(b.buffer).getUint16(16,true)===191),'W default limit is 19.1%');
 assert(writes.filter(b=>b[1]===11).length>=35,'one keydown keeps renewing PWM for two seconds');
 assert(!writes.some(b=>b[1]===3),'no unsolicited STOP during the hold');
 key('keyup','w');await flush();assert.equal(writes.at(-1)[1],3);
 const released=writes.length;time+=25;controlTick();await flush();assert.equal(writes.length,released);
 for(const direction of ['a','d']){
   key('keydown',direction);await flush();
   assert.equal(new DataView(writes.at(-1).buffer).getUint16(16,true),218,'A/D default limit is 21.8%');
   key('keyup',direction);await flush();
 }
 // A gap DURING an old hold is different: do not transmit one stale PWM first.
 key('keydown','w');await flush();const beforeStall=writes.length;
 time+=175;controlTick();await flush();
 assert.deepEqual(writes.slice(beforeStall).map(b=>b[1]),[3],'interrupted hold stops before another PWM packet');
 key('keydown','w',document.body,{repeat:true});time+=25;controlTick();await flush();
 assert.equal(window.__crtcDashboard.intent().throttle,0,'repeat cannot resurrect an interrupted hold');
 key('keyup','w');Object.assign(c.state,{active:false});writes.length=0;
 await click('[data-record-node="node1"]');assert(a.recorder.active);
 key('keydown','w');key('keydown','w',document.body,{repeat:true});key('keyup','w');
 assert.deepEqual(JSON.parse(JSON.stringify(a.recorder.trace.keyEvents.map(e=>[e.type,e.key]))),[['down','w'],['up','w']]);
 const wSamples=a.recorder.trace.samples;
 assert(wSamples.some(s=>s.throttle===1000),'short WASD tap records updated drive intent');
 assert.equal(wSamples.at(-1).throttle,0);
 const before=a.recorder.trace.keyEvents.length,field=document.querySelector('[data-key-name]');
 key('keydown','f',field);key('keyup','f',field);
 key('keydown','f',document.body,{ctrlKey:true});key('keyup','f'); // Ctrl may be released first.
 await click('[data-block-row="key0"][data-block-index="0"]');
 const dialogButton=document.getElementById('closeBlock');key('keydown','f',dialogButton);key('keyup','f',dialogButton);
 assert.equal(a.recorder.trace.keyEvents.length,before,'typing, shortcuts and open dialog never enter the recording');
 await click('#closeBlock');
 key('keydown','f');key('keyup','f');key('keydown','g');key('keyup','g');
 const f=a.recorder.trace.keyEvents.find(e=>e.key==='f'&&e.type==='down');
 assert.equal(f.accepted,true);assert.equal(f.action.length,2);
 assert.equal(a.recorder.trace.keyEvents.find(e=>e.key==='g'&&e.type==='down').accepted,false,'real keyboard hook preserves the 2s rule');
 assert(a.timeline.active);key('keydown','a');key('keyup','a',document.querySelector('[data-key-name]'));
 assert(!a.timeline.active,'manual drive cancels pending function blocks');assert.equal(window.__crtcDashboard.intent().steer,0,'held drive key releases even after focus enters a field');
 key('keydown','f');key('keyup','f');key('keydown','Escape');key('keyup','Escape');
 assert(!a.timeline.active);assert(a.recorder.active,'STOP is recorded without ending the recording');
 await click('[data-finish-node="node1"]');
 const saved=JSON.parse(window.crtcMotion.export()).nodes[0].trace;
 assert(saved.keyEvents.some(e=>e.key==='escape'&&e.type==='down'));
 assert.equal(saved.samples.at(-1).throttle,0);assert.equal(saved.samples.at(-1).steer,0);
 assert.equal(saved.endEncoder.counts.length,4);assert.equal(a.recorder.active,false);
 await flush();assert(writes.some(b=>b[1]===11),'WASD enters the real PWM queue through a fake offline writer');
 assert(!writes.some(b=>b[1]===8||b[1]===10),'editing and recording alone do not issue servo commands');
 c.disconnect();
 console.log('PASS Dashboard keyboard integration: first press after idle, two-second held PWM + status, interrupted hold, short taps, input/dialog exclusion, 2s rule, takeover, STOP and saved node');
}
async function bluetoothIntegration({window,document,main}){
 const {controller:c,connect,sameCar:link,initializer}=main.namespace;
 const writes=[],retryTimers=[];let picks=0,connections=0,session=100,fail=false;
 const listeners=new Map();
 const bytes=new Uint8Array(20);bytes[0]=1;bytes[2]=1|8|32|64;bytes[18]=15;
 const status={properties:{read:true,notify:true},addEventListener(){},removeEventListener(){},startNotifications:async()=>{},readValue:async()=>{new DataView(bytes.buffer).setUint32(4,session,true);return bytes;}};
 const control={properties:{write:true},writeValueWithResponse:async b=>writes.push(new Uint8Array(b))};
 const device={id:'physical-car',name:link.expectedName,addEventListener:(k,f)=>listeners.set(k,f),removeEventListener:k=>listeners.delete(k)};
 device.gatt={connected:false,connect:async()=>{connections++;if(fail)throw Error('out of range');device.gatt.connected=true;return device.gatt;},disconnect:()=>{device.gatt.connected=false;listeners.get('gattserverdisconnected')?.();},getPrimaryService:null};
 // Inspect known protocol source constants in this offline test module only.
 const protocol=fs.readFileSync(path.join(__dirname,'../web/protocol.js'),'utf8');
 const controlUuid=protocol.match(/control:\s*'([^']+)'/)[1];
 device.gatt.getPrimaryService=async()=>({getCharacteristic:async id=>id===controlUuid?control:status});
 link.bluetooth={requestDevice:async options=>{picks++;assert.equal(options.filters[0].name,link.expectedName);return device;},getDevices:async()=>[device]};
 link.setTimer=(f,ms)=>{const t={f,ms};retryTimers.push(t);return t;};link.clearTimer=t=>t.cancelled=true;
 await connect();assert(c.state.connected);assert.equal(picks,1);assert.equal(writes.length,0,'connect sends no movement/servo defaults');
 // A stale old held key is explicitly dropped by disconnection.
 const key=new window.Event('keydown',{bubbles:true,cancelable:true});Object.assign(key,{key:'w'});document.body.dispatchEvent(key);
 device.gatt.disconnect();assert(!c.state.connected);assert.equal(window.__crtcDashboard.intent().throttle,0);assert(!initializer.active);
 const afterDrop=writes.length;session++;await retryTimers.at(-1).f();
 // Retry callback is intentionally fire-and-forget; drain finite microtasks.
 for(let i=0;i<8;i++)await new Promise(r=>setImmediate(r));
 assert(c.state.connected);assert.equal(picks,1);assert.equal(writes.length,afterDrop,'reconnect does not replay W or any servo command');
 await connect();assert(!c.state.connected);assert.equal(link.enabled,false,'manual disconnect pauses retries');
 const before=connections;for(const t of retryTimers)if(!t.cancelled)await t.f();assert.equal(connections,before);
 link.enabled=true;fail=true;await connect(true);assert(!c.state.connected);assert(retryTimers.length>=2);assert.equal(picks,1);
 link.pause();
 console.log('PASS real Dashboard BLE wiring (mock GATT): first selection, disconnect -> exact-car retry, no stale W/servo replay, manual pause, failed retry');
}
(async()=>{
 await boot('dashboard.html','dashboard.js');await boot('index.html','app.js');
 const old=Array.from({length:4},(_,wheel)=>({wheel,motorNegative:wheel%2===1,encoderNegative:wheel%2===0,confirmed:false,diameterTenthMm:(65+wheel)*10,countsPerRev:wheel===2?900:1768,kpMilli:1000,kiMilli:2000,kdMilli:0,maxMmS:300}));
 const store=new Map([['crtc-c3-pa-encoder-calibration-v1',JSON.stringify(old)]]);
 await boot('dashboard.html','dashboard.js',store);await boot('dashboard.html','dashboard.js',store);
 console.log('PASS C3 settings ignore old browser storage on repeated refresh');
})().catch(e=>{console.error(e);process.exitCode=1;});
