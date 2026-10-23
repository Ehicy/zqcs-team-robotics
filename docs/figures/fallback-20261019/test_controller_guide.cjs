/* Offline teaching-page tests; no device connections. */
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {parseHTML}=require(process.env.WIRING_DOM_LIB||'linkedom');
const html=fs.readFileSync(path.join(__dirname,'../../../firmware/web/fallback-controller-guide.html'),'utf8');
function boot(){
 const {window,document}=parseHTML(html),timers=new Map();let id=0;
 Object.defineProperty(window.HTMLSelectElement.prototype,'value',{configurable:true,get(){return(this.querySelector('option[selected]')||this.querySelector('option')).value;},set(v){for(const o of this.querySelectorAll('option'))o.toggleAttribute('selected',o.value===v);}});
 const ctx=vm.createContext({document,setTimeout:(f,ms)=>{timers.set(++id,{f,ms});return id;},clearTimeout:id=>timers.delete(id)});
 vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],ctx);
 return {document,timers,get:id=>document.getElementById(id),scenario(v){this.get('scenario').value=v;this.get('scenario').dispatchEvent(new window.Event('change'));},tick(){const [id,t]=timers.entries().next().value;timers.delete(id);t.f();return t.ms;}};
}
test('guide is standalone, includes full controls and has no hardware API',()=>{
 assert(!html.includes('%%'));assert(!/navigator\.(bluetooth|serial|usb)|fetch\s*\(|XMLHttpRequest|WebSocket/.test(html));
 assert(html.includes('href="fallback-wiring.html"'));assert(html.includes('13 项参数'));
 for(const text of ['R1＋SELECT','500ms','1300','2500','PC13','L3','30 秒'])assert(html.includes(text),text);
});
test('every storyboard selects existing keys, steps forwards/backwards, and ends LED playback',()=>{
 const h=boot();
 for(const [name,n] of Object.entries({drive:4,tune:5,servo:4,direction:3,read:3,reset:2,l3:2})){
  h.scenario(name);assert(h.get('prev').disabled);
  for(let i=0;i<n;i++){
   assert.equal(h.get('step-count').textContent,`步骤 ${i+1} / ${n}`);
   assert(h.get('step-title').textContent);assert(h.get('instruction').textContent);assert(h.get('expected').textContent);
   if(!h.get('play-led').disabled){h.get('play-led').click();let count=0;while(h.timers.size){h.tick();assert(++count<100);}assert(!h.get('demo-led').classList.contains('on'));}
   if(i<n-1)h.get('next').click();
  }
  assert(h.get('next').disabled);h.get('prev').click();assert.equal(h.get('step-count').textContent,`步骤 ${n-1} / ${n}`);
 }
 h.scenario('tune');assert.deepEqual([...h.document.querySelectorAll('.key.hot')].map(e=>e.dataset.key).sort(),['SELECT','START']);
 for(let i=0;i<4;i++)h.get('next').click();assert.deepEqual([...h.document.querySelectorAll('.key.hot')].map(e=>e.dataset.key).sort(),['R1','SELECT']);
 h.scenario('l3');assert.deepEqual([...h.document.querySelectorAll('.key.hot')].map(e=>e.dataset.key),['L3']);
 h.get('play-led').click();assert(!h.get('demo-led').classList.contains('on'));assert.equal(h.tick(),80);assert(h.get('demo-led').classList.contains('on'));assert.equal(h.tick(),200);assert(!h.get('demo-led').classList.contains('on'));h.scenario('drive');assert.equal(h.timers.size,0);
});
