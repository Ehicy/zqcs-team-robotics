import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

// Execute the real renderer offline. No browser, BLE connection, or motor writes.
const source=readFileSync(new URL('../web/dashboard.js',import.meta.url),'utf8');
const begin=source.indexOf('function renderTelemetry() {');
const end=source.indexOf('\nfunction paintBar(',begin);
assert(begin>=0 && end>begin);
function fixture({pwm=true,validMask=3,age=10,measured=[58,-58,999,-999],targets=[60,-60,60,-60]}={}) {
  const cells=new Map(),bars=Array.from({length:4},()=>({
    measured:{value:null},target:{value:null},caption:{textContent:''},
    querySelector(selector){return selector==='i.measured'?this.measured:selector==='i.target'?this.target:this.caption;},
  }));
  const cell=id=>{if(!cells.has(id))cells.set(id,{textContent:''});return cells.get(id);};
  const sandbox={performance:{now:()=>1000},controller:{state:{connected:true},mainStatusAt:990},
    encoderAt:990,encoder:{validMask:15,counts:[1,2,3,4]},speedsAt:1000-age,
    wheelSpeeds:{validMask,measured,targets},WHEELS:[0,1,2,3],$:cell,pwmSelected:()=>pwm,
    document:{querySelector:selector=>bars[Number(selector.slice(-1))]},
    paintBar:(element,value)=>{element.value=value;}};
  vm.runInNewContext('('+source.slice(begin,end)+')',sandbox)();
  return {cells,bars};
}
test('invalid rear feedback is unknown rather than fabricated 0 / 0',()=>{
  const {cells,bars}=fixture();
  assert.equal(cells.get('wheelSpeed0').textContent,'58 / —');
  assert.equal(cells.get('wheelSpeed1').textContent,'-58 / —');
  for(const wheel of [2,3]) {
    assert.equal(cells.get('wheelSpeed'+wheel).textContent,'— / —');
    assert.equal(bars[wheel].caption.textContent,'— / —');
    assert.equal(bars[wheel].measured.value,0);
    assert.equal(bars[wheel].target.value,0);
    assert.equal(cells.get('count'+wheel).textContent,String(wheel+1));
  }
});
test('PWM captions and bars do not present a speed-model estimate as a PWM command',()=>{
  const {bars}=fixture({validMask:15});
  assert.equal(bars[0].caption.textContent,'58 / —');
  assert.equal(bars[0].target.value,0);
  assert.equal(bars[1].caption.textContent,'-58 / —');
});
test('real zero measurements remain distinct from unknown measurements',()=>{
  const {bars}=fixture({validMask:1,measured:[0,0,0,0]});
  assert.equal(bars[0].caption.textContent,'0 / —');
  assert.equal(bars[1].caption.textContent,'— / —');
});
test('PID mode retains valid signed speed targets and hides expired samples',()=>{
  const valid=fixture({pwm:false,validMask:15});
  assert.equal(valid.cells.get('wheelSpeed3').textContent,'-999 / -60');
  assert.equal(valid.bars[3].caption.textContent,'-999 / -60');
  assert.equal(valid.bars[3].target.value,-60);
  const stale=fixture({pwm:false,validMask:15,age:1000});
  for(const bar of stale.bars)assert.equal(bar.caption.textContent,'— / —');
});
