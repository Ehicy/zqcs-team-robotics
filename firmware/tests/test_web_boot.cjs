/* Offline DOM boot of both real ES-module entrypoints. No browser/BLE/network. */
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const {parseHTML}=require(process.env.WIRING_DOM_LIB||'linkedom');
async function boot(name,entry){
 const base=path.resolve(__dirname,'../web'),{window,document}=parseHTML(fs.readFileSync(path.join(base,name),'utf8'));
 Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option'))?.value||'';},set(v){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===String(v));}});
 document.hasFocus=()=>true;Object.defineProperty(document,'hidden',{value:false,configurable:true});
 const timers=[],store=new Map();
 const globals={window,document,console,URL,Blob,performance,TextEncoder,TextDecoder,Uint8Array,ArrayBuffer,DataView,
   navigator:{},location:{hostname:'localhost',protocol:'http:'},isSecureContext:true,
   setInterval:f=>{timers.push(f);return timers.length;},clearInterval:()=>{},setTimeout:()=>0,clearTimeout:()=>{},
   requestAnimationFrame:()=>0,cancelAnimationFrame:()=>{},ResizeObserver:class{observe(){}disconnect(){}},
   localStorage:{getItem:k=>store.get(k)??null,setItem:(k,v)=>store.set(k,v)}};
 const context=vm.createContext(globals),cache=new Map();
 function module(file){
  if(cache.has(file))return cache.get(file);
  const m=new vm.SourceTextModule(fs.readFileSync(file,'utf8'),{context,identifier:file});cache.set(file,m);return m;
 }
 const main=module(path.join(base,entry));
 await main.link((specifier,ref)=>module(path.resolve(path.dirname(ref.identifier),specifier)));await main.evaluate({timeout:5000});
 assert.equal(document.getElementById('pidKp').value,'1000');assert.equal(document.getElementById('pidKi').value,'2000');assert.equal(document.getElementById('pidKd').value,'0');
 assert(document.getElementById('writePid').disabled);
 assert(document.getElementById('servoValue2'));assert(document.getElementById('doorOpenButton').disabled);
 assert.equal(document.querySelectorAll('input.servo-angle[type="range"]').length,3);
 for(let i=0;i<3;i++)assert(document.getElementById('servoAngle'+i).disabled);
 assert.equal(document.getElementById('doorClosed').value,'1000');assert.equal(document.getElementById('doorOpen').value,'2000');
 assert.equal(document.querySelectorAll('[id$="_kp"],[id$="_ki"]').length,0);
 assert.equal(document.querySelectorAll('[id$="_max"][max="1000"]').length,4);
 for(const id of ['pidKp','pidKi','pidKd','writePid'])assert.equal(document.querySelectorAll('#'+id).length,1);
 if(entry==='dashboard.js'){
  assert.equal(document.getElementById('pidPercent').value,'100');assert(document.getElementById('pidTestStart').disabled);
  assert(document.querySelector('#pidTestPanel details').textContent.includes('Kp'));
  assert.equal(window.crtcAgent.status().state.connected,false);
 }
 console.log('PASS offline DOM boot:',name,'(PID controls, no automatic movement, no BLE calls)');
}
(async()=>{await boot('dashboard.html','dashboard.js');await boot('index.html','app.js');})().catch(e=>{console.error(e);process.exitCode=1;});
