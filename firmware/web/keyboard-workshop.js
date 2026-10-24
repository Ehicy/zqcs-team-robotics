import {ARM_PRESETS} from './dashboard-defaults.js?v=portable-20261024';
export {ARM_PRESETS};
import {pulseFromAngle,validateTrace,validatePulse,validateProgram} from './motion-library.js';
import {isDriveKey,normalizeKey} from './dashboard-input.js';
export const WORKSHOP_KEY='crtc-keyboard-nodes-v2';
const clone=x=>JSON.parse(JSON.stringify(x));
export function functionKey(value){
  const key=String(value).trim().toLowerCase();
  if(!/^[a-z0-9]$/.test(key)||isDriveKey(key))throw Error('功能键使用单个字母或数字，W/A/S/D留给底盘');
  return key;
}
export function validateBlocks(blocks){
  if(!Array.isArray(blocks)||!blocks.length||blocks.length>100)throw Error('每个功能需要1–100个方块');
  const result=blocks.map((b,i)=>{
    if(!Number.isInteger(b.channel)||b.channel<0||b.channel>2||!Number.isInteger(b.atMs)||b.atMs<0||b.atMs>600000)
      throw Error('舵机须为0–2，开始时间须为0–600秒（毫秒精度）');
    pulseFromAngle(b.channel,b.angle);
    if(!Number.isFinite(b.angle)||b.angle<0||b.angle>[270,180,180][b.channel])throw Error('目标角度超出舵机范围');
    const out={channel:b.channel,atMs:b.atMs,angle:b.angle};
    if(b.trigger){
      const t=b.trigger;
      if(!Number.isInteger(t.after)||t.after<0||t.after>=i||!['start','done'].includes(t.event)||!Number.isInteger(t.delayMs)||t.delayMs<0||t.delayMs>60000)throw Error('关联动作须选择前面的方块，延时0–60秒');
      out.trigger={after:t.after,event:t.event,delayMs:t.delayMs};
    }
    return out;
  });
  if(result.some(b=>b.trigger))programFromBlocks(result);
  return result;
}
export function programFromBlocks(blocks){
  return validateProgram(blocks.map(b=>({channel:b.channel,pulseUs:pulseFromAngle(b.channel,b.angle),
    trigger:b.trigger||{after:null,event:'start',delayMs:b.atMs}})));
}
export function encoderPoint(packet,at,now){
  return {counts:packet?.counts?.map(v=>Number.isInteger(v)?v:null)??[null,null,null,null],
    validMask:packet&&now-at<500?packet.validMask:0};
}
function validPoint(p){return p&&Array.isArray(p.counts)&&p.counts.length===4&&p.counts.every(v=>v===null||Number.isInteger(v)&&v>=-2147483648&&v<=2147483647)&&Number.isInteger(p.validMask)&&p.validMask>=0&&p.validMask<=15;}
export function validateNodeTrace(trace){
  const t=validateTrace(trace);
  if(!validPoint(t.startEncoder)||!validPoint(t.endEncoder)||!Array.isArray(t.keyEvents)||t.keyEvents.length>20000||!Array.isArray(t.directServos)||t.directServos.length>2000)throw Error('节点编码器或键盘记录无效');
  const end=t.samples.at(-1).t;let previous=-1;
  for(const e of t.keyEvents){
    if(!Number.isFinite(e.t)||e.t<previous||e.t<0||e.t>end||!['down','up'].includes(e.type)||typeof e.key!=='string'||e.key.length>24)throw Error('键盘事件时间或按键无效');
    if(e.action){functionKey(e.key);e.action=validateBlocks(e.action);if(e.type!=='down'||e.accepted!==true)throw Error('功能快照必须对应已接受的按下事件');}
    previous=e.t;
  }
  previous=-1;
  for(const e of t.directServos){validatePulse(e.channel,e.pulseUs);if(!Number.isFinite(e.t)||e.t<previous||e.t<0||e.t>end)throw Error('舵机录制事件无效');previous=e.t;}
  return t;
}
export class WorkshopStore{
  constructor(storage=globalThis.localStorage){
    this.storage=storage;this.error='';this.data={version:3,bindings:ARM_PRESETS.map((preset,i)=>({id:'key'+i,...clone(preset)})),nodes:[{id:'node1',name:'节点1',trace:null}]};
    try{const raw=storage?.getItem(WORKSHOP_KEY);if(raw)this.data=this.validate(JSON.parse(raw));}catch(e){this.error='新配置未载入：'+e.message;}
  }
  validate(data){
    const d=clone(data);if(![2,3].includes(d.version)||!Array.isArray(d.bindings)||d.bindings.length>40||!Array.isArray(d.nodes)||d.nodes.length>100)throw Error('按键/节点资料格式无效');
    d.version=3; // v3 retains dependent blocks; v2 files migrate without motion.
    const ids=new Set(),keys=new Set();
    for(const item of [...d.bindings,...d.nodes]){if(typeof item.id!=='string'||!item.id||ids.has(item.id)||typeof item.name!=='string'||item.name.length>64)throw Error('行编号或名称无效');ids.add(item.id);}
    for(const row of d.bindings){row.key=functionKey(row.key);if(keys.has(row.key))throw Error('功能键不能重复');keys.add(row.key);row.blocks=validateBlocks(row.blocks);}
    for(const row of d.nodes)if(row.trace)row.trace=validateNodeTrace(row.trace);
    return d;
  }
  commit(data){const next=this.validate(data),json=JSON.stringify(next);if(json.length>4000000)throw Error('记录超过4MB，请导出并清理');if(!this.storage)throw Error('浏览器存储不可用，请使用导出备份');this.storage.setItem(WORKSHOP_KEY,json);this.data=next;}
  edit(fn){const d=clone(this.data);fn(d);this.commit(d);}
  export(){return JSON.stringify(this.data,null,2);}
  import(json){this.commit(JSON.parse(json));}
  installArmPresets(){this.edit(d=>{for(const preset of ARM_PRESETS){
    const row=d.bindings.find(r=>r.key===preset.key);
    if(row)Object.assign(row,clone(preset));
    else {let id='arm-'+preset.key;while([...d.bindings,...d.nodes].some(r=>r.id===id))id+='-';d.bindings.push({id,...clone(preset)});}
  }});}
}
/* Timelines retain each accepted key's t=0 independently. No dependency on
 * previous blocks finishing. Re-pressing the same key replaces its unsent
 * blocks; a later due target supersedes the previous target on that channel. */
export class KeyTimeline{
  constructor({now=()=>performance.now(),send=()=>{},sendProgram=()=>{throw Error('未连接关联动作执行器');},ready=()=>true}){Object.assign(this,{now,send,sendProgram,ready});this.pending=[];this.lastKey=null;this.lastAt=-Infinity;this.order=0;this.message='等待功能键';}
  trigger(row,at=this.now(),{recorded=false}={}){
    const key=functionKey(row.key),blocks=validateBlocks(row.blocks);
    // Recorded events already contain the original acceptance decision. A
    // preceding manual key must not silently drop an accepted replay event.
    if(!recorded&&this.lastKey!==key&&at-this.lastAt<2000){this.message=`${key.toUpperCase()} 未执行：功能键间隔还剩 ${((2000-at+this.lastAt)/1000).toFixed(3)}秒`;return false;}
    this.pending=this.pending.filter(s=>s.key!==key);this.lastKey=key;this.lastAt=at;
    if(blocks.some(b=>b.trigger))this.pending.push({key,due:at,order:this.order++,channel:blocks[0].channel,program:programFromBlocks(blocks)});
    else this.pending.push(...blocks.map(b=>({...b,key,due:at+b.atMs,order:this.order++})));
    this.pending.sort((a,b)=>a.due-b.due||a.order-b.order);this.message=`${key.toUpperCase()} 已触发，按下为0秒`;return true;
  }
  tick(){
    const first=this.pending[0];
    if(first?.program&&first.due<=this.now()&&this.ready(first)){this.pending.shift();this.sendProgram(first.program);this.message=`${first.key.toUpperCase()} 关联动作执行中`;return;}
    const due=[];while(this.pending.length&&!this.pending[0].program&&this.pending[0].due<=this.now()&&this.ready(this.pending[0]))due.push(this.pending.shift());
    if(due.length){this.send([...new Map(due.map(b=>[b.channel,{channel:b.channel,pulseUs:pulseFromAngle(b.channel,b.angle)}])).values()]);this.message=`已下发 ${due.length} 个到时方块（同路以最后目标为准）`;}
  }
  cancel(reason='动作已停止'){this.pending=[];this.message=reason;}
  get active(){return this.pending.length>0;}
}
export class NodeRecorder{
  constructor(){this.active=false;this.held=new Set();}
  start({at,calibration,trackWidthMm,startEncoder}){
    this.started=at;this.active=true;this.held.clear();this.trace={version:2,calibration:clone(calibration),trackWidthMm,startEncoder:clone(startEncoder),endEncoder:clone(startEncoder),samples:[],servos:[],keyEvents:[],directServos:[]};
  }
  key(type,key,at,{accepted=false,action=null,synthetic=false}={}){
    if(!this.active)return;
    if(this.trace.keyEvents.length>=19900)throw Error('键盘事件接近上限，请结束录制');
    key=normalizeKey(key);
    if(type==='down'){if(this.held.has(key))return;this.held.add(key);}else if(!this.held.delete(key))return;
    this.trace.keyEvents.push({t:Math.max(0,at-this.started),type,key,accepted,...(action?{action:clone(action)}:{}),...(synthetic?{synthetic:true}:{})});
  }
  target(e,at){if(this.active)this.trace.directServos.push({...e,t:at-this.started});}
  sample({at,odometry,counts,intent,limit},force=false){
    if(!this.active)return;
    const t=Math.max(0,at-this.started),last=this.trace.samples.at(-1);
    if(t>1800000||this.trace.samples.length>=20000)throw Error('录制已达到30分钟或采样数量上限');
    if(!force&&last&&t-last.t<50)return;
    const [left,right]=odometry?.sideTravelMm??[last?.left??0,last?.right??0];
    const point={t,left,right,counts:counts?[...counts]:null,estimated:!odometry?.observable||!!odometry?.estimated,missingSideMask:odometry?.missingSideMask??3,throttle:intent.throttle,steer:intent.steer,limit};
    if(last&&last.t===t)this.trace.samples[this.trace.samples.length-1]=point;else this.trace.samples.push(point);
  }
  finish(sample,endEncoder){
    if(!this.active)throw Error('当前没有录制');
    const at=Math.max(sample.at,this.started+1);
    for(const key of [...this.held])this.key('up',key,at,{synthetic:true});
    this.sample({...sample,at,intent:{throttle:0,steer:0}},true);this.trace.endEncoder=clone(endEncoder);this.active=false;
    return validateNodeTrace(this.trace);
  }
}
export function relativeEncoderTargets(trace,current){
  return trace.endEncoder.counts.map((end,i)=>{
    const start=trace.startEncoder.counts[i],base=current.counts[i];
    return end===null||start===null||base===null?null:(base+((end-start)|0))|0;
  });
}
