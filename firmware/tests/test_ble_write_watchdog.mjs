import test from 'node:test';
import assert from 'node:assert/strict';
import {createBleWriter,BLE_COMMAND_ACK_MS} from '../web/ble-write-watchdog.js';
import {WriteQueue,RemoteController} from '../web/protocol.js';
const flush=()=>new Promise(r=>setImmediate(r));
function fixture(){
  let now=0,warnings=0;const timers=[],writes=[],errors=[];
  const writer=createBleWriter({write:bytes=>new Promise((resolve,reject)=>writes.push({bytes,resolve,reject})),
    onSlow:()=>warnings++,setTimer:(fn,ms)=>{const t={fn,at:now+ms};timers.push(t);return t;},clearTimer:t=>{if(t)t.cancelled=true;}});
  return {writer,writes,errors,warnings:()=>warnings,advance:async ms=>{now+=ms;for(const t of timers)if(!t.cancelled&&!t.fired&&t.at<=now){t.fired=true;t.fn();}await flush();},
    queue:new WriteQueue({now:()=>now,onError:e=>errors.push(e)})};
}
test('900ms GATT response succeeds without 600ms disconnect, retry or overlapping writes',async()=>{
  const f=fixture();f.queue.setWriter(f.writer);
  const first=f.queue.offer(Uint8Array.of(1));const second=f.queue.offer(Uint8Array.of(2),{expiresAt:2000});await flush();
  await f.advance(600);assert.equal(f.warnings(),1);assert.equal(f.errors.length,0);assert.equal(f.writes.length,1);
  await f.advance(300);f.writes[0].resolve();await first;await flush();assert.equal(f.writes.length,2);
  f.writes[1].resolve();await second;await f.advance(2000);assert.equal(f.errors.length,0);
});
test('hung write has bounded 1500ms failure; late resolution cannot re-open a disconnected queue',async()=>{
  const f=fixture();f.queue.onError=e=>{f.errors.push(e);f.queue.setWriter(null);};f.queue.setWriter(f.writer);
  const pending=f.queue.offer(Uint8Array.of(1));const rejected=assert.rejects(pending,/1500ms/);await flush();
  await f.advance(1500);await rejected;assert.equal(f.errors.length,1);
  f.writes[0].resolve();await flush();assert.equal(f.queue.writer,null);assert.equal(f.writes.length,1);
});
test('STOP clears queued actions but waits behind the single GATT write',async()=>{
  const f=fixture();f.queue.setWriter(f.writer);const first=f.queue.offer(Uint8Array.of(1));await flush();
  await f.advance(700);const stale=f.queue.offer(Uint8Array.of(2));const dropped=assert.rejects(stale,/停止/);
  const stop=f.queue.offer(Uint8Array.of(3),{kind:'stop',expiresAt:2000});await dropped;
  assert.equal(f.writes.length,1);f.writes[0].resolve();await first;await flush();
  assert.deepEqual([...f.writes[1].bytes],[3]);f.writes[1].resolve();await stop;
});
test('Dashboard ACK budget covers transport grace without changing legacy default',()=>{
  assert.equal(new RemoteController().ackTimeoutMs,1500);
  assert.equal(new RemoteController({ackTimeoutMs:BLE_COMMAND_ACK_MS}).ackTimeoutMs,2500);
});
