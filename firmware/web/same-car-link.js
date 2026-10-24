/* Connection identity only. Mechanical/PID/servo settings still live in C3 Flash. */
export class SameCarLink {
  constructor({bluetooth,storage,expectedName=null,service,onState=()=>{},setTimer=(fn,ms)=>setTimeout(fn,ms),clearTimer=id=>clearTimeout(id)}) {
    Object.assign(this,{bluetooth,storage,expectedName,service,onState,setTimer,clearTimer});
    this.key='crtc-bound-ble-device-v1';this.device=null;this.record=null;this.timer=null;this.enabled=true;this.failures=0;
    try{const r=JSON.parse(storage?.getItem(this.key)||'null');if(r&&typeof r.id==='string'&&r.id&&typeof r.name==='string'&&(!expectedName||r.name===expectedName)){this.record=r;this.enabled=!r.paused;}}catch{}
  }
  check(device){
    if(!device?.id||this.expectedName&&device.name!==this.expectedName)throw Error('不是本便携包绑定的车，未连接');
    if(!this.expectedName&&!/^CRTC-Lower-Car(?:-[0-9A-F]{12})?$/.test(device.name||''))throw Error('设备名称不匹配');
    return device;
  }
  remember(device){
    this.check(device);this.device=device;this.record={id:device.id,name:device.name};
    try{this.storage?.setItem(this.key,JSON.stringify(this.record));}catch{}
    this.enabled=true;this.onState(`已绑定 ${device.name} · 断线只重连此设备`);
  }
  async restore(){
    if(this.device)return this.device;
    if(!this.record||!this.bluetooth?.getDevices)return null;
    const devices=await this.bluetooth.getDevices();
    // Never .find(name): two cars can have the same advertised name.
    const device=devices.find(d=>d.id===this.record.id);
    if(!device)return null;
    this.device=this.check(device);return device;
  }
  choose(){
    this.enabled=true;
    // Must execute directly from a human click. Automatic retries never call this.
    return this.bluetooth.requestDevice({filters:[this.expectedName?{name:this.expectedName}:{namePrefix:'CRTC-Lower-Car'}],optionalServices:[this.service]});
  }
  succeeded(){this.failures=0;if(this.timer!==null)this.clearTimer(this.timer);this.timer=null;}
  pause(){
    this.enabled=false;if(this.timer!==null)this.clearTimer(this.timer);this.timer=null;
    // Persist deliberate handoff, so restoring the old PC's browser cannot
    // unexpectedly take the car back from the second computer.
    if(this.record){this.record={...this.record,paused:true};try{this.storage?.setItem(this.key,JSON.stringify(this.record));}catch{}}
    this.onState('自动重连已暂停；点击连接恢复');
  }
  schedule(callback){
    if(!this.enabled||!this.device&&!this.record||this.timer!==null)return;
    const delay=Math.min(8000,1000*2**Math.min(this.failures++,3));
    this.onState(`${this.record?.name||this.expectedName} · ${delay/1000}秒后重连，动作不恢复`);
    this.timer=this.setTimer(()=>{this.timer=null;if(this.enabled)callback();},delay);
  }
}
