/* Shared by desktop and Android pages. Deliberate clicks only, no auto-repeat. */
import { DoorControl } from './door-control.js';
export function installServoPanel(controller) {
  const root = document.getElementById('servoPanel');
  root.innerHTML = `<h2>舵机 · 大臂 / 小臂 / 开门</h2>
    <p class="hint">底盘锁定、四轮停稳后调节。显示的是指令脉宽，不是实测角度。首次输出 1500μs 可能转动，请先托住机构。</p>
    <label class="servo-confirm"><input id="servoSetupConfirmed" type="checkbox">我已确认舵机供电、共地，并托住机构留出活动空间</label>
    <div class="servo-grid">${['DS3128 360°位置型 · 大臂 · GPIO12', 'MG995 180° · 小臂 · GPIO13', 'MG995 · 门约90°行程 · GPIO14'].map((name,i) => `
      <fieldset><legend>${name}</legend><output id="servoValue${i}">等待固件状态</output>
      <label for="servoAngle${i}">目标参考角度 <strong id="servoAngleValue${i}">—</strong></label>
      <input id="servoAngle${i}" class="servo-angle" type="range" min="${i===0?144:i===1?72:0}" max="${i===0?216:i===1?108:90}" step="1" value="${i===0?180:i===1?90:45}" disabled>
      <p class="hint">${i===2?'0–90°按下方门端点映射':'标准500–2500μs换算参考；当前保留小范围调试窗口'}，不是实测角度。</p>
      <p id="servoType${i}" class="hint"></p><div class="servo-buttons">
      <button type="button" data-channel="${i}" data-servo="enable" disabled>启用 1500μs</button>
      <button type="button" data-channel="${i}" data-servo="-25" disabled>−25</button>
      <button type="button" data-channel="${i}" data-servo="-10" disabled>−10</button>
      <button type="button" data-channel="${i}" data-servo="10" disabled>+10</button>
      <button type="button" data-channel="${i}" data-servo="25" disabled>+25</button>
      <button type="button" data-channel="${i}" data-servo="release" disabled>释放输出</button>
      </div></fieldset>`).join('')}</div>
    <fieldset><legend>第三路 · 门端点</legend>
      <p class="hint">先托住门，启用1500μs后用±10/25微调。1000→2000μs仅作约90°行程参考，可能因舵机不同而变化；若方向反了，交换开关端点。无角度反馈或限位开关。</p>
      <label>关门 μs <input id="doorClosed" type="number" min="1000" max="2000" step="1" value="1000"></label>
      <label>开门 μs <input id="doorOpen" type="number" min="1000" max="2000" step="1" value="2000"></label>
      <label class="servo-confirm"><input id="doorConfirmed" type="checkbox">我已校准门端点并确认行程无阻挡</label>
      <div class="servo-buttons"><button id="doorClose" type="button" disabled>关门</button><button id="doorOpenButton" type="button" disabled>开门</button><button id="doorStop" type="button" disabled>停止舵机跟随 · 保持</button></div>
      <p id="doorMessage" role="status" class="hint"></p>
    </fieldset>
    <p id="servoMessage" role="status" class="hint">需配套三舵机固件；不会自动启用。大/小臂1300–1700μs，门1000–2000μs。</p>
    <p class="hint">位置舵机在 STOP / 断联后保持最后指令，不自动回中。释放输出或断电可能使机械臂下垂；这些操作不等于已测得机构停止。</p>`;
  const confirm = document.getElementById('servoSetupConfirmed');
  const message = document.getElementById('servoMessage');
  const runners = [0,1,2].map(channel=>new DoorControl(controller,{channel}));
  const door = runners[2];
  const angleToPulse=(i,angle)=>i===2 ? Math.round(Number(document.getElementById('doorClosed').value)+
    (Number(document.getElementById('doorOpen').value)-Number(document.getElementById('doorClosed').value))*angle/90) : Math.round(500+angle*2000/(i===0?360:180));
  const pulseToAngle=(i,pulse)=>i===2 ? (pulse-Number(document.getElementById('doorClosed').value))*90/
    (Number(document.getElementById('doorOpen').value)-Number(document.getElementById('doorClosed').value)) : (pulse-500)*(i===0?360:180)/2000;
  let lastSession = 0, cooldown = 0;
  const render = () => {
    if (lastSession !== controller.state.session) { confirm.checked = false; document.getElementById('doorConfirmed').checked=false; lastSession = controller.state.session; }
    const s = controller.servoStatus;
    const fresh = controller.state.connected && s && controller.now()-controller.servoStatusAt < 500;
    for (let i=0;i<3;i++) {
      document.getElementById(`servoValue${i}`).textContent = fresh && i<s.pulses.length ? (s.pulses[i] ? `${s.pulses[i]} μs · 指令保持` : 'PWM 已释放') : controller.state.connected?'状态过期 / 需配套固件':'未连接蓝牙';
      document.getElementById(`servoType${i}`).textContent = fresh && (s.positionMask & (1<<i)) ? '位置微调 · 单次最大 25μs' : '等待固件确认位置模式';
      const slider=document.getElementById(`servoAngle${i}`), runner=runners[i];
      const old=fresh?s.pulses[i]:0, moving=controller.servoOwner;
      slider.disabled=!confirm.checked || !old || (!runner.active && !controller.servoAvailable(i)) ||
        (moving && moving!==runner) || (i===2 && !document.getElementById('doorConfirmed').checked);
      const pulse=runner.active?runner.target:old;
      const angle=pulse?pulseToAngle(i,pulse):NaN;
      if(Number.isFinite(angle)) slider.value=String(Math.round(angle));
      document.getElementById(`servoAngleValue${i}`).textContent=Number.isFinite(angle)?`${angle.toFixed(1)}°${runner.active?' · 跟随中':''}`:'未启用';
    }
    root.querySelectorAll('button[data-servo]').forEach(b => {
      const i=Number(b.dataset.channel), action=b.dataset.servo, old=fresh ? s.pulses[i] : 0;
      const release=action==='release', enable=action==='enable';
      const next=old+Number(action);
      b.disabled = runners.some(r=>r.active || r.inFlight) || !controller.servoAvailable(i, release) || controller.now()<cooldown ||
        (release ? !old : !confirm.checked || (enable ? !!old : !old || next<(i===2?1000:1300) || next>(i===2?2000:1700)));
    });
    const allowed=!runners.some(r=>r.active || r.inFlight) && confirm.checked && document.getElementById('doorConfirmed').checked && controller.servoAvailable(2) && !!s?.pulses[2];
    document.getElementById('doorClose').disabled=!allowed;
    document.getElementById('doorOpenButton').disabled=!allowed;
    document.getElementById('doorStop').disabled=!runners.some(r=>r.active);
    document.getElementById('doorClosed').disabled=door.active;
    document.getElementById('doorOpen').disabled=door.active;
    document.getElementById('doorMessage').textContent=runners.find(r=>r.active)?.message || door.message;
  };
  runners.forEach(r=>{r.changed=render;});
  const cancelAll=reason=>runners.forEach(r=>r.cancel(reason));
  for(let i=0;i<3;i++) document.getElementById(`servoAngle${i}`).addEventListener('input',()=>{
    try { runners[i].targetTo(angleToPulse(i,Number(document.getElementById(`servoAngle${i}`).value)),confirm.checked && (i!==2 || document.getElementById('doorConfirmed').checked)); }
    catch(e) { message.textContent=e.message; render(); }
  });
  for(const [id,input] of [['doorClose','doorClosed'],['doorOpenButton','doorOpen']]) {
    document.getElementById(id).addEventListener('click',()=>{
      try { door.start(Number(document.getElementById(input).value),confirm.checked && document.getElementById('doorConfirmed').checked); }
      catch(e) { door.message=e.message; render(); }
    });
  }
  document.getElementById('doorStop').addEventListener('click',()=>{ cancelAll('滑块跟随已停止，保持最后指令'); controller.stop('停止舵机跟随；保持最后指令').catch(()=>{}); });
  for(const id of ['doorClosed','doorOpen']) document.getElementById(id).addEventListener('input',()=>{ document.getElementById('doorConfirmed').checked=false; render(); });
  document.getElementById('doorConfirmed').addEventListener('change',()=>{ if(!document.getElementById('doorConfirmed').checked) door.cancel(); render(); });
  window.addEventListener('blur',()=>cancelAll('页面失焦，滑块跟随已取消；保持最后指令'));
  document.addEventListener('visibilitychange',()=>{ if(document.hidden) cancelAll('页面隐藏，滑块跟随已取消；保持最后指令'); });
  root.addEventListener('click', async event => {
    const b=event.target.closest('button[data-servo]');
    if(!b || b.disabled) return;
    const channel=Number(b.dataset.channel), action=b.dataset.servo;
    const pulseUs=action==='release' ? 0 : action==='enable' ? 1500 : controller.servoStatus.pulses[channel]+Number(action);
    cooldown=controller.now()+200;
    try { await controller.servo(channel,pulseUs); message.textContent=controller.state.note; }
    catch(error) { message.textContent=error.message; }
    render();
  });
  confirm.addEventListener('change',()=>{ if(!confirm.checked) cancelAll('已撤销确认，保持最后指令'); render(); });
  setInterval(()=>{ runners.forEach(r=>{void r.tick();}); render(); },50); render();
  return { render, door, runners };
}
