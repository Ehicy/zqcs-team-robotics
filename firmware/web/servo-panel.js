/* Shared by desktop and Android pages. Deliberate clicks only, no auto-repeat. */
export function installServoPanel(controller) {
  const root = document.getElementById('servoPanel');
  root.innerHTML = `<h2>机械臂 · 双舵机</h2>
    <p class="hint">底盘锁定、四轮停稳后调节。显示的是指令脉宽，不是实测角度。首次输出 1500μs 可能转动，请先托住机构。</p>
    <label class="servo-confirm"><input id="servoSetupConfirmed" type="checkbox">我已确认舵机供电、共地，并托住机构留出活动空间</label>
    <div class="servo-grid">${['DS3128 360°位置型 · 大臂 · GPIO12', 'MG995 180° · 小臂 · GPIO13'].map((name,i) => `
      <fieldset><legend>${name}</legend><output id="servoValue${i}">等待固件状态</output>
      <p id="servoType${i}" class="hint"></p><div class="servo-buttons">
      <button type="button" data-channel="${i}" data-servo="enable" disabled>启用 1500μs</button>
      <button type="button" data-channel="${i}" data-servo="-25" disabled>−25</button>
      <button type="button" data-channel="${i}" data-servo="-10" disabled>−10</button>
      <button type="button" data-channel="${i}" data-servo="10" disabled>+10</button>
      <button type="button" data-channel="${i}" data-servo="25" disabled>+25</button>
      <button type="button" data-channel="${i}" data-servo="release" disabled>释放输出</button>
      </div></fieldset>`).join('')}</div>
    <p id="servoMessage" role="status" class="hint">需配套舵机固件；不会自动启用。调试范围 1300–1700μs。</p>
    <p class="hint">位置舵机在 STOP / 断联后保持最后指令，不自动回中。释放输出或断电可能使机械臂下垂；这些操作不等于已测得机构停止。</p>`;
  const confirm = document.getElementById('servoSetupConfirmed');
  const message = document.getElementById('servoMessage');
  let lastSession = 0, cooldown = 0;
  const render = () => {
    if (lastSession !== controller.state.session) { confirm.checked = false; lastSession = controller.state.session; }
    const s = controller.servoStatus;
    const fresh = controller.state.connected && s && controller.now()-controller.servoStatusAt < 500;
    for (let i=0;i<2;i++) {
      document.getElementById(`servoValue${i}`).textContent = fresh ? (s.pulses[i] ? `${s.pulses[i]} μs · 指令保持` : 'PWM 已释放') : '等待新版固件 / 状态已过期';
      document.getElementById(`servoType${i}`).textContent = fresh && (s.positionMask & (1<<i)) ? '位置微调 · 单次最大 25μs' : '等待固件确认位置模式';
    }
    root.querySelectorAll('button').forEach(b => {
      const i=Number(b.dataset.channel), action=b.dataset.servo, old=fresh ? s.pulses[i] : 0;
      const release=action==='release', enable=action==='enable';
      const next=old+Number(action);
      b.disabled = !controller.servoAvailable(i, release) || controller.now()<cooldown ||
        (release ? !old : !confirm.checked || (enable ? !!old : !old || next<1300 || next>1700));
    });
  };
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
  confirm.addEventListener('change',render);
  setInterval(render,100); render();
  return { render };
}
