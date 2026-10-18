/* Offline DOM/netlist tests. No browser, network, Bluetooth or hardware actions. */
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {parseHTML}=require(process.env.WIRING_DOM_LIB||'linkedom');
const source=fs.readFileSync(path.join(__dirname,'chassis-wiring-editor.html'),'utf8');
function boot({storage=new Map(),html=source}={}){
 const {window,document}=parseHTML(html),downloads=[],timers=[];
 Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option'))?.value||'';},set(value){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===String(value));}});
 for(const d of document.querySelectorAll('dialog')){d.showModal=()=>d.setAttribute('open','');d.close=()=>d.removeAttribute('open');}
 const context=vm.createContext({window,document,console,Blob,URL:{createObjectURL:b=>{downloads.push(b);return'blob:test';},revokeObjectURL:()=>{}},setTimeout:f=>{timers.push(f);return timers.length;},clearTimeout:()=>{},__timers:timers,localStorage:{getItem:k=>storage.get(k)??null,setItem:(k,v)=>storage.set(k,v)}});
 vm.runInContext(html.match(/<script>\s*([\s\S]*?)<\/script>/)[1],context,{timeout:10000});
 return{api:window.chassisWiringEditor,window,document,storage,downloads,click:id=>document.getElementById(id).click(),get:id=>document.getElementById(id),route:()=>vm.runInContext('__timers.shift()()',context,{timeout:30000})};
}
test('original stylesheet and editing layout are retained',()=>{
 const baseline=path.join(__dirname,'original-style.css');
 const lf=s=>s.replace(/\r\n/g,'\n');
 assert.equal(lf(source.match(/<style>([\s\S]*?)<\/style>/)[1]),lf(fs.readFileSync(baseline,'utf8')));
 const h=boot(),m=h.api.getModel();assert.equal(m.modules.length,20);assert.equal(m.wires.length,82);assert.deepEqual([...h.api.validate(m)],[]);
 assert(h.get('edit-mode'));assert(h.get('connect-mode'));assert(h.get('relay-help'));
});
test('2S relay switches boost input; buck follows 12V boost; receiver supply precedes contacts',()=>{
 const m=boot().api.getModel(),pairs=new Set(m.wires.map(w=>w.from+'>'+w.to));
 for(const p of['battery.POS>fuse.IN','fuse.OUT>relay.COM','fuse.OUT>relay.VPLUS','relay.NO>boost.IN_POS','boost.OUT_POS>buck.IN_POS','boost.OUT_POS>tb1.VM','boost.OUT_POS>tb2.VM','relay.VMINUS>ground.RELAY','boost.IN_NEG>ground.BOOST_IN','boost.OUT_NEG>ground.BOOST_OUT'])assert(pairs.has(p),p);
 assert(!pairs.has('relay.NO>relay.VPLUS'));assert(!pairs.has('relay.NO>buck.IN_POS'));
 assert(!m.wires.some(w=>w.from==='relay.NC'||w.to==='relay.NC'));
 const mods=Object.fromEntries(m.modules.map(m=>[m.id,m]));assert.equal(mods.battery.pins[0].voltage,7.4);assert.equal(mods.boost.pins.find(p=>p.id==='OUT_POS').voltage,12);assert.equal(mods.buck.pins.find(p=>p.id==='OUT_POS').voltage,5);assert.equal(mods.buck.pins.find(p=>p.id==='IN_POS').voltage,12);
});
test('relay back photo, physical orientation and all five ports are retained',()=>{
 const h=boot(),m=h.api.getModel().modules.find(m=>m.id==='relay');
 assert.deepEqual([...m.pins.map(p=>p.label)],['NC','COM','NO','+V','−V']);assert(m.note.includes('背面'));assert(m.note.includes('镜像'));
 assert(h.get('relay-help').querySelector('img').src.startsWith('data:image/jpeg;base64,'));assert(h.get('relay-help').querySelector('img').style.transform.includes('180'));
});
test('current measured wheel names and current servo GPIOs survive style restoration',()=>{
 const h=boot(),m=h.api.getModel();
 for(const[id,pos]of[['m1','右前'],['m2','左前'],['m3','右后'],['m4','左后']])assert(m.modules.find(m=>m.id===id).title.includes(pos));
 assert(m.wires.some(w=>w.from==='esp.GPIO12'&&w.to==='servo.ARM'));assert(m.wires.some(w=>w.from==='esp.GPIO13'&&w.to==='servo.FOREARM'));
 assert(m.wires.some(w=>w.from==='esp.GPIO14'&&w.to==='servo.DOOR'));
 assert(m.modules.find(m=>m.id==='ps2').note.includes('备选'));
});
test('all physical ports fit and module rectangles do not overlap',()=>{
 const h=boot(),mods=h.api.getModel().modules;
 assert.deepEqual([...h.api.validate(h.api.getModel())],[]);
 for(let i=0;i<mods.length;i++)for(let j=i+1;j<mods.length;j++){
  const a=mods[i],b=mods[j];assert(!(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h),a.id+' overlaps '+b.id);
 }
});
test('all wires route; M1 filter now matches lowercase module ids',()=>{
 const h=boot();h.route();const routes=Object.values(h.api.getRoutes());assert.equal(routes.length,82);assert.equal(routes.filter(r=>!r.routed).length,0);
 h.api.selectFilter('M1');assert(h.document.querySelectorAll('.net-button').length>=4);
 h.api.selectFilter('encoder');assert.equal(h.document.querySelectorAll('.net-button').length,8);
});
test('unsafe positive-to-ground imports are rejected without altering the plan',()=>{
 const h=boot(),m=h.api.getModel();m.wires.push({id:'bad',from:'battery.POS',to:'stm.GND',kind:'battery',group:'power'});
 assert.throws(()=>h.api.importModel(m),/地线/);assert.equal(h.api.getModel().wires.length,82);
});
test('edit, undo, redo and matching-build cache still work',()=>{
 const h=boot();h.api.selectModule('relay');const old=h.get('module-title').value;
 h.get('module-title').value='继电器测试';h.click('apply-module');assert.equal(h.api.getModel().modules.find(m=>m.id==='relay').title,'继电器测试');
 h.click('undo');assert.equal(h.api.getModel().modules.find(m=>m.id==='relay').title,old);h.click('redo');
 assert.equal(boot({storage:h.storage}).api.getModel().modules.find(m=>m.id==='relay').title,'继电器测试');
});
test('old cache cannot replace confirmed 2S plan',()=>{
 const storage=new Map(),key='crtc-chassis-wiring-editor-v1-base';storage.set(key,JSON.stringify({buildId:'old',model:{}}));
 const h=boot({storage});assert(h.api.getModel().modules.some(m=>m.id==='boost'));assert(storage.has(key+'-backup'));
});
test('exported standalone HTML reloads and keeps its build id and correct supply net',async()=>{
 const h=boot();h.click('export-html');const exported=await h.downloads[0].text();const next=boot({html:exported});
 assert.equal(next.api.getModel().wires.length,82);assert.equal(next.api.getState().buildId,h.api.getState().buildId);assert(exported.length<source.length+100000);
});
if(process.argv.includes('--preview'))test('render actual original-style SVG for visual review',()=>{
 const h=boot();h.route();h.api.selectFilter('power');const svg=h.get('scene');
 const style=h.document.createElementNS('http://www.w3.org/2000/svg','style');style.textContent='.route{fill:none;stroke-linecap:round;stroke-linejoin:round}.wire-hit{fill:none;stroke:transparent;stroke-width:19}.node-title{font-size:29px;font-weight:700;fill:#fff}.node-subtitle{font-size:20px;fill:#e1e9e2}.pin-label{font-size:24px;font-weight:600;fill:#fff}.pin-label.outside{fill:#23392f;stroke:#edf2e7;stroke-width:5;paint-order:stroke}.figure-label{font-size:30px;fill:#506a58;font-weight:600}';svg.prepend(style);svg.setAttribute('xmlns','http://www.w3.org/2000/svg');
 const data=svg.outerHTML;fs.writeFileSync(path.join(__dirname,'overview.svg'),data);
 const {Resvg}=require('@resvg/resvg-js');const png=new Resvg(data,{background:'#edf2e7',fitTo:{mode:'width',value:1600},font:{defaultFontFamily:'Microsoft YaHei'}}).render().asPng();fs.writeFileSync(path.join(__dirname,'overview.png'),png);assert(png.length>10000);
});
