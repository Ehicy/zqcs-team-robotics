/* Named commanded positions, poses, action graphs and teach traces.
 * Local browser storage only; loading/importing never executes anything. */
export const LIBRARY_KEY='crtc-motion-library-270-v1';
export const SERVO_SPANS=[270,180,180];
export const SERVO_PINS=[7,6,10];
export const servoMin=ch=>500;
export const servoMax=ch=>2500;
export function pulseFromAngle(ch,angle){
  const pulse=500+Number(angle)*2000/SERVO_SPANS[ch];
  return validatePulse(ch,Math.round(pulse));
}
export function angleFromPulse(ch,pulse){return (pulse-500)*SERVO_SPANS[ch]/2000;}
export function validatePulse(ch,pulse){
  if(!Number.isInteger(ch)||ch<0||ch>2||!Number.isInteger(pulse)||pulse<servoMin(ch)||pulse>servoMax(ch))throw Error('舵机通道或位置脉宽无效');
  return pulse;
}
const copy=x=>JSON.parse(JSON.stringify(x));
function nameOf(name){if(typeof name!=='string'||!name.trim()||name.trim().length>64)throw Error('名称须为1–64个字符');return name.trim();}
export function validateProgram(steps){
  if(!Array.isArray(steps)||!steps.length||steps.length>100)throw Error('动作须有1–100个积木');
  return steps.map((s,i)=>{
    validatePulse(s.channel,s.pulseUs);
    const t=s.trigger||{after:null,event:'start',delayMs:0};
    if(t.after!==null&&(!Number.isInteger(t.after)||t.after<0||t.after>=i))throw Error('积木只能依赖前面的积木');
    if(!['start','done','position'].includes(t.event)||!Number.isFinite(t.delayMs)||t.delayMs<0||t.delayMs>60000)throw Error('触发条件无效');
    if(t.event==='position'&&t.after!==null)validatePulse(steps[t.after].channel,t.thresholdUs);
    const settleMs=s.settleMs??300;
    if(!Number.isFinite(settleMs)||settleMs<0||settleMs>10000)throw Error('到位等待须为0–10秒');
    return {channel:s.channel,pulseUs:s.pulseUs,settleMs,trigger:{after:t.after,event:t.event,delayMs:t.delayMs,thresholdUs:t.thresholdUs??1500}};
  });
}
export function validateTrace(trace){
  if(!trace||!Array.isArray(trace.samples)||trace.samples.length<2||trace.samples.length>20000)throw Error('路径采样须为2–20000条');
  if(!Array.isArray(trace.calibration)||trace.calibration.length!==4||trace.calibration.some(c=>!Number.isFinite(c.cpr)||c.cpr<1||c.cpr>1e6||!Number.isFinite(c.diameterMm)||c.diameterMm<=0||c.diameterMm>500||![1,-1].includes(c.direction)))throw Error('路径轮参数无效');
  if(!Number.isFinite(trace.trackWidthMm)||trace.trackWidthMm<50||trace.trackWidthMm>1000)throw Error('路径轮距无效');
  let time=-1;
  for(const s of trace.samples){
    if(!Number.isFinite(s.t)||s.t<=time||s.t>1800000||![s.left,s.right,s.throttle,s.steer,s.limit].every(Number.isFinite)||Math.abs(s.throttle)>1000||Math.abs(s.steer)>1000||s.limit<0||s.limit>1000||Math.abs(s.left)>1e7||Math.abs(s.right)>1e7)throw Error('路径采样字段无效');time=s.t;
    if(s.missingSideMask!==undefined&&(!Number.isInteger(s.missingSideMask)||s.missingSideMask<0||s.missingSideMask>3))throw Error('逐侧质量掩码无效');
  }
  if(!Array.isArray(trace.servos)||trace.servos.length>2000)throw Error('路径舵机事件无效');
  let previousEvent=-1;for(const e of trace.servos){validatePulse(e.channel,e.pulseUs);if(!Number.isFinite(e.t)||e.t<previousEvent||e.t<0||e.t>time||e.referenceUs&&validatePulse(e.channel,e.referenceUs)!==e.referenceUs)throw Error('舵机事件无效');previousEvent=e.t;}
  return copy(trace);
}
export class MotionLibrary{
  constructor(storage=globalThis.localStorage){this.storage=storage;this.data={version:1,positions:[],poses:[],programs:[],routes:[]};this.error='';try{const raw=storage?.getItem(LIBRARY_KEY);if(raw)this.data=this.validate(JSON.parse(raw));}catch(e){this.error='存储未载入：'+e.message;}}
  validate(data){
    if(data?.version!==1)throw Error('资料版本不符');
    const d=copy(data);
    for(const kind of ['positions','poses','programs','routes']){
      if(!Array.isArray(d[kind])||d[kind].length>100)throw Error('记录数量无效');
      const names=new Set();for(const r of d[kind]){r.name=nameOf(r.name);if(names.has(r.name))throw Error('名称重复');names.add(r.name);
        if(kind==='positions')validatePulse(r.channel,r.pulseUs);
        if(kind==='poses'){if(!Array.isArray(r.pulses)||r.pulses.length!==3)throw Error('姿态须包含三路');r.pulses.forEach((p,ch)=>validatePulse(ch,p));}
        if(kind==='programs')r.steps=validateProgram(r.steps);
        if(kind==='routes')r.trace=validateTrace(r.trace);
      }
    }return d;
  }
  commit(data){const next=this.validate(data),json=JSON.stringify(next);if(json.length>4000000)throw Error('资料超过4MB，请导出后删除旧路径');if(!this.storage)throw Error('浏览器存储不可用');this.storage.setItem(LIBRARY_KEY,json);this.data=next;}
  save(kind,name,record){if(!Object.hasOwn(this.data,kind)||kind==='version')throw Error('记录类型无效');const d=copy(this.data),r={...copy(record),name:nameOf(name)},i=d[kind].findIndex(x=>x.name===r.name);if(i<0)d[kind].push(r);else d[kind][i]=r;this.commit(d);return copy(r);}
  remove(kind,name){const d=copy(this.data);d[kind]=d[kind].filter(r=>r.name!==name);this.commit(d);}
  export(){return JSON.stringify(this.data,null,2);}
  import(json){this.commit(this.validate(JSON.parse(json)));}
}
