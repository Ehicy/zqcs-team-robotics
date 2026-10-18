/* Shared desktop/mobile controls; no servo confirmation checkboxes. */
import { DoorControl } from './door-control.js';
export function installServoPanel(controller) {
  const root=document.getElementById('servoPanel');
  root.innerHTML=`<h2>舵机 · 大臂 / 小臂 / 开门</h2>
    <p class="hint">直接拖动目标滑块。ESP32以100μs/s、200μs/s²缓加减速；显示均为指令参考，并非实测角度。</p>
    <p class="hint">首次接管无位置反馈：把“起点参考”设为当前机构位置，再拖动目标。起点不符仍可能突然转动；软件限速不等于限流或堵转保护。</p>
    <div class="servo-grid">${['DS3128 360°位置型 · 大臂 · GPIO12','MG995 180° · 小臂 · GPIO13','MG995 · 门约90°行程 · GPIO14'].map((name,i)=>`
      <fieldset><legend>${name}</legend><output id="servoValue${i}">等待固件状态</output>
      <label for="servoReference${i}">上电 / 释放后的起点参考 <strong id="servoReferenceValue${i}"></strong></label>
      <input id="servoReference${i}" type="range" min="${i===0?144:i===1?72:0}" max="${i===0?216:i===1?108:90}" step="1" value="${i===0?180:i===1?90:45}">
      <label for="servoAngle${i}">目标参考角度 <strong id="servoAngleValue${i}">—</strong></label>
      <input id="servoAngle${i}" class="servo-angle" type="range" min="${i===0?144:i===1?72:0}" max="${i===0?216:i===1?108:90}" step="1" value="${i===0?180:i===1?90:45}" disabled>
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
    <p id="servoMessage" role="status" class="hint">需配套v4缓启动固件；上电不自动输出，不强制回到1500μs。</p>
    <p class="hint">底盘锁定停稳后可调。STOP / 断联冻结最后PWM指令，保留保持力；释放或断电可能下垂。</p>`;
  const $=id=>document.getElementById(id), message=$('servoMessage');
  const runners=[0,1,2].map(channel=>new DoorControl(controller,{channel})), door=runners[2];
  const endpoints=()=>{
    const a=Number($('doorClosed').value),b=Number($('doorOpen').value);
    if(!Number.isInteger(a)||!Number.isInteger(b)||a<1000||b<1000||a>2000||b>2000||a===b) throw new Error('门端点须为1000–2000μs且互不相同');
    return [a,b];
  };
  const angleToPulse=(i,angle)=>{if(i!==2)return Math.round(500+angle*2000/(i===0?360:180));const[a,b]=endpoints();return Math.round(a+(b-a)*angle/90);};
  const pulseToAngle=(i,pulse)=>{if(i!==2)return(pulse-500)*(i===0?360:180)/2000;const[a,b]=endpoints();return(pulse-a)*90/(b-a);};
  const render=()=>{
    const s=controller.servoStatus, fresh=controller.state.connected&&s&&controller.now()-controller.servoStatusAt<500;
    const owner=controller.servoOwner;
    for(let i=0;i<3;i++) {
      const runner=runners[i],old=fresh?s.pulses[i]:0,other=owner&&owner!==runner;
      $(`servoValue${i}`).textContent=fresh?old?`${old} μs · 当前PWM指令`:'PWM已释放':controller.state.connected?'状态过期 / 等待固件':'未连接蓝牙';
      $(`servoType${i}`).textContent=s?.slew?'ESP32缓加减速 · 拖动只保留最新目标':'需要v4缓启动固件';
      $(`servoAngle${i}`).disabled=!!other||(!runner.active&&!controller.servoAvailable(i));
      $(`servoReference${i}`).disabled=!!old||runner.active;
      $(`servoReferenceValue${i}`).textContent=`${$(`servoReference${i}`).value}°`;
      let angle=NaN;
      try { angle=pulseToAngle(i,runner.active?runner.target:old||angleToPulse(i,Number($(`servoReference${i}`).value))); } catch {}
      if(Number.isFinite(angle))$(`servoAngle${i}`).value=String(Math.round(angle));
      $(`servoAngleValue${i}`).textContent=Number.isFinite(angle)?`${angle.toFixed(1)}°${runner.active?' · 跟随中':''}`:'端点无效';
    }
    root.querySelectorAll('button[data-servo]').forEach(b=>{
      const i=Number(b.dataset.channel),old=fresh?s.pulses[i]:0,action=b.dataset.servo,release=action==='release';
      b.disabled=!!owner||!controller.servoAvailable(i,release)||(release?!old:action==='enable'?!!old:!old||old+Number(action)<(i===2?1000:1300)||old+Number(action)>(i===2?2000:1700));
    });
    $('doorClose').disabled=$('doorOpenButton').disabled=!!owner||!controller.servoAvailable(2);
    $('doorStop').disabled=!owner&&!controller.servoMotion?.movingMask;
    $('doorClosed').disabled=$('doorOpen').disabled=door.active;
    $('doorMessage').textContent=runners.find(r=>r.active)?.message||door.message;
  };
  runners.forEach(r=>{r.changed=render;});
  const move=async(channel,target,referenceUs)=>{
    const r=runners[channel];
    r.targetTo(target,referenceUs??(controller.servoStatus?.pulses[channel]?0:angleToPulse(channel,Number($(`servoReference${channel}`).value))));
    await r.tick(); if(!r.active&&r.message.startsWith('跟随中止'))throw new Error(r.message);
  };
  const cancelAll=reason=>runners.forEach(r=>r.cancel(reason));
  for(let i=0;i<3;i++) {
    $(`servoReference${i}`).addEventListener('input',render);
    $(`servoAngle${i}`).addEventListener('input',()=>{
      let target;try{target=angleToPulse(i,Number($(`servoAngle${i}`).value));}catch(e){message.textContent=e.message;return;}
      move(i,target).catch(e=>{message.textContent=e.message;render();});
    });
  }
  for(const[id,input]of[['doorClose','doorClosed'],['doorOpenButton','doorOpen']])$(id).addEventListener('click',()=>move(2,Number($(input).value)).catch(e=>{message.textContent=e.message;render();}));
  const halt=reason=>{cancelAll(reason);controller.stop(reason).catch(()=>{});};
  $('doorStop').addEventListener('click',()=>halt('跟随已停止，保持最后PWM指令'));
  for(const id of['doorClosed','doorOpen'])$(id).addEventListener('input',render);
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
