/* Offline DOM/netlist tests. No browser, network, Bluetooth or hardware actions. */
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {parseHTML}=require(process.env.WIRING_DOM_LIB||'linkedom');
const source=fs.readFileSync(path.join(__dirname,'../../../firmware/web/fallback-wiring.html'),'utf8');
function boot({storage=new Map(),html=source}={}){
 const {window,document}=parseHTML(html),downloads=[],timers=[];
 Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option'))?.value||'';},set(value){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===String(value));}});
 for(const d of document.querySelectorAll('dialog')){d.showModal=()=>d.setAttribute('open','');d.close=()=>d.removeAttribute('open');}
 const context=vm.createContext({window,document,console,Blob,URL:{createObjectURL:b=>{downloads.push(b);return'blob:test';},revokeObjectURL:()=>{}},setTimeout:f=>{timers.push(f);return timers.length;},clearTimeout:()=>{},__timers:timers,localStorage:{getItem:k=>storage.get(k)??null,setItem:(k,v)=>storage.set(k,v)}});
 vm.runInContext(html.match(/<script>\s*([\s\S]*?)<\/script>/)[1],context,{timeout:10000});
 return{api:window.chassisWiringEditor,window,document,storage,downloads,click:id=>document.getElementById(id).click(),get:id=>document.getElementById(id),route:()=>vm.runInContext('__timers.shift()()',context,{timeout:30000})};
}
test('fallback original editor: complete ports, valid model, no module overlaps',()=>{
 const h=boot(),m=h.api.getModel();assert.equal(m.modules.length,19);assert.equal(m.wires.length,62);assert.deepEqual([...h.api.validate(m)],[]);
 for(let i=0;i<m.modules.length;i++)for(let j=i+1;j<m.modules.length;j++){const a=m.modules[i],b=m.modules[j];assert(!(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h),a.id+' overlaps '+b.id);}
 for(const id of ['tb1','tb2'])assert.equal(m.modules.find(x=>x.id===id).pins.length,16);
 for(const id of ['stm','remote'])assert.equal(m.modules.find(x=>x.id===id).pins.filter(p=>/^(PA|PB|PC)\d+$/.test(p.id)).length,34);
 h.route();const routes=Object.values(h.api.getRoutes());assert.equal(routes.length,62);assert.equal(routes.filter(r=>!r.routed).length,0);
 h.api.selectFilter('ps2');assert.equal(h.document.querySelectorAll('.net-button').length,4);
});

test('photo side order, negative returns and unchanged motor firmware map',()=>{
 const m=boot().api.getModel(),mods=Object.fromEntries(m.modules.map(x=>[x.id,x]));
 const order=(id,side)=>mods[id].pins.filter(x=>x.side===side).map(x=>x.id).join(',');
 assert.equal(order('stm','right'),'GND2,GND3,V3_2,NRST,PB11,PB10,PB1,PB0,PA7,PA6,PA5,PA4,PA3,PA2,PA1,PA0,PC15,PC14,PC13,VBAT');
 assert.equal(order('remote','left'),'VBAT,PC13,PC14,PC15,PA0,PA1,PA2,PA3,PA4,PA5,PA6,PA7,PB0,PB1,PB10,PB11,NRST,V3,GND,GND2');
 assert(mods.remote.y<mods.stm.y&&mods.tb1.x>mods.stm.x&&mods.tb2.x>mods.stm.x);assert(!mods.ground);
 for(const id of ['stm','remote'])for(const side of ['left','right'])assert.equal(mods[id].pins.filter(x=>x.side===side).length,20);
 const pairs=new Set(m.wires.map(w=>w.from+'>'+w.to));
 for(const s of ['buck.IN_NEG>battery.NEG','boost.IN_NEG>battery.NEG','stm.GND>buck.OUT_NEG','remote.GND>buck.OUT_NEG','tb1.GND2>boost.OUT_NEG','tb2.GND2>boost.OUT_NEG','stm.V3>tb1.VCC','stm.V3>tb2.VCC'])assert(pairs.has(s),s);
 assert(mods.boost.internalGroundCommon&&mods.buck.internalGroundCommon);
});

test('7.4V input splits into independent boost and buck branches, not a cascade',()=>{
 const m=boot().api.getModel(),pairs=new Set(m.wires.map(w=>w.from+'>'+w.to));
 assert(pairs.has('relay.NO>boost.IN_POS'));assert(pairs.has('relay.NO>buck.IN_POS'));
 assert(!pairs.has('boost.OUT_POS>buck.IN_POS'));assert(!pairs.has('boost.OUT_NEG>buck.IN_NEG'));
 assert.equal(m.modules.find(x=>x.id==='buck').pins.find(x=>x.id==='IN_POS').voltage,7.4);
});
