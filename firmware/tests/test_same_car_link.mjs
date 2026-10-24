import test from 'node:test';
import assert from 'node:assert/strict';
import {SameCarLink} from '../web/same-car-link.js';
const name='CRTC-Lower-Car-AABBCCDDEEFF';
function setup(initial={id:'our-id',name}){
 const map=new Map(initial?[['crtc-bound-ble-device-v1',JSON.stringify(initial)]]:[]);
 const tasks=[];let picks=0;
 const bt={getDevices:async()=>[{id:'other-id',name},{id:'our-id',name}],requestDevice:async options=>{picks++;assert.deepEqual(options.filters,[{name}]);return{id:'our-id',name};}};
 const link=new SameCarLink({bluetooth:bt,expectedName:name,service:'service',storage:{getItem:k=>map.get(k),setItem:(k,v)=>map.set(k,v)},setTimer:(f,ms)=>{const t={f,ms};tasks.push(t);return t;},clearTimer:t=>{t.cancelled=true;}});
 return {link,bt,tasks,picks:()=>picks};
}
test('restore chooses exact authorized ID, not the first same-name car; auto never invokes picker',async()=>{
 const t=setup();assert.equal((await t.link.restore()).id,'our-id');assert.equal(t.picks(),0);
 t.link.device=null;t.bt.getDevices=async()=>[{id:'other-id',name}];assert.equal(await t.link.restore(),null);assert.equal(t.picks(),0);
});
test('portable identity rejects different car; first permission requires explicit choose',async()=>{
 const t=setup(null);assert.equal(await t.link.restore(),null);assert.equal(t.picks(),0);
 assert.throws(()=>t.link.remember({id:'bad',name:'CRTC-Lower-Car-000000000000'}));
 t.link.remember(await t.link.choose());assert.equal(t.picks(),1);
});
test('bounded retry 1/2/4/8/8s, single timer, manual pause, success reset',()=>{
 const t=setup();let retries=0;
 for(const delay of [1000,2000,4000,8000,8000]){t.link.schedule(()=>retries++);t.link.schedule(()=>assert.fail());assert.equal(t.tasks.at(-1).ms,delay);t.tasks.at(-1).f();}
 assert.equal(retries,5);assert.equal(t.picks(),0);
 t.link.schedule(()=>retries++);t.link.pause();t.tasks.at(-1).f();assert.equal(retries,5);
 const n=t.tasks.length;t.link.schedule(()=>{});assert.equal(t.tasks.length,n);
 t.link.enabled=true;t.link.succeeded();t.link.schedule(()=>{});assert.equal(t.tasks.at(-1).ms,1000);
});
test('old package car ID is not carried over to a different car package',async()=>{
 const t=setup({id:'old',name:'CRTC-Lower-Car-111111111111'});assert.equal(t.link.record,null);assert.equal(await t.link.restore(),null);
});

test('deliberate handoff remains paused after browser reload',async()=>{
 const map=new Map();const storage={getItem:k=>map.get(k),setItem:(k,v)=>map.set(k,v)};
 const a=new SameCarLink({bluetooth:{},storage,expectedName:name});a.remember({id:'our-id',name});a.pause();
 const b=new SameCarLink({bluetooth:{getDevices:async()=>[{id:'our-id',name}]},storage,expectedName:name});
 assert.equal(b.enabled,false);assert.equal((await b.restore()).id,'our-id');assert.equal(b.enabled,false);
 b.remember(b.device);assert.equal(b.enabled,true);
});
test('default browser timers never receive the SameCarLink object as their native receiver',()=>{
 const originalSet=globalThis.setTimeout,originalClear=globalThis.clearTimeout;let link,scheduled=0,cleared=0;
 try{
  globalThis.setTimeout=function(fn,ms){assert.notEqual(this,link,'Window.setTimeout rejects a foreign this');scheduled++;return 17;};
  globalThis.clearTimeout=function(id){assert.notEqual(this,link,'Window.clearTimeout rejects a foreign this');assert.equal(id,17);cleared++;};
  link=new SameCarLink({bluetooth:{},expectedName:name});link.remember({id:'car-id',name});link.schedule(()=>{});link.pause();
  assert.equal(scheduled,1);assert.equal(cleared,1);
 }finally{globalThis.setTimeout=originalSet;globalThis.clearTimeout=originalClear;}
});
