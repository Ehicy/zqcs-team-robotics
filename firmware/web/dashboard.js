/* CRTC 下层车 BLE 遥控台：桌面 WASD 控制。
 *
 * 复用 firmware/web/protocol.js 的线协议与安全状态机（同一个 20 字节格式、
 * 同一个单在途写队列、同一个会话/解锁门禁），本文件只负责 DOM、键盘生命周期
 * 与遥测呈现。安全约定与手机面板一致，不因改成键盘而放宽：
 *   - 只有 ARM 之后才发 DRIVE；未解锁时按键只显示意图，不产生运动。
 *   - 松开全部方向键立即发零 DRIVE（保留解锁）。
 *   - 页面失焦 / 隐藏 / 离开、事件循环卡顿 ≥150ms、断连一律停止并锁定。
 *   - 解锁瞬间清空按住的键，必须重新按下才动，避免"按住 W 再点 ARM"直接窜车。
 */

import { UUID, WHEELS, DEFAULT_CALIBRATION, RemoteController, decodeStatus, validateCalibration } from './protocol.js';
import { KeyHoldSet, mixWheels, describeIntent, isDriveKey, isStopKey, normalizeKey } from './dashboard-input.js';

const $ = id => document.getElementById(id);
const storageKey = 'crtc-lower-car-calibration-v1';
const BAR_FULL_MM_S = 300;

let connection = null;
let connecting = false;
let attempt = 0;
let supported = false;
let encoder = null;
let wheelSpeeds = null;
let encoderAt = -Infinity;
let speedsAt = -Infinity;
let lastArmed = false;
let heldPad = null;
let heldPadPointer = null;

const keys = new KeyHoldSet();
let lastIntent = { throttle: 0, steer: 0 };

/* ---------------- 日志 ---------------- */
function log(message, isError = false) {
  const list = $('log');
  if (!list) return;
  const item = document.createElement('li');
  if (isError) item.className = 'error';
  const time = document.createElement('time');
  time.textContent = new Date().toLocaleTimeString('zh-CN', { hour12: false });
  const span = document.createElement('span');
  span.textContent = message;
  item.append(time, span);
  list.append(item);
  while (list.children.length > 200) list.removeChild(list.firstChild);
  list.scrollTop = list.scrollHeight;
}

function showError(error) {
  const message = error?.message || String(error);
  controller.state.note = message;
  $('statusMessage').textContent = message;
  $('statusMessage').classList.add('error');
  log(message, true);
}
function safely(promise) { promise.catch(showError); }

/* ---------------- 意图 ---------------- */
function clampAxis(value) { return Math.max(-1000, Math.min(1000, value)); }

function padIntent() {
  if (!heldPad) return { throttle: 0, steer: 0 };
  return { throttle: Number(heldPad.dataset.throttle), steer: Number(heldPad.dataset.steer) };
}

/** 合并键盘与虚拟键盘的意图，写入控制器并刷新读数。 */
function applyIntent({ announce = false } = {}) {
  const fromKeys = keys.intent();
  const fromPad = padIntent();
  const intent = {
    throttle: clampAxis(fromKeys.throttle + fromPad.throttle),
    steer: clampAxis(fromKeys.steer + fromPad.steer),
  };
  const changed = intent.throttle !== lastIntent.throttle || intent.steer !== lastIntent.steer;
  lastIntent = intent;

  const accepted = controller.setIntent(intent.throttle, intent.steer);
  if (accepted && changed && announce) {
    log(`意图 ${describeIntent(intent)}（低速 ${intent.throttle} / 转向 ${intent.steer}）`);
  } else if (!accepted && changed && controller.state.connected && !controller.state.armed) {
    $('statusMessage').textContent = '尚未解锁：按键只记录意图，不会驱动底盘。';
  }
  paintIntent(intent);
  return intent;
}

function clearHolds(note) {
  const had = keys.clear();
  heldPad = null; heldPadPointer = null;
  document.querySelectorAll('.key').forEach(button => button.classList.remove('held'));
  if (had || lastIntent.throttle || lastIntent.steer) {
    lastIntent = { throttle: 0, steer: 0 };
    controller.setIntent(0, 0);
  }
  if (note) log(note);
  paintIntent({ throttle: 0, steer: 0 });
}

function paintIntent(intent) {
  $('intentText').textContent = controller.state.armed ? describeIntent(intent) : `${describeIntent(intent)}（未解锁）`;
  const mix = mixWheels(intent, controller.state.limit);
  $('mixText').textContent = `左 ${mix.left} / 右 ${mix.right} mm/s`;
  const active = {
    w: intent.throttle > 0 && !intent.steer,
    s: intent.throttle < 0 && !intent.steer,
    a: intent.steer < 0 && !intent.throttle,
    d: intent.steer > 0 && !intent.throttle,
  };
  for (const [name, on] of Object.entries(active)) {
    document.querySelector(`.key-${name}`)?.classList.toggle('held', on);
  }
}

/* ---------------- 渲染 ---------------- */
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
  $('trackSpeeds').textContent = state.connected ? `${state.left} / ${state.right}` : '— / —';

  $('statusMessage').textContent = state.note;
  $('statusMessage').classList.remove('error');
  $('faultMessage').hidden = !(state.connected && (state.fault || state.faultCode));
  $('faultMessage').textContent = `故障码 ${state.faultCode} · 原因 ${state.reason}。先排除原因并停稳，清除故障不会自动解锁。`;

  $('armButton').disabled = !controller.canArm() || state.armPending || state.armed;
  $('armButton').textContent = state.armed ? '控制已解锁' : state.armPending ? '等待主控确认…' : '解锁控制 ARM';
  $('armHint').textContent = state.armed
    ? '按住 W/A/S/D 行驶；松开归零。空格或 STOP 会重新锁定。'
    : '四轮配置确认、UART 在线、底盘就绪且停稳后才能解锁。';

  const padEnabled = state.armed && state.connected;
  document.querySelectorAll('.key').forEach(button => { button.disabled = !padEnabled; });

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
  $('speedLimit').value = state.limit;
  $('speedValue').textContent = state.limit;

  // 解锁上升沿：丢弃按住的键，必须重新按下才会动。
  if (state.armed && !lastArmed) {
    keys.clear();
    document.querySelectorAll('.key').forEach(button => button.classList.remove('held'));
    log('已解锁：请重新按下方向键开始行驶');
  } else if (!state.armed && lastArmed) {
    log('已锁定');
  }
  lastArmed = state.armed;

  paintIntent(lastIntent);
  renderTelemetry();
}

function renderTelemetry() {
  const now = performance.now();
  const mainFresh = controller.state.connected && now - controller.mainStatusAt < 500;
  if (!mainFresh) $('trackSpeeds').textContent = controller.state.connected ? '— / —' : '— / —';

  const encoderFresh = controller.state.connected && now - encoderAt < 1000;
  const speedFresh = controller.state.connected && now - speedsAt < 1000;

  WHEELS.forEach((_, i) => {
    const countCell = $(`count${i}`);
    const speedCell = $(`wheelSpeed${i}`);
    if (countCell) countCell.textContent = encoderFresh && encoder.validMask & (1 << i) ? String(encoder.counts[i]) : '—';
    if (speedCell) {
      speedCell.textContent = speedFresh && wheelSpeeds.validMask & (1 << i)
        ? `${wheelSpeeds.measured[i]} / ${wheelSpeeds.targets[i]}` : '— / —';
    }
    const measured = speedFresh && wheelSpeeds.validMask & (1 << i) ? wheelSpeeds.measured[i] : 0;
    const target = speedFresh && wheelSpeeds.validMask & (1 << i) ? wheelSpeeds.targets[i] : 0;
    const bar = document.querySelector(`.bar-${i}`);
    if (bar) {
      paintBar(bar.querySelector('i.measured'), measured);
      paintBar(bar.querySelector('i.target'), target);
      bar.querySelector('.value').textContent = speedFresh ? `${measured} / ${target}` : '— / —';
    }
  });
  $('telemetryFreshness').textContent = encoderFresh || speedFresh ? '样本在线' : '等待采样 / 已过期';
}

function paintBar(element, value) {
  if (!element) return;
  const percent = Math.min(50, Math.abs(value) / BAR_FULL_MM_S * 50);
  element.style.width = `${percent}%`;
  element.style.left = value >= 0 ? '50%' : `${50 - percent}%`;
}

/* ---------------- 蓝牙 ---------------- */
function timeout(promise, milliseconds) {
  let timer;
  return Promise.race([promise, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('写入响应超时')), milliseconds); })])
    .finally(() => clearTimeout(timer));
}

function closeTransport() {
  attempt++;
  const old = connection;
  connection = null;
  if (old) {
    old.status.removeEventListener('characteristicvaluechanged', old.listener);
    old.device.removeEventListener('gattserverdisconnected', old.disconnected);
    if (old.device.gatt.connected) old.device.gatt.disconnect();
  }
  controller.disconnect();
  connecting = false;
  encoder = wheelSpeeds = null;
  encoderAt = speedsAt = -Infinity;
  clearHolds();
  render();
}

async function connect() {
  if (controller.state.connected) {
    log('正在断开连接');
    await Promise.race([controller.stop('正在断开，保持锁定').catch(() => {}), new Promise(resolve => setTimeout(resolve, 200))]);
    closeTransport();
    return;
  }
  if (!supported || connecting) return;
  const currentAttempt = ++attempt;
  connecting = true;
  render();
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
        if (packet.kind === 1) { encoder = packet; encoderAt = performance.now(); log(`编码器计数 ${packet.counts.join(' / ')}`); }
        if (packet.kind === 2) { wheelSpeeds = packet; speedsAt = performance.now(); }
        renderTelemetry();
      } catch (error) {
        safely(controller.stop('状态包异常，已停止并锁定'));
        showError(error);
      }
    };
    const listener = event => receive(event.target.value);
    const disconnected = () => { if (connection?.generation === generation) { log('蓝牙已断开，保持锁定', true); closeTransport(); } };
    connection = { device, status, listener, disconnected, generation };
    status.addEventListener('characteristicvaluechanged', listener);
    device.addEventListener('gattserverdisconnected', disconnected);
    await status.startNotifications();
    receive(await status.readValue());
    log('已连接 CRTC-Lower-Car，新会话默认锁定');
  } catch (error) {
    if (device?.gatt?.connected) device.gatt.disconnect();
    if (currentAttempt === attempt) closeTransport();
    showError(error.name === 'NotFoundError' ? new Error('未选择设备；保持锁定') : error);
  } finally {
    connecting = false;
    render();
  }
}

/* ---------------- 校准表单 ---------------- */
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
  } catch { /* 私有模式或损坏的存储：保留显式未确认的初值。 */ }
  setCalibration(values);
}

function setCalibration(values) {
  WHEELS.forEach((_, wheel) => {
    const value = values[wheel];
    const field = name => $(`wheel${wheel}_${name}`);
    field('diameter').value = value.diameterTenthMm / 10;
    field('cpr').value = value.countsPerRev;
    field('motor').value = value.motorNegative ? '-1' : '1';
    field('encoder').value = value.encoderNegative ? '-1' : '1';
    field('confirmed').checked = value.confirmed;
    field('kp').value = value.kpMilli;
    field('ki').value = value.kiMilli;
    field('max').value = value.maxMmS;
  });
}

function readCalibration() {
  return WHEELS.map((_, wheel) => {
    const field = name => $(`wheel${wheel}_${name}`);
    const diameter = Number(field('diameter').value);
    if (!Number.isFinite(diameter) || Math.abs(diameter * 10 - Math.round(diameter * 10)) > 0.00001) {
      throw new Error(`${WHEELS[wheel]} 轮径最多保留一位小数`);
    }
    return validateCalibration({
      wheel,
      motorNegative: field('motor').value === '-1',
      encoderNegative: field('encoder').value === '-1',
      confirmed: field('confirmed').checked,
      diameterTenthMm: Math.round(diameter * 10),
      countsPerRev: Number(field('cpr').value),
      kpMilli: Number(field('kp').value),
      kiMilli: Number(field('ki').value),
      maxMmS: Number(field('max').value),
    });
  });
}

/* ---------------- 控制器 ---------------- */
const controller = new RemoteController({
  changed: render,
  locked: () => { clearHolds(); },
  transportError: error => { closeTransport(); showError(new Error(`蓝牙写入失败，已锁定：${error.message}`)); },
});

/* ---------------- 界面装配 ---------------- */
$('telemetryRows').innerHTML = WHEELS.map((name, i) =>
  `<tr><td>${name}</td><td id="count${i}">—</td><td id="wheelSpeed${i}">— / —</td></tr>`).join('');
$('wheelBars').innerHTML = WHEELS.map((name, i) =>
  `<div class="bar bar-${i}"><span>${name.split(' ')[0]}</span><div class="track"><i class="target"></i><i class="measured"></i></div><span class="value">— / —</span></div>`).join('');
buildCalibration();

$('connectButton').addEventListener('click', () => safely(connect()));
$('armButton').addEventListener('click', () => safely(controller.arm()));
$('speedLimit').addEventListener('input', event => {
  const value = Number(event.target.value);
  controller.setLimit(value);
  $('speedValue').textContent = value;
  paintIntent(lastIntent);
});

function stopNow(note = '已停止并锁定') {
  clearHolds();
  safely(controller.stop(note));
  log(note);
}
$('stopButton').addEventListener('pointerdown', event => { event.preventDefault(); stopNow('按下 STOP：已停止并锁定'); });
$('stopButton').addEventListener('click', event => { if (event.detail === 0) stopNow('按下 STOP：已停止并锁定'); });
$('ledOnButton').addEventListener('click', () => safely(controller.led(true).then(() => log('已发送板载灯 亮'))));
$('ledOffButton').addEventListener('click', () => safely(controller.led(false).then(() => log('已发送板载灯 灭'))));
$('clearFaultButton').addEventListener('click', () => safely(controller.clearFault().then(() => log('清故障请求已确认'))));
$('calibrationForm').addEventListener('submit', event => {
  event.preventDefault();
  try {
    if (wheelSpeeds && performance.now() - speedsAt < 1000 &&
        wheelSpeeds.measured.some((speed, i) => (wheelSpeeds.validMask & (1 << i)) && speed !== 0)) {
      throw new Error('仍测到轮速，请等待四轮停稳后再写入');
    }
    const values = readCalibration();
    log(`开始写入四轮配置（已确认 ${values.filter(v => v.confirmed).length} / 4）`);
    safely(controller.configure(values).then(() => log('四轮配置已逐包确认（仅存 RAM）')));
  } catch (error) { showError(error); }
});
$('rememberButton').addEventListener('click', () => {
  try {
    localStorage.setItem(storageKey, JSON.stringify(readCalibration()));
    $('statusMessage').textContent = '已记住本机表单；未下发配置，也不会自动解锁。';
    log('已记住本机表单');
  } catch (error) { showError(error); }
});
$('resetFormButton').addEventListener('click', () => {
  setCalibration(DEFAULT_CALIBRATION);
  $('statusMessage').textContent = '已恢复待验证初值；未发送到车辆。';
  log('表单已恢复初值');
});

/* 虚拟键盘：鼠标/触控按住等效于对应按键。 */
document.querySelectorAll('.key').forEach(button => {
  button.addEventListener('contextmenu', event => event.preventDefault());
  button.addEventListener('pointerdown', event => {
    event.preventDefault();
    if (!controller.state.armed || heldPadPointer !== null || event.button !== 0) return;
    heldPad = button; heldPadPointer = event.pointerId;
    button.setPointerCapture(event.pointerId);
    button.classList.add('held');
    applyIntent({ announce: true });
  });
  const release = () => {
    if (heldPadPointer === null) return;
    heldPad = null; heldPadPointer = null;
    applyIntent();
  };
  button.addEventListener('pointerup', event => { if (heldPadPointer === event.pointerId) { event.preventDefault(); release(); } });
  for (const name of ['pointercancel', 'lostpointercapture']) {
    button.addEventListener(name, event => { if (heldPadPointer === event.pointerId) stopNow('指针中断，已停止并锁定'); });
  }
});

/* 键盘：WASD 与方向键；只在非输入控件上生效，避免填表时窜车。 */
function typingInField(target) {
  if (!target) return false;
  const tag = target.tagName;
  return tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || target.isContentEditable;
}
window.addEventListener('keydown', event => {
  if (event.ctrlKey || event.metaKey || event.altKey || typingInField(event.target)) return;
  if (isStopKey(event.key)) {
    event.preventDefault();
    if (!event.repeat) stopNow('按键急停：已停止并锁定');
    return;
  }
  if (!isDriveKey(event.key)) return;
  event.preventDefault();
  const normalized = normalizeKey(event.key);
  if (event.repeat) {
    // 长按自动重复：键已在集合里，忽略即可，避免重复刷新意图。
    if (keys.held.has(normalized)) return;
  }
  if (keys.press(normalized)) applyIntent({ announce: true });
});
window.addEventListener('keyup', event => {
  if (typingInField(event.target)) return;
  if (!isDriveKey(event.key)) return;
  event.preventDefault();
  if (keys.release(event.key)) applyIntent({ announce: true });
});

window.addEventListener('blur', () => stopNow('页面失去焦点，已停止并锁定'));
document.addEventListener('visibilitychange', () => stopNow(document.hidden ? '页面已隐藏，已停止并锁定' : '页面已恢复，请重新解锁'));
window.addEventListener('pagehide', () => stopNow('页面已离开，已停止并锁定'));
window.addEventListener('pageshow', event => { if (event.persisted) stopNow('页面已恢复，请重新解锁'); });

setInterval(() => { controller.tick(); renderTelemetry(); }, 25);

/* 支持性检查 */
const warning = $('supportWarning');
if (!window.isSecureContext) {
  warning.textContent = '此地址不是安全上下文，Web Bluetooth 不可用。请通过 http://localhost 或 HTTPS 打开本页。';
  warning.hidden = false;
} else if (!navigator.bluetooth) {
  warning.textContent = '此浏览器没有 Web Bluetooth。请使用桌面版 Chrome / Edge（系统蓝牙需开启）。';
  warning.hidden = false;
} else {
  supported = true;
}

/* 只读调试钩子（供自动化测试与现场排查使用）。 */
window.__crtcDashboard = {
  controller,
  keys,
  intent: () => ({ ...lastIntent }),
  state: () => ({ ...controller.state }),
  mixWheels,
  describeIntent,
};

log('遥控台已就绪：等待连接 CRTC-Lower-Car');
render();
