/* Shared desktop/mobile controls; no servo confirmation checkboxes. */
import { DoorControl } from './door-control.js?v=bootquiet-v5';
import {servoBounds} from './protocol.js?v=bootquiet-v5';
export function installServoPanel(controller,{onTarget=()=>{}}={}) {
  const root=document.getElementById('servoPanel');
  const gpio=document.body?.dataset?.hardwareProfile==='c3-pa' ? [7,6,10] : [12,13,14];
  const names=[`DS3218 270°位置型 · 大臂 · GPIO${gpio[0]}`,`MG995 180° · 小臂 · GPIO${gpio[1]}`,`MG995 · 门约90°行程 · GPIO${gpio[2]}`];
  // 2026-10-23: user confirmed the arm is the 270-degree position variant.
  // Keep the existing pulse windows; correcting the scale must not expand travel.
  const spans=[270,180,90];
  const ranges=spans.map((span,i)=>i===2?{min:0,max:90,reference:45}:{min:span*0.4,max:span*0.6,reference:span/2});
  root.innerHTML=`<h2>舵机 · 大臂 / 小臂 / 开门</h2>
    <p class="hint">直接拖动目标滑块。ESP32以100μs/s、200μs/s²缓加减速；显示均为指令参考，并非实测角度。</p>
    <p class="hint">首次接管无位置反馈：把“起点参考”设为当前机构位置，再拖动目标。起点不符仍可能突然转动；软件限速不等于限流或堵转保护。</p>
    <div class="servo-grid">${names.map((name,i)=>`
      <fieldset><legend id="servoLegend${i}">${name}</legend><output id="servoValue${i}">等待固件状态</output>
      <label for="servoReference${i}">上电 / 释放后的起点参考 <strong id="servoReferenceValue${i}"></strong></label>
      <input id="servoReference${i}" type="range" min="${ranges[i].min}" max="${ranges[i].max}" step="${i===0?0.5:1}" value="${ranges[i].reference}">
      <label for="servoAngle${i}">目标参考角度 <strong id="servoAngleValue${i}">—</strong></label>
      <input id="servoAngle${i}" class="servo-angle" type="range" min="${ranges[i].min}" max="${ranges[i].max}" step="${i===0?0.5:1}" value="${ranges[i].reference}" disabled>
      <div class="row"><label>机械下限 μs<input id="servoMin${i}" type="number" value="${i===0?500:i===2?1000:1300}"></label>
      <label>机械上限 μs<input id="servoMax${i}" type="number" value="${i===0?2500:i===2?2000:1700}"></label>
      <button id="servoSave${i}" type="button" disabled>保存限位与参考到 C3</button></div>
      <p id="servoType${i}" class="hint"></p><div class="servo-buttons">
      <button type="button" data-channel="${i}" data-servo="enable" disabled>从参考起点启用</button>
      <button type="button" data-channel="${i}" data-servo="-25" disabled>−25μs</button>
      <button type="button" data-channel="${i}" data-servo="25" disabled>+25μs</button>
      <button type="button" data-channel="${i}" data-servo="release" disabled>释放输出</button>
      </div></fieldset>`).join('')}</div>
    <fieldset><legend>第三路 · 门端点</legend>
      <p class="hint">1000→2000μs仅作约90°行程参考。方向相反可交换端点；不要把目标设到机械死点。</p>
      <label>关门 μs <input id="doorClosed" type="number" min="1000" max="2000" step="1" value="1000"></label>
      <label>开门 μs <input id="doorOpen" type="number" min="1000" max="2000" step="1" value="2000"></label>
      <div class="servo-buttons"><button id="doorClose" type="button" disabled>关门</button><button id="doorOpenButton" type="button" disabled>开门</button><button id="doorStop" type="button" disabled>停止跟随 · 保持</button></div>
      <p id="doorMessage" role="status" class="hint"></p>
    </fieldset>
    <p id="servoMessage" role="status" class="hint">上电不自动输出，不强制回到1500μs。V8接线IO7/6/10需新版C3固件。</p>
    <p class="hint">底盘锁定停稳后可调。STOP / 断联冻结最后PWM指令，保留保持力；释放或断电可能下垂。</p>`;
  const $=id=>document.getElementById(id), message=$('servoMessage');
  const dirty=[false,false,false],readKeys=['','',''];
  const runners=[0,1,2].map(channel=>new DoorControl(controller,{channel})), door=runners[2];
  const endpoints=()=>{
    const a=Number($('doorClosed').value),b=Number($('doorOpen').value);
    if(!Number.isInteger(a)||!Number.isInteger(b)||a<1000||b<1000||a>2000||b>2000||a===b) throw new Error('门端点须为1000–2000μs且互不相同');
    return [a,b];
  };
  const angleToPulse=(i,angle)=>{if(i!==2)return Math.round(500+angle*2000/spans[i]);const[a,b]=endpoints();return Math.round(a+(b-a)*angle/90);};
  const pulseToAngle=(i,pulse)=>{if(i!==2)return(pulse-500)*spans[i]/2000;const[a,b]=endpoints();return(pulse-a)*90/(b-a);};
  const render=()=>{
    const s=controller.servoStatus, fresh=controller.state.connected&&s&&controller.now()-controller.servoStatusAt<500;
    const owner=controller.servoOwner;
    for(let i=0;i<3;i++) {
      const setting=controller.deviceSettings?.servos[i],key=setting?`${setting.session}/${setting.revision}`:'';
      if(setting&&key!==readKeys[i]&&!dirty[i]){
        if(i===2){$('doorClosed').value=setting.doorClosed;$('doorOpen').value=setting.doorOpen;}
        $(`servoMin${i}`).value=setting.minUs;$(`servoMax${i}`).value=setting.maxUs;
        $(`servoReference${i}`).value=pulseToAngle(i,setting.referenceUs);readKeys[i]=key;
      }
      const [min,max]=setting?[setting.minUs,setting.maxUs]:servoBounds(i,s);
      const a=pulseToAngle(i,min),b=pulseToAngle(i,max);
      for(const id of [`servoReference${i}`,`servoAngle${i}`]){ $(id).setAttribute('min',String(Math.min(a,b)));$(id).setAttribute('max',String(Math.max(a,b))); }
      $(`servoSave${i}`).disabled=!setting||!controller.state.connected||controller.state.configBusy||!!owner;
      const runner=runners[i],old=fresh?s.pulses[i]:0,other=owner&&owner!==runner;
      if(fresh&&document.body?.dataset?.hardwareProfile==='c3-pa'){
        const pins=s.armFullRange?[7,6,10]:[7,10,0];
        $(`servoLegend${i}`).textContent=names[i].replace(/GPIO\d+/,`GPIO${pins[i]}${s.armFullRange?'':' · 旧C3端口'}`);
      }
      $(`servoValue${i}`).textContent=fresh?old?`${old} μs · 当前PWM指令`:'PWM已释放':controller.state.connected?'状态过期 / 等待固件':'未连接蓝牙';
      $(`servoType${i}`).textContent=s?.slew?(document.body?.dataset?.hardwareProfile==='c3-pa'&&!s.armFullRange?'旧C3仍用IO7/10/0；接V8端口前需烧新版C3':'ESP32缓加减速 · 拖动只保留最新目标'):'需要支持缓启动的固件';
      $(`servoAngle${i}`).disabled=!!other||(!runner.active&&!controller.servoAvailable(i));
      $(`servoReference${i}`).disabled=!!old||runner.active;
      $(`servoReferenceValue${i}`).textContent=`${$(`servoReference${i}`).value}°`;
      let angle=NaN;
      try { angle=pulseToAngle(i,runner.active?runner.target:old||angleToPulse(i,Number($(`servoReference${i}`).value))); } catch {}
      if(Number.isFinite(angle))$(`servoAngle${i}`).value=String(i===0?Math.round(angle*2)/2:Math.round(angle));
      $(`servoAngleValue${i}`).textContent=Number.isFinite(angle)?`${angle.toFixed(1)}°${runner.active?' · 跟随中':''}`:'端点无效';
    }
    root.querySelectorAll('button[data-servo]').forEach(b=>{
      const i=Number(b.dataset.channel),old=fresh?s.pulses[i]:0,action=b.dataset.servo,release=action==='release';
      const [min,max]=servoBounds(i,s);
      b.disabled=!!owner||!controller.servoAvailable(i,release)||(release?!old:action==='enable'?!!old:!old||old+Number(action)<min||old+Number(action)>max);
    });
    $('doorClose').disabled=$('doorOpenButton').disabled=!!owner||!controller.servoAvailable(2);
    $('doorStop').disabled=!owner&&!controller.servoMotion?.movingMask;
    $('doorClosed').disabled=$('doorOpen').disabled=door.active;
    $('doorMessage').textContent=runners.find(r=>r.active)?.message||door.message;
  };
  runners.forEach(r=>{r.changed=render;});
  const move=async(channel,target,referenceUs)=>{
    const r=runners[channel];
    const reference=referenceUs??(controller.servoStatus?.pulses[channel]?0:angleToPulse(channel,Number($(`servoReference${channel}`).value)));
    r.targetTo(target,reference);
    await r.tick(); if(!r.active&&r.message.startsWith('跟随中止'))throw new Error(r.message);
    onTarget({channel,pulseUs:target,referenceUs:reference});
  };
  const cancelAll=reason=>runners.forEach(r=>r.cancel(reason));
  for(let i=0;i<3;i++) {
    $(`servoReference${i}`).addEventListener('input',()=>{dirty[i]=true;render();});
    for(const kind of ['Min','Max'])$(`servo${kind}${i}`).addEventListener('input',()=>{dirty[i]=true;});
    $(`servoSave${i}`).addEventListener('click',async()=>{
      try{
        let minUs=Number($(`servoMin${i}`).value),maxUs=Number($(`servoMax${i}`).value),reversed=false;
        if(i===2){const[a,b]=endpoints();minUs=Math.min(a,b);maxUs=Math.max(a,b);reversed=a>b;}
        await controller.configureServo(i,{minUs,maxUs,referenceUs:angleToPulse(i,Number($(`servoReference${i}`).value)),calibrated:true,reversed});
        dirty[i]=false;readKeys[i]='';message.textContent='已写入 C3 Flash，等待回读；没有执行位置';
      }catch(e){message.textContent=e.message;}render();
    });
    $(`servoAngle${i}`).addEventListener('input',()=>{
      let target;try{target=angleToPulse(i,Number($(`servoAngle${i}`).value));}catch(e){message.textContent=e.message;return;}
      move(i,target).catch(e=>{message.textContent=e.message;render();});
    });
  }
  for(const[id,input]of[['doorClose','doorClosed'],['doorOpenButton','doorOpen']])$(id).addEventListener('click',()=>move(2,Number($(input).value)).catch(e=>{message.textContent=e.message;render();}));
  const halt=reason=>{cancelAll(reason);controller.stop(reason).catch(()=>{});};
  $('doorStop').addEventListener('click',()=>halt('跟随已停止，保持最后PWM指令'));
  for(const id of['doorClosed','doorOpen'])$(id).addEventListener('input',()=>{dirty[2]=true;render();});
  window.addEventListener('blur',()=>{if(controller.servoOwner)halt('页面失焦，停止跟随');});
  document.addEventListener('visibilitychange',()=>{if(document.hidden&&controller.servoOwner)halt('页面隐藏，停止跟随');});
  root.addEventListener('click',async event=>{
    const b=event.target.closest('button[data-servo]');if(!b||b.disabled)return;
    const ch=Number(b.dataset.channel),action=b.dataset.servo;
    try {
      if(action==='release')await controller.servo(ch,0);
      else await move(ch,action==='enable'?angleToPulse(ch,Number($(`servoReference${ch}`).value)):controller.servoStatus.pulses[ch]+Number(action));
      message.textContent=controller.state.note;
    }catch(e){message.textContent=e.message;}render();
  });
  setInterval(()=>{runners.forEach(r=>{void r.tick();});render();},50);render();
  return {render,door,runners,async move(channel,pulse,referenceUs=0){
    if(pulse===0){cancelAll('释放输出');return controller.servo(channel,0);}
    // Agent must provide its own cold-start reference, never silently reuse a form value.
    return move(channel,pulse,referenceUs);
  }};
}
