import {LivePidWriter,validateGains} from './live-pid.js?v=relay-enable-v10r2';
export const PHASE_NAMES = Object.freeze(['等待链路', '等待 PID 初始化', '等待四轮校准', '锁定', '已解锁', '运行', '故障']);
export function installPidPanel({root,controller,error=()=>{},log=()=>{},written=()=>{},direct=false}) {
  const controls=[['pidKp','比例 Kp','kpMilli',1000],['pidKi','积分 Ki','kiMilli',2000],['pidKd','微分 Kd','kdMilli',0]];
  root.innerHTML=`<h2>1 · PID 滑块调参 · 自动写入</h2>
    <p class="hint">四轮使用统一系数，在 STM32 各自独立计算。拖动即更新显示，自动合并并写入最新一组，无需点保存。</p>
    <div class="config-grid pid-slider-grid">${controls.map(([id,label,,value])=>`<label>${label}<output id="${id}Value" for="${id}">${(value/1000).toFixed(3)}</output>
      <input id="${id}" type="range" min="0" max="20000" step="1" value="${value}" aria-label="${label}">
      <span class="pid-range-label"><span>0.000</span><span>20.000</span></span>
      <input id="${id}Exact" type="number" min="0" max="20" step="0.001" value="${value/1000}" aria-label="${label} 精确值">
      <span class="field-unit">实际系数 · 1.000 = 协议值1000${id==='pidKd'?' · 测速微分，30ms滤波':''}</span></label>`).join('')}</div>
    <p id="pidWriteStatus" class="hint" role="status"></p>
    <div class="row"><button id="writePid" type="button" hidden>重试当前 PID</button></div>
    <p id="pidPhase" class="hint" role="status"></p>
    <p class="hint">${direct?'参数直接保存至 C3 Flash，由 C3 同步 STM32；浏览器不保存设备参数。':'历史固件仅保存 RAM。'}当前固件不支持行驶中写系数；运行中可拖动，停稳后自动写入最后一组。每次最多约2次写入；载入/回读不发送指令。</p>`;
  const field=id=>root.querySelector(`#${id}`);
  function read(){
    // An unfinished exact-value edit must not let Start use a stale slider
    // value that differs from what the operator is currently typing.
    for(const [id]of controls){const text=field(id+'Exact').value.trim(),value=Number(text);
      if(!text||!Number.isFinite(value)||value<0||value>20||Math.abs(value*1000-Math.round(value*1000))>0.00001)
        throw new Error('PID 精确值范围0–20，最多三位小数');
    }
    return validateGains(Object.fromEntries(controls.map(([id,,name])=>[name,Number(field(id).value)])));
  }
  function paint(){for(const [id]of controls)field(id+'Value').textContent=(Number(field(id).value)/1000).toFixed(3);}
  const writer=new LivePidWriter({controller,direct,changed:render,error,written:(gains,state)=>{
    written(gains,state);log(direct?'PID 滑块最新值已保存至 C3，等待回读/底盘同步':'PID 滑块最新值已由 STM32 确认');
  }});
  function set(gains){
    if(!writer.accept(gains))return;
    for(const [id,,name]of controls){field(id).value=String(gains[name]??0);field(id+'Exact').value=String((gains[name]??0)/1000);}
    paint();
  }
  function render(){
    const s=controller.state;
    const names=direct?['等待链路','等待 PID 初始化','等待参数应用','待输入','控制中','运行','故障']:PHASE_NAMES;
    field('pidPhase').textContent=`${names[s.phase]??'状态未知'} · PID 初始化${s.pidReady?'已确认':'未确认'}`;
    field('pidWriteStatus').textContent=writer.message;
    field('writePid').hidden=!writer.failed;field('writePid').disabled=!writer.failed||writer.busy||!writer.ready();
    for(const [id]of controls){field(id).disabled=!s.connected;field(id+'Exact').disabled=!s.connected;}
    paint();
  }
  for(const [id]of controls){
    field(id).addEventListener('input',()=>{
      field(id+'Exact').value=String(Number(field(id).value)/1000);paint();
      try{writer.edit(read());}catch(e){field('pidWriteStatus').textContent=e.message;}
    });
    field(id+'Exact').addEventListener('input',()=>{
      const value=field(id+'Exact').value.trim(),number=Number(value),raw=Math.round(number*1000);
      if(!value||!Number.isFinite(number)||number<0||number>20||Math.abs(number*1000-raw)>0.00001){writer.rejectDraft('精确值范围0–20，最多三位小数');return;}
      field(id).value=String(raw);paint();
      try{writer.edit(read());}catch(e){field('pidWriteStatus').textContent=e.message;}
    });
    for(const control of [field(id),field(id+'Exact')])control.addEventListener('change',()=>{void writer.release();});
  }
  field('writePid').onclick=()=>writer.retry();
  const timer=setInterval(()=>{void writer.tick();},80);
  render();return {read,set,render,writer,get pending(){return !!writer.pending||writer.busy;},dispose:()=>clearInterval(timer)};
}
