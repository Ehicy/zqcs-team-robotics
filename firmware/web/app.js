import { UUID, WHEELS, DEFAULT_CALIBRATION, RemoteController, decodeStatus, validateCalibration } from './protocol.js';

const $ = id => document.getElementById(id);
const directionButtons = [...document.querySelectorAll('.direction')];
const storageKey = 'crtc-lower-car-calibration-v2-rf-lf-rr-lr';
let connection = null, connecting = false, attempt = 0, heldPointer = null, heldButton = null, heldKey = null;
let encoder = null, wheelSpeeds = null, encoderAt = -Infinity, speedsAt = -Infinity;
let supported = false;

function releaseVisuals() {
  const button = heldButton, pointer = heldPointer;
  heldPointer = null; heldButton = null; heldKey = null;
  directionButtons.forEach(item => item.classList.remove('held'));
  if (button && pointer !== null && button.hasPointerCapture(pointer)) button.releasePointerCapture(pointer);
}
function showError(error) {
  const message = error?.message || String(error);
  controller.state.note = message;
  $('statusMessage').textContent = message;
  $('statusMessage').classList.add('error');
}
function safely(promise) { promise.catch(showError); }
const controller = new RemoteController({
  changed: render,
  locked: releaseVisuals,
  transportError: error => { closeTransport(); showError(new Error(`蓝牙写入失败，已锁定：${error.message}`)); },
});

function countBits(value) { return [0, 1, 2, 3].filter(i => value & (1 << i)).length; }
function render(state = controller.state) {
  $('connectionText').textContent = state.connected ? 'CRTC-Lower-Car' : (connecting ? '正在连接…' : '尚未连接');
  $('connectionDot').classList.toggle('online', state.connected);
  $('connectButton').textContent = state.connected ? '断开连接' : (connecting ? '正在连接…' : '连接蓝牙');
  $('connectButton').disabled = !supported || connecting;
  $('modeBadge').textContent = state.fault && state.connected ? '底盘故障' : state.armed ? '已解锁' : state.armPending ? '等待解锁' : '已锁定';
  $('modeBadge').className = `badge${state.fault && state.connected ? ' fault' : state.armed ? ' armed' : ''}`;
  $('uartStatus').textContent = state.connected ? (state.uart ? '在线' : '未连接') : '—';
  $('readyStatus').textContent = state.connected ? (state.ready ? '就绪' : '待校准 / 未就绪') : '—';
  $('calibrationStatus').textContent = `${countBits(state.calibratedMask)} / 4`;
  $('statusMessage').textContent = state.note;
  $('statusMessage').classList.remove('error');
  $('faultMessage').hidden = !(state.connected && (state.fault || state.faultCode));
  $('faultMessage').textContent = state.faultCode === 6
    ? '看门狗故障：电机已禁止输出，通信和灯控仍可用。停稳后可尝试清除；初始化失败需修复并重启。'
    : `故障码 ${state.faultCode} · 原因 ${state.reason}。先排除原因并停稳，清除故障不会自动解锁。`;
  $('armButton').disabled = !controller.canArm() || state.armPending || state.armed;
  $('armButton').textContent = state.armed ? '控制已解锁' : state.armPending ? '等待主控确认…' : '解锁控制 ARM';
  $('armHint').textContent = state.armed ? '按住方向行驶；松开归零。停止键会重新锁定。' : '四轮配置确认、底盘就绪且停稳后才能解锁。';
  directionButtons.forEach(button => { button.disabled = !state.armed; });
  const sessionReady = state.connected && state.session !== 0;
  $('ledOnButton').disabled = !sessionReady || state.configBusy;
  $('ledOffButton').disabled = !sessionReady || state.configBusy;
  const configAllowed = sessionReady && state.uart && !state.armed && !state.armPending && !state.remoteArmed && !state.active && !state.configBusy;
  $('saveConfigButton').disabled = !configAllowed;
  $('saveConfigButton').textContent = state.configBusy ? '等待主控逐包确认…' : '停车后写入四轮配置';
  $('clearFaultButton').disabled = !configAllowed;
  $('calibrationForm').querySelectorAll('input,select').forEach(input => { input.disabled = state.configBusy; });
  $('rememberButton').disabled = state.configBusy;
  $('resetFormButton').disabled = state.configBusy;
  $('sessionText').textContent = state.session ? `会话 ${state.session.toString(16).padStart(8, '0')}` : '会话 —';
  renderTelemetry();
}
function renderTelemetry() {
  const now = performance.now();
  const mainFresh = controller.state.connected && now - controller.mainStatusAt < 500;
  $('leftSpeed').textContent = mainFresh ? controller.state.left : '—';
  $('rightSpeed').textContent = mainFresh ? controller.state.right : '—';
  const encoderFresh = controller.state.connected && now - encoderAt < 1000;
  const speedFresh = controller.state.connected && now - speedsAt < 1000;
  WHEELS.forEach((_, i) => {
    $(`count${i}`).textContent = encoderFresh && encoder.validMask & (1 << i) ? String(encoder.counts[i]) : '—';
    $(`wheelSpeed${i}`).textContent = speedFresh && wheelSpeeds.validMask & (1 << i) ? `${wheelSpeeds.measured[i]} / ${wheelSpeeds.targets[i]}` : '— / —';
  });
  $('telemetryFreshness').textContent = encoderFresh || speedFresh ? '样本在线' : '等待采样 / 已过期';
}
function timeout(promise, milliseconds) {
  let timer;
  return Promise.race([promise, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('写入响应超时')), milliseconds); })])
    .finally(() => clearTimeout(timer));
}
function closeTransport() {
  attempt++;
  const old = connection; connection = null;
  if (old) {
    old.status.removeEventListener('characteristicvaluechanged', old.listener);
    old.device.removeEventListener('gattserverdisconnected', old.disconnected);
    if (old.device.gatt.connected) old.device.gatt.disconnect();
  }
  controller.disconnect(); connecting = false;
  encoder = wheelSpeeds = null; encoderAt = speedsAt = -Infinity; render();
}
async function connect() {
  if (controller.state.connected) {
    await Promise.race([controller.stop('正在断开，保持锁定').catch(() => {}), new Promise(resolve => setTimeout(resolve, 200))]);
    closeTransport(); return;
  }
  if (!supported || connecting) return;
  const currentAttempt = ++attempt;
  connecting = true; render();
  let device;
  try {
    device = await navigator.bluetooth.requestDevice({ filters: [{ name: 'CRTC-Lower-Car' }], optionalServices: [UUID.service] });
    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(UUID.service);
    const control = await service.getCharacteristic(UUID.control);
    const status = await service.getCharacteristic(UUID.status);
    if (currentAttempt !== attempt) throw new Error('连接已取消');
    if (!control.properties.write || typeof control.writeValueWithResponse !== 'function') throw new Error('设备不支持带响应写入，保持锁定');
    if (!status.properties.read || (!status.properties.notify && !status.properties.indicate)) throw new Error('设备缺少状态读取 / 通知能力');
    const generation = controller.attach(bytes => timeout(control.writeValueWithResponse(bytes), 600));
    const receive = value => {
      if (connection?.generation !== generation) return;
      try {
        const packet = decodeStatus(value);
        if (!controller.ingest(packet, generation)) return;
        if (packet.kind === 1) { encoder = packet; encoderAt = performance.now(); }
        if (packet.kind === 2) { wheelSpeeds = packet; speedsAt = performance.now(); }
        renderTelemetry();
      } catch (error) { safely(controller.stop('状态包异常，已停止并锁定')); showError(error); }
    };
    const listener = event => receive(event.target.value);
    const disconnected = () => { if (connection?.generation === generation) closeTransport(); };
    connection = { device, status, listener, disconnected, generation };
    status.addEventListener('characteristicvaluechanged', listener);
    device.addEventListener('gattserverdisconnected', disconnected);
    await status.startNotifications();
    receive(await status.readValue());
  } catch (error) {
    if (device?.gatt?.connected) device.gatt.disconnect();
    if (currentAttempt === attempt) closeTransport();
    showError(error.name === 'NotFoundError' ? new Error('未选择设备；保持锁定') : error);
  } finally { connecting = false; render(); }
}

function readCalibration() {
  return WHEELS.map((_, wheel) => {
    const field = name => $(`wheel${wheel}_${name}`);
    const diameter = Number(field('diameter').value);
    if (!Number.isFinite(diameter) || Math.abs(diameter * 10 - Math.round(diameter * 10)) > 0.00001) throw new Error(`${WHEELS[wheel]} 轮径最多保留一位小数`);
    return validateCalibration({ wheel,
      motorNegative: field('motor').value === '-1', encoderNegative: field('encoder').value === '-1',
      confirmed: field('confirmed').checked, diameterTenthMm: Math.round(diameter * 10),
      countsPerRev: Number(field('cpr').value), kpMilli: Number(field('kp').value),
      kiMilli: Number(field('ki').value), maxMmS: Number(field('max').value),
    });
  });
}
function setCalibration(values) {
  WHEELS.forEach((_, wheel) => {
    const value = values[wheel], field = name => $(`wheel${wheel}_${name}`);
    field('diameter').value = value.diameterTenthMm / 10; field('cpr').value = value.countsPerRev;
    field('motor').value = value.motorNegative ? '-1' : '1'; field('encoder').value = value.encoderNegative ? '-1' : '1';
    field('confirmed').checked = value.confirmed; field('kp').value = value.kpMilli; field('ki').value = value.kiMilli; field('max').value = value.maxMmS;
  });
}
function buildCalibration() {
  $('calibrationWheels').innerHTML = WHEELS.map((name, i) => `<fieldset class="wheel-config"><legend>${name} · ${i === 3 ? 'EXTI 2×' : '定时器 4×'}</legend><div class="config-grid">
    <label for="wheel${i}_diameter">轮径 mm<input id="wheel${i}_diameter" type="number" min="0.1" max="500" step="0.1" required inputmode="decimal"></label>
    <label for="wheel${i}_cpr">每轮每圈计数<input id="wheel${i}_cpr" type="number" min="1" max="1000000" step="1" required inputmode="numeric"></label>
    <label for="wheel${i}_motor">电机方向<select id="wheel${i}_motor"><option value="1">+ 正向</option><option value="-1">− 反向</option></select></label>
    <label for="wheel${i}_encoder">编码器方向<select id="wheel${i}_encoder"><option value="1">+ 正向</option><option value="-1">− 反向</option></select></label>
    <label for="wheel${i}_kp">Kp_milli<input id="wheel${i}_kp" type="number" min="0" max="20000" step="1" required inputmode="numeric"></label>
    <label for="wheel${i}_ki">Ki_milli<input id="wheel${i}_ki" type="number" min="0" max="20000" step="1" required inputmode="numeric"></label>
    <label for="wheel${i}_max">最大轮速 mm/s<input id="wheel${i}_max" type="number" min="1" max="300" step="1" required inputmode="numeric"></label>
    </div><label class="confirmation"><input id="wheel${i}_confirmed" type="checkbox"><span>我已实测核对本轮的方向、每圈计数和轮径</span></label></fieldset>`).join('');
  let values = DEFAULT_CALIBRATION.map(value => ({ ...value }));
  try {
    const saved = JSON.parse(localStorage.getItem(storageKey));
    if (Array.isArray(saved) && saved.length === 4) {
      const checked = saved.map(value => validateCalibration({ ...value, confirmed: false }));
      if (checked.every((value, i) => value.wheel === i)) values = checked;
    }
  } catch { /* Invalid/private-mode storage leaves explicit, unconfirmed defaults. */ }
  setCalibration(values);
}

$('telemetryRows').innerHTML = WHEELS.map((name, i) => `<tr><td>${name}</td><td id="count${i}">—</td><td id="wheelSpeed${i}">— / —</td></tr>`).join('');
buildCalibration();

$('connectButton').addEventListener('click', () => safely(connect()));
$('armButton').addEventListener('click', () => safely(controller.arm()));
$('speedLimit').addEventListener('input', event => {
  const value = Number(event.target.value); controller.setLimit(value); $('speedValue').textContent = value;
});
function stopNow(note = '已停止并锁定') { safely(controller.stop(note)); }
$('stopButton').addEventListener('pointerdown', event => { event.preventDefault(); stopNow(); });
$('stopButton').addEventListener('click', event => { if (event.detail === 0) stopNow(); });
$('ledOnButton').addEventListener('click', () => safely(controller.led(true)));
$('ledOffButton').addEventListener('click', () => safely(controller.led(false)));
$('clearFaultButton').addEventListener('click', () => safely(controller.clearFault()));
$('calibrationForm').addEventListener('submit', event => {
  event.preventDefault();
  try {
    if (wheelSpeeds && performance.now() - speedsAt < 1000 && wheelSpeeds.measured.some((speed, i) => (wheelSpeeds.validMask & (1 << i)) && speed !== 0)) {
      throw new Error('仍测到轮速，请等待四轮停稳后再写入');
    }
    safely(controller.configure(readCalibration()));
  } catch (error) { showError(error); }
});
$('rememberButton').addEventListener('click', () => {
  try { localStorage.setItem(storageKey, JSON.stringify(readCalibration())); $('statusMessage').textContent = '已记住本机表单；未下发配置，也不会自动解锁。'; }
  catch (error) { showError(error); }
});
$('resetFormButton').addEventListener('click', () => { setCalibration(DEFAULT_CALIBRATION); $('statusMessage').textContent = '已恢复待验证初值；未发送到车辆。'; });

directionButtons.forEach(button => {
  const intent = () => controller.setIntent(Number(button.dataset.throttle), Number(button.dataset.steer));
  button.addEventListener('contextmenu', event => event.preventDefault());
  button.addEventListener('pointerdown', event => {
    event.preventDefault();
    if (!controller.state.armed || heldPointer !== null || heldKey !== null || event.button !== 0) return;
    heldPointer = event.pointerId; heldButton = button; button.setPointerCapture(event.pointerId);
    button.classList.add('held'); intent();
  });
  button.addEventListener('pointerup', event => {
    if (heldPointer !== event.pointerId || heldButton !== button) return;
    event.preventDefault(); releaseVisuals(); controller.setIntent(0, 0);
  });
  for (const name of ['pointercancel', 'lostpointercapture']) button.addEventListener(name, event => {
    if (heldPointer === event.pointerId && heldButton === button) stopNow('触控中断，已停止并锁定');
  });
  button.addEventListener('keydown', event => {
    if (![' ', 'Enter'].includes(event.key)) return;
    event.preventDefault();
    if (event.repeat || heldPointer !== null || heldKey !== null || !controller.state.armed) return;
    heldKey = event.key; heldButton = button; button.classList.add('held'); intent();
  });
  button.addEventListener('keyup', event => {
    if (event.key === heldKey && heldButton === button) { event.preventDefault(); releaseVisuals(); controller.setIntent(0, 0); }
  });
});
window.addEventListener('blur', () => stopNow('页面失去焦点，已停止并锁定'));
document.addEventListener('visibilitychange', () => stopNow(document.hidden ? '页面已隐藏，已停止并锁定' : '页面已恢复，请重新解锁'));
window.addEventListener('pagehide', () => stopNow('页面已离开，已停止并锁定'));
window.addEventListener('pageshow', event => { if (event.persisted) stopNow('页面已恢复，请重新解锁'); });
window.addEventListener('keydown', event => { if (event.key === 'Escape') { event.preventDefault(); stopNow(); } });
setInterval(() => { controller.tick(); renderTelemetry(); }, 25);

const warning = $('supportWarning');
if (!window.isSecureContext) {
  warning.textContent = '此地址不是安全上下文，蓝牙不可用。请通过 HTTPS 打开；手机访问电脑的普通 http://局域网IP 不支持 Web Bluetooth。'; warning.hidden = false;
} else if (!navigator.bluetooth) {
  warning.textContent = '此浏览器没有 Web Bluetooth。请在支持蓝牙的 Android Chrome 中打开 HTTPS 页面，并开启系统蓝牙。'; warning.hidden = false;
} else supported = true;
render();
