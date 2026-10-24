import test from 'node:test';
import assert from 'node:assert/strict';
import {ARM_PRESETS,DEFAULT_TRAVEL_PERMILLE,DEFAULT_TURN_PERMILLE} from '../web/dashboard-defaults.js';
import {WorkshopStore,KeyTimeline} from '../web/keyboard-workshop.js';
import {manualPwmLimit} from '../web/pwm-drive.js';

test('new browser defaults have confirmed channels, angles and key-relative timing',()=>{
  let writes=0;
  const store=new WorkshopStore({getItem:()=>null,setItem:()=>writes++});
  assert.equal(writes,0,'opening must not write or execute presets');
  assert.deepEqual(store.data.bindings.map(({id,...row})=>row),ARM_PRESETS);
  const sent=[];let now=1000;
  const timeline=new KeyTimeline({now:()=>now,send:events=>sent.push({at:now,events})});
  timeline.trigger(store.data.bindings[0]);timeline.tick();
  assert.deepEqual(sent[0].events.map(e=>[e.channel,e.pulseUs]),[[0,2219]]);
  now=1249;timeline.tick();assert.equal(sent.length,1);
  now=1250;timeline.tick();assert.deepEqual(sent[1].events.map(e=>[e.channel,e.pulseUs]),[[1,2167]]);
  now=4000;timeline.trigger(store.data.bindings[1]);timeline.tick();
  assert.deepEqual(sent[2].events.map(e=>[e.channel,e.pulseUs]),[[0,1352],[1,1456]]);
  now=7000;timeline.trigger(store.data.bindings[2]);timeline.tick();
  assert.deepEqual(sent[3].events.map(e=>[e.channel,e.pulseUs]),[[1,2133]]);
});

test('existing custom actions survive opening; restore updates F/G/H only',()=>{
  const existing=new WorkshopStore(null).data;
  existing.bindings[0].blocks[0].angle=100;
  existing.bindings.push({id:'extra',key:'j',name:'custom',blocks:[{channel:2,atMs:0,angle:90}]});
  let raw=JSON.stringify(existing),writes=0;
  const store=new WorkshopStore({getItem:()=>raw,setItem:(key,v)=>{raw=v;writes++;}});
  assert.deepEqual(store.data,existing);assert.equal(writes,0);
  store.installArmPresets();assert.equal(writes,1);
  assert.deepEqual(store.data.bindings.slice(0,3).map(({id,...r})=>r),ARM_PRESETS);
  assert.deepEqual(store.data.bindings[3],existing.bindings[3]);
  assert.deepEqual(store.data.nodes,existing.nodes);
});

test('21.8% turning is independent of 19.1% travel',()=>{
  assert.equal(manualPwmLimit({steer:0},DEFAULT_TRAVEL_PERMILLE,DEFAULT_TURN_PERMILLE),191);
  for(const steer of [-1000,1000])assert.equal(manualPwmLimit({steer},DEFAULT_TRAVEL_PERMILLE,DEFAULT_TURN_PERMILLE),218);
});
