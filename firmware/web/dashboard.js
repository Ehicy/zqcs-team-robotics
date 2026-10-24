import {DEFAULT_TRAVEL_PERMILLE,DEFAULT_TURN_PERMILLE} from './dashboard-defaults.js?v=portable-20261024';
import {createBleWriter,BLE_COMMAND_ACK_MS} from './ble-write-watchdog.js?v=ble-servo-link-v11';
import { SameCarLink } from './same-car-link.js?v=softpower-v8r1';
import { CAR_NAME } from './car-identity.js?v=softpower-v8r1';
import { EncoderInitializer } from './encoder-initializer.js?v=softpower-v8r1';
import { installPidTest } from './pid-test.js?v=pid-sliders-v1';
import { installPidPanel } from './pid-panel.js?v=relay-force-v11r1';
import { installServoPanel } from './servo-panel.js?v=portable-20261024';
import { DirectDrive } from './direct-drive.js?v=relay-force-v11r1';
import { PwmDrive, manualPwmLimit } from './pwm-drive.js?v=portable-20261024';
/* C3 uses fresh-held-input preparation via DirectDrive. Historical profiles
 * retain manual ARM. Both use the same queue, session and stale-command stop. */

import { UUID, WHEELS, DEFAULT_CALIBRATION, PA_CHASSIS_CALIBRATION, RemoteController, decodeStatus, validateCalibration } from './protocol.js?v=portable-20261024';
import { KeyHoldSet, mixWheels, describeIntent, isDriveKey, isStopKey, normalizeKey } from './dashboard-input.js';
import { installLiveMap } from './live-map.js';
import { createDashboardAgent, registerAgentTools } from './dashboard-agent.js';
import { installAutomation } from './automation-panel.js?v=portable-20261024';

const $ = id => document.getElementById(id);
let pidPanel = null, pidTest = null, directDrive = null, initializer = null;
const paChassis = document.body.dataset.hardwareProfile === 'c3-pa';
const pwmSelected = () => paChassis && $('driveMode')?.value === 'pwm';
const calibrationDefaults = paChassis ? PA_CHASSIS_CALIBRATION : DEFAULT_CALIBRATION;
const storageKey = 'crtc-lower-car-calibration-v2-rf-lf-rr-lr'; // Historical non-C3 profile only.
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
let liveMap = null;
let dashboardAgent = null;
let automation = null;
let calibrationDirty=false,mechanicalDirty=false,calibrationReadKey='';
function readDeviceCalibration(force=false){
  if(!paChassis)return;
  const ds=controller.deviceSettings,values=ds.calibration();
  if(!values)return;
  const key=`${ds.session}/${ds.revision}`;
  if(key===calibrationReadKey&&!force||calibrationDirty&&!force)return;
  if(!force&&$('calibrationForm').contains(document.activeElement))return;
  setCalibration(values);calibrationReadKey=key;calibrationDirty=false;mechanicalDirty=false;
  liveMap?.invalidate('C3参数回读更新');
}

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
  if(initializer?.active){stopNow('手动输入接管，初始化已取消，请重新按方向');return {throttle:0,steer:0};}
  if(automation?.isRunning()&&(!controller.concurrentMotion()||automation.isDriving())) automation.cancel('手动输入接管自动行驶');
  if (pidTest?.runner.active || pidTest?.preparing) { stopNow('手动输入接管，往复测试已中止'); return { throttle: 0, steer: 0 }; }
  if (dashboardAgent?.isDriving()) {
    stopNow('手动输入接管，Agent 短测已停止并锁定');
    return { throttle: 0, steer: 0 };
  }
  const fromKeys = keys.intent();
  const fromPad = padIntent();
  const intent = {
    throttle: clampAxis(fromKeys.throttle + fromPad.throttle),
    steer: clampAxis(fromKeys.steer + fromPad.steer),
  };
  const changed = intent.throttle !== lastIntent.throttle || intent.steer !== lastIntent.steer;
  lastIntent = intent;

  const accepted = directDrive ? directDrive.request(intent.throttle, intent.steer) : controller.setIntent(intent.throttle, intent.steer);
  if (accepted && changed && announce) {
    log(`意图 ${describeIntent(intent)}（低速 ${intent.throttle} / 转向 ${intent.steer}）`);
  } else if (!accepted && changed && controller.state.connected && !controller.state.armed) {
    $('statusMessage').textContent = paChassis ? '等待双板链路在线' : '尚未解锁：按键只记录意图，不会驱动底盘。';
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
  automation?.clearInput();
}

function manualLimit(intent=lastIntent){
  return pwmSelected()?manualPwmLimit(intent,controller.state.limit,Number($('turnSpeedLimit').value)):controller.state.limit;
}
function paintIntent(intent) {
  $('intentText').textContent = directDrive ? describeIntent(intent) : controller.state.armed ? describeIntent(intent) : `${describeIntent(intent)}（未解锁）`;
  const mix = mixWheels(intent, manualLimit(intent));
  $('mixText').textContent = pwmSelected() ? `左 ${mix.left/10} / 右 ${mix.right/10} %` : `左 ${mix.left} / 右 ${mix.right} mm/s`;
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
  readDeviceCalibration();
  $('connectionText').textContent = state.connected ? (connection?.device.name||CAR_NAME||'已连接车辆') : (connecting ? '正在连接…' : '尚未连接');
  $('connectionDot').classList.toggle('online', state.connected);
  $('connectButton').textContent = state.connected ? '断开连接' : (connecting ? '正在连接…' : '连接蓝牙');
  $('connectButton').disabled = !supported || connecting;

  $('modeBadge').textContent = paChassis ? directDrive?.job ? (pwmSelected()?'PWM 控制中':'PID 控制中') : '待输入' : state.fault && state.connected ? '底盘故障' : state.armed ? '已解锁' : state.armPending ? '等待解锁' : '已锁定';
  $('modeBadge').className = `badge${state.fault && state.connected ? ' fault' : state.armed ? ' armed' : ''}`;

  $('uartStatus').textContent = state.connected ? (state.uart ? '在线' : '未连接') : '—';
  $('readyStatus').textContent = state.connected ? (pwmSelected() ? (state.uart?(controller.servoStatus?.quickProfile?'PID辅助 · 斜坡控制':'PWM 直控'):'等待串口') : state.ready ? '就绪' : '等待 PID 反馈') : '—';
  if($('driveProfileHint')) $('driveProfileHint').textContent = controller.servoStatus?.quickProfile
    ? '快起快停协议在线。需烧入 STM32 0xC3D92417：200ms 曲线起步，松杆/STOP 先撤驱动 10ms、再主动制动最多 30ms，制动 PWM 不超过停止前的本轮输出。当前回传不能核验底盘具体版本。'
    : '等待双板能力；本页兼容旧固件，新增C3 softpower-v13轨迹支持。网页更新不等于车辆已烧入新版。';
  $('calibrationStatus').textContent = `${countBits(state.calibratedMask)} / 4`;
  $('trackSpeeds').textContent = state.connected ? `${state.left} / ${state.right}` : '— / —';

  $('statusMessage').textContent = paChassis ? state.note.replaceAll('请手动解锁','按方向键即可控制').replaceAll('请重新解锁','请重新按方向键').replaceAll('解锁','控制准备').replaceAll('保持锁定','等待输入').replaceAll('并锁定','').replaceAll('仍保持锁定','等待输入').replaceAll('已锁定','已停止') : state.note;
  $('statusMessage').classList.remove('error');
  $('faultMessage').hidden = paChassis || !(state.connected && (state.fault || state.faultCode));
  $('faultMessage').textContent = `故障码 ${state.faultCode} · 原因 ${state.reason}。先排除原因并停稳，清除故障不会自动解锁。`;

  $('armButton').disabled = !controller.canArm() || state.armPending || state.armed;
  $('armButton').hidden = paChassis;
  $('armButton').textContent = state.armed ? '控制已解锁' : state.armPending ? '等待主控确认…' : '解锁控制 ARM';
  $('armHint').textContent = state.armed
    ? '按住 W/A/S/D 行驶；松开归零。空格或 STOP 会重新锁定。'
    : '四轮配置确认、UART 在线、底盘就绪且停稳后才能解锁。';

  if (paChassis) $('armHint').textContent = pwmSelected() ? '按住即可控制；新版STM32持续融合可信编码器，坏轮自动后备，无反馈保留前馈。松开停止。' : 'PID使用当前参数；按住方向即可控制。';
  const padEnabled = paChassis ? state.connected && state.uart : state.armed && state.connected;
  document.querySelectorAll('.key').forEach(button => { button.disabled = !padEnabled; });

  const sessionReady = state.connected && state.session !== 0;
  $('ledOnButton').disabled = !sessionReady || state.configBusy;
  $('ledOffButton').disabled = !sessionReady || state.configBusy;
  const configAllowed = sessionReady && (paChassis?!!controller.deviceSettings.calibration():state.uart) && !state.armed && !state.armPending && !state.remoteArmed && !state.active && !state.configBusy;
  $('saveConfigButton').disabled = !configAllowed;
  $('saveConfigButton').textContent = state.configBusy ? '等待写入确认…' : paChassis?'写入 C3 Flash':'停车后写入四轮配置';
  $('clearFaultButton').disabled = !configAllowed;
  $('clearFaultButton').hidden = paChassis;
  $('calibrationForm').querySelectorAll('input,select').forEach(input => { input.disabled = state.configBusy; });
  $('rememberButton').disabled = state.configBusy;
  $('rememberButton').hidden=paChassis;
  if($('resetEncoders'))$('resetEncoders').disabled=!sessionReady||!state.uart||state.active||state.configBusy||!controller.servoStatus?.encoderReset;
  renderInitializer();
  if($('deviceSettingsText')){
    const ds=controller.deviceSettings;
    $('deviceSettingsText').textContent=!state.connected?'参数唯一来源：C3 Flash；连接后自动读取':!ds.calibration()?'正在读取 C3 参数（约1.5秒）':
      `C3 Flash 修订${ds.revision} · ${ds.pid.loaded?'已持久保存':'存储未加载'} · ${ds.pid.synced?'底盘已同步':'等待底盘同步'}${calibrationDirty?' · 有未写入修改':''}`;
  }
  $('resetFormButton').disabled = state.configBusy;

  $('sessionText').textContent = state.session ? `会话 ${state.session.toString(16).padStart(8, '0')}` : '会话 —';
  $('speedLimit').value = state.limit;
  $('speedValue').textContent = pwmSelected() ? state.limit/10 : state.limit;
  if($('speedLabel')) $('speedLabel').textContent=pwmSelected()?'前后行驶上限':'目标速度上限';
  if($('turnSpeedControl')) $('turnSpeedControl').hidden=!pwmSelected();
  if($('speedUnit')) $('speedUnit').textContent=pwmSelected()?'%':'mm/s';
  if($('mixLabel')) $('mixLabel').textContent=pwmSelected()?'指令输出':'目标轮速';

  // 解锁上升沿：丢弃按住的键，必须重新按下才会动。
  if (!paChassis && state.armed && !lastArmed) {
    keys.clear();
    document.querySelectorAll('.key').forEach(button => button.classList.remove('held'));
    log('已解锁：请重新按下方向键开始行驶');
  } else if (!paChassis && !state.armed && lastArmed) {
    log('已锁定');
  }
  lastArmed = state.armed;

  paintIntent(lastIntent);
  pidPanel?.render();
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
    const validSpeed = speedFresh && !!(wheelSpeeds.validMask & (1 << i));
    if (countCell) countCell.textContent = encoderFresh && encoder.validMask & (1 << i) ? String(encoder.counts[i]) : '—';
    if (speedCell) {
      speedCell.textContent = validSpeed
        ? `${wheelSpeeds.measured[i]} / ${pwmSelected()?'—':wheelSpeeds.targets[i]}` : '— / —';
    }
    const measured = validSpeed ? wheelSpeeds.measured[i] : 0;
    // A PWM request is not a measured/modelled speed target. Missing telemetry
    // must not masquerade as a real zero-speed command in the bar caption.
    const target = validSpeed && !pwmSelected() ? wheelSpeeds.targets[i] : 0;
    const bar = document.querySelector(`.bar-${i}`);
    if (bar) {
      paintBar(bar.querySelector('i.measured'), measured);
      paintBar(bar.querySelector('i.target'), target);
      bar.querySelector('.value').textContent = validSpeed
        ? `${measured} / ${pwmSelected()?'—':target}` : '— / —';
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
function timeout(promise, milliseconds, message='蓝牙操作超时') {
  let timer;
  return Promise.race([promise, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(message)), milliseconds); })])
    .finally(() => clearTimeout(timer));
}

const sameCar=new SameCarLink({bluetooth:navigator.bluetooth,storage:globalThis.localStorage,expectedName:CAR_NAME,service:UUID.service,
  onState:message=>{if($('reconnectStatus'))$('reconnectStatus').textContent=`${CAR_NAME||'已选车辆'} · ${message}`;}});
function retryConnection(){sameCar.schedule(()=>connect(true));}

function closeTransport() {
  initializer?.cancel('蓝牙断开，初始化已取消',false);
  revokeAgentPermission();
  liveMap?.invalidate('蓝牙连接已断开');
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

async function connect(automatic=false) {
  if(!automatic&&controller.state.connected){
    sameCar.pause();initializer?.cancel('主动断开，初始化已取消',false);
    await Promise.race([controller.stop('主动断开').catch(()=>{}),new Promise(r=>setTimeout(r,200))]);
    closeTransport();return;
  }
  if(!supported||connecting||controller.state.connected||automatic&&!sameCar.enabled)return;
  if(!automatic)sameCar.enabled=true;
  const currentAttempt=++attempt;connecting=true;render();let device;
  const current=()=>{if(currentAttempt!==attempt)throw Error('连接已取消');};
  try{
    // Only a human click can request a new browser permission. Retries restore
    // the exact previously authorized ID, never the first matching name.
    device=automatic?await sameCar.restore():(sameCar.device||await sameCar.choose());
    current();
    if(!device){sameCar.pause();log(sameCar.record?'浏览器未能恢复原授权设备，请点连接蓝牙重新选择本车；同页断线仍可自动重连':'请点连接蓝牙，首次选择本车授权');return;}
    sameCar.remember(device);
    const pending=device.gatt.connect().then(server=>{if(currentAttempt!==attempt){if(server.connected)server.disconnect();throw Error('连接已取消');}return server;});
    const server=await timeout(pending,8000,'车辆未上线或被另一台电脑占用');current();
    const service=await timeout(server.getPrimaryService(UUID.service),4000);current();
    const control=await timeout(service.getCharacteristic(UUID.control),4000);current();
    const status=await timeout(service.getCharacteristic(UUID.status),4000);current();
    if(!control.properties.write||typeof control.writeValueWithResponse!=='function')throw Error('设备不支持带响应写入');
    if(!status.properties.read||(!status.properties.notify&&!status.properties.indicate))throw Error('设备缺少状态读取/通知能力');
    let lastBlePacketAt=-Infinity;
    const generation=controller.attach(createBleWriter({
      write:bytes=>control.writeValueWithResponse(bytes),
      onSlow:()=>log('蓝牙写入超过600ms，仍等待本次响应；未重复发送、未断开'),
      timeoutMessage:()=>{
        const age=performance.now()-lastBlePacketAt;
        return `写入响应超时（1500ms；GATT${device.gatt.connected?'仍连接':'已断开'}；${Number.isFinite(age)?'最后回传'+Math.round(age)+'ms前':'尚无回传'}）`;
      }
    }));
    const receive=value=>{
      if(connection?.generation!==generation)return;
      try{
        const packet=decodeStatus(value);if(!controller.ingest(packet,generation))return;lastBlePacketAt=performance.now();
        if(packet.kind===1){encoder=packet;encoderAt=performance.now();liveMap?.receive(packet);automation?.receive(packet,encoderAt);}
        if(packet.kind===2){wheelSpeeds=packet;speedsAt=performance.now();}
        renderTelemetry();
      }catch(error){safely(controller.stop('状态包异常，已停止'));showError(error);}
    };
    const listener=event=>receive(event.target.value);
    const disconnected=()=>{if(connection?.generation===generation){log('蓝牙断开，只重连本车，动作不续跑',true);closeTransport();retryConnection();}};
    connection={device,status,listener,disconnected,generation};
    status.addEventListener('characteristicvaluechanged',listener);
    device.addEventListener('gattserverdisconnected',disconnected);
    await timeout(status.startNotifications(),4000);current();
    receive(await timeout(status.readValue(),2000));current();
    sameCar.succeeded();log(`已连接 ${device.name}；请重新输入操作`);
  }catch(error){
    if(currentAttempt===attempt){
      closeTransport();if(device?.gatt?.connected)device.gatt.disconnect();
      if(error.name==='NotFoundError'&&!device){sameCar.pause();log('未选择设备');}
      else {showError(error);retryConnection();}
    }
  }finally{if(currentAttempt===attempt){connecting=false;render();}}
}

/* ---------------- 校准表单 ---------------- */
function buildCalibration() {
  $('calibrationWheels').innerHTML = WHEELS.map((name, i) => `<fieldset class="wheel-config"><legend>${name} · ${(paChassis ? i >= 2 : i === 3) ? 'EXTI 2×' : '定时器 4×'}</legend><div class="config-grid">
    <label for="wheel${i}_diameter">轮径 mm<input id="wheel${i}_diameter" type="number" min="0.1" max="500" step="0.1" required inputmode="decimal"></label>
    <label for="wheel${i}_cpr">每轮每圈计数<input id="wheel${i}_cpr" type="number" min="1" max="1000000" step="1" required inputmode="numeric"></label>
    <label for="wheel${i}_motor">电机方向<select id="wheel${i}_motor"><option value="1">+ 正向</option><option value="-1">− 反向</option></select></label>
    <label for="wheel${i}_encoder">编码器方向<select id="wheel${i}_encoder"><option value="1">+ 正向</option><option value="-1">− 反向</option></select></label>


    <label for="wheel${i}_max">${paChassis?'满PWM参考轮速 / 历史速度上限':'最大轮速'} mm/s<input id="wheel${i}_max" type="number" min="1" max="1000" step="1" required inputmode="numeric"></label>
    </div><label class="confirmation" ${paChassis ? 'hidden' : ''}><input id="wheel${i}_confirmed" type="checkbox"><span>我已实测核对本轮的方向、每圈计数和轮径</span></label></fieldset>`).join('');
  let values = calibrationDefaults.map(value => ({ ...value }));
  if(!paChassis)try {
    let saved = JSON.parse(localStorage.getItem(storageKey));
    if (Array.isArray(saved) && saved.length === 4) {
      const checked = saved.map(value => validateCalibration({ ...value, confirmed: false }));
      if (checked.every((value, i) => value.wheel === i)) values = checked;
    }
  } catch { /* 私有模式或损坏的存储：保留显式未确认的初值。 */ }
  setCalibration(values);
}

function setCalibration(values) {
  pidPanel.set(values[0]);
  WHEELS.forEach((_, wheel) => {
    const value = values[wheel];
    const field = name => $(`wheel${wheel}_${name}`);
    field('diameter').value = value.diameterTenthMm / 10;
    field('cpr').value = value.countsPerRev;
    field('motor').value = value.motorNegative ? '-1' : '1';
    field('encoder').value = value.encoderNegative ? '-1' : '1';
    field('confirmed').checked = value.confirmed;
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
      ...pidPanel.read(),

      maxMmS: Number(field('max').value),
    });
  });
}

/* ---------------- 控制器 ---------------- */
const controller = new RemoteController({
  deviceStorage:paChassis,ackTimeoutMs:BLE_COMMAND_ACK_MS,
  changed: render,
  locked: event => { automation?.onControllerLock(event); directDrive?.cancel('控制已停止', false); pidTest?.runner.cancel('控制停止，往复测试已中止'); dashboardAgent?.onLock(); revokeAgentPermission(); clearHolds(); },
  transportError: error => { closeTransport(); showError(new Error(`蓝牙写入失败：${error.message}`)); retryConnection(); },
});

/* ---------------- 界面装配 ---------------- */
$('telemetryRows').innerHTML = WHEELS.map((name, i) =>
  `<tr><td>${name}</td><td id="count${i}">—</td><td id="wheelSpeed${i}">— / —</td></tr>`).join('');
$('wheelBars').innerHTML = WHEELS.map((name, i) =>
  `<div class="bar bar-${i}"><span>${name.split(' ')[0]}</span><div class="track"><i class="target"></i><i class="measured"></i></div><span class="value">— / —</span></div>`).join('');
pidPanel = installPidPanel({ root: $('pidPanel'), controller, error: showError, log: typeof log === 'function' ? log : () => {}, written:(_gains,{pending=false}={})=>{calibrationDirty=mechanicalDirty||pending;calibrationReadKey='';},direct: paChassis });
buildCalibration();
$('calibrationForm').addEventListener('input',()=>{calibrationDirty=true;mechanicalDirty=true;});
$('pidPanel').addEventListener('input',()=>{calibrationDirty=true;});
function createDrive() {
  const options={controller,turnLimit:()=>Number($('turnSpeedLimit').value),calibration:readCalibration,foreground:()=>!document.hidden&&document.hasFocus(),error:showError,changed:()=>render()};
  return pwmSelected()?new PwmDrive(options):new DirectDrive(options);
}
if(paChassis) directDrive=createDrive();
$('driveMode')?.addEventListener('change',()=>{
  stopNow('行驶方式已切换'); directDrive=createDrive();
  controller.setLimit(pwmSelected()?DEFAULT_TRAVEL_PERMILLE:100); render();
});
liveMap = installLiveMap({
  snapshot: () => ({ ...controller.state, mainAt: controller.mainStatusAt,
    encoder, encoderAt, speed: wheelSpeeds, speedAt: speedsAt }),
  calibration: readCalibration,
});

$('connectButton').addEventListener('click', () => safely(connect()));
$('armButton').addEventListener('click', () => safely(controller.arm()));
$('speedLimit').addEventListener('input', event => {
  if (pidTest?.runner.active) stopNow('手动改速，往复测试已中止');
  if (dashboardAgent?.isDriving()) stopNow('手动调整速度，Agent 短测已停止并锁定');
  const value = Number(event.target.value);
  controller.setLimit(value);
  $('speedValue').textContent = pwmSelected()?value/10:value;
  paintIntent(lastIntent);
});

$('turnSpeedLimit').addEventListener('input',()=>{
  $('turnSpeedValue').textContent=Number($('turnSpeedLimit').value)/10;paintIntent(lastIntent);
});

function stopNow(note = '已停止') {
  initializer?.cancel(note,false);
  automation?.cancel(note);
  pidTest?.runner.cancel(note);
  pidTest?.cancelPreparation();
  directDrive?.cancel(note, false);
  clearHolds();
  safely(controller.stop(note));
  log(note);
}
$('stopButton').addEventListener('pointerdown', event => { event.preventDefault(); stopNow('按下 STOP：已停止并锁定'); });
$('stopButton').addEventListener('click', event => { if (event.detail === 0) stopNow('按下 STOP：已停止并锁定'); });
$('ledOnButton').addEventListener('click', () => safely(controller.led(true).then(() => log('已发送板载灯 亮'))));
$('ledOffButton').addEventListener('click', () => safely(controller.led(false).then(() => log('已发送板载灯 灭'))));
$('resetEncoders')?.addEventListener('click',()=>safely((async()=>{
  stopNow('编码器清零：停止控制');automation?.encoderReset?.();liveMap?.invalidate('编码器已清零，请重设起点');
  // STOP rotates the control session. Wait for its fresh status before reset.
  await new Promise(resolve=>setTimeout(resolve,200));
  await controller.resetEncoders();encoder=null;encoderAt=-Infinity;wheelSpeeds=null;speedsAt=-Infinity;
  log('四轮编码器已清零；后续计数以新零点为准');renderTelemetry();
})()));
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
    safely(controller.configure(values).then(() => {calibrationDirty=false;mechanicalDirty=false;calibrationReadKey='';log(paChassis?'已写 C3 Flash，正在回读确认':'统一 PID 与四轮配置已逐包确认（仅存 RAM）');}));
  } catch (error) { showError(error); }
});
$('rememberButton').addEventListener('click', () => {
  if(paChassis)return;
  try {
    localStorage.setItem(storageKey, JSON.stringify(readCalibration()));
    $('statusMessage').textContent = '已记住本机表单；未下发配置，也不会自动解锁。';
    log('已记住本机表单');
  } catch (error) { showError(error); }
});
$('resetFormButton').addEventListener('click', () => {
  if(paChassis){readDeviceCalibration(true);$('statusMessage').textContent='已重新读取 C3 参数，未执行运动';return;}
  setCalibration(calibrationDefaults);
  $('statusMessage').textContent = '已恢复待验证初值；未发送到车辆。';
  log('表单已恢复初值');
});

/* 虚拟键盘：鼠标/触控按住等效于对应按键。 */
document.querySelectorAll('.key').forEach(button => {
  button.addEventListener('contextmenu', event => event.preventDefault());
  button.addEventListener('pointerdown', event => {
    event.preventDefault();
    if (!(paChassis ? controller.state.connected && controller.state.uart : controller.state.armed) || heldPadPointer !== null || event.button !== 0) return;
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
  return tag === 'INPUT' && target.type !== 'range' || tag === 'SELECT' || tag === 'TEXTAREA' || target.isContentEditable || !!target.closest?.('dialog[open]');
}
window.addEventListener('keydown', event => {
  if (event.ctrlKey || event.metaKey || event.altKey || typingInField(event.target)) return;
  // Keep range arrow-key adjustment; WASD/function keys remain available while
  // the operator drags a servo slider with the mouse.
  if(event.target?.type==='range'&&event.key.startsWith('Arrow'))return;
  if (isStopKey(event.key)) {
    event.preventDefault();
    automation?.keyEvent('down',event);
    if (!event.repeat) stopNow('按键急停：已停止并锁定');
    return;
  }
  if (!isDriveKey(event.key)) { if(automation?.keyEvent('down',event))event.preventDefault();return; }
  event.preventDefault();
  const normalized = normalizeKey(event.key);
  if (event.repeat) return; // STOP/session cancellation always needs a fresh physical press.
  if (keys.press(normalized)) applyIntent({ announce: true });
  automation?.keyEvent('down',event);
});
window.addEventListener('keyup', event => {
  const captured=keys.held.has(normalizeKey(event.key))||automation?.recorder.held.has(normalizeKey(event.key));
  if((typingInField(event.target)||event.ctrlKey||event.metaKey||event.altKey)&&!captured)return;
  if (isDriveKey(event.key)) {
    event.preventDefault();
    if (keys.release(event.key)) applyIntent({ announce: true });
    automation?.keyEvent('up',event);
  } else automation?.keyEvent('up',event);
});

window.addEventListener('blur', () => { revokeAgentPermission(); stopNow('页面失去焦点，已停止并锁定'); });
document.addEventListener('visibilitychange', () => { revokeAgentPermission(); stopNow(document.hidden ? '页面已隐藏，已停止并锁定' : '页面已恢复，请重新解锁'); });
window.addEventListener('pagehide', () => { revokeAgentPermission(); stopNow('页面已离开，已停止并锁定'); });
window.addEventListener('pageshow', event => { if (event.persisted) stopNow('页面已恢复，请重新解锁'); });

setInterval(() => { initializer?.tick(); directDrive?.tick(); pidTest?.tick(); dashboardAgent?.tick(); controller.tick(); automation?.tick(); renderTelemetry(); liveMap.tick(); }, 25);

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

/* 旧调试名称只返回副本，不再暴露可绕过门禁的 controller/queue。 */
window.__crtcDashboard = Object.freeze({
  intent: () => ({ ...lastIntent }),
  state: () => ({ ...controller.state }),
  mixWheels,
  describeIntent,
});

$('turnSpeedLimit').value=String(DEFAULT_TURN_PERMILLE);
$('turnSpeedValue').textContent=DEFAULT_TURN_PERMILLE/10;
controller.setLimit(paChassis?DEFAULT_TRAVEL_PERMILLE:80);
log('遥控台已就绪：等待连接 CRTC-Lower-Car');
render();

const servoPanel=installServoPanel(controller,{onTarget:e=>automation?.target(e)});
automation=installAutomation({root:$('automationPanel'),controller,calibration:readCalibration,
  telemetry:()=>({encoder,encoderAt}),intent:()=>({...lastIntent}),driveLimit:manualLimit,
  takeover:()=>{initializer?.cancel('自动动作接管');directDrive?.cancel('自动动作接管',false);pidTest?.runner.cancel('自动动作接管');clearHolds();}});
Object.defineProperty(window,'crtcMotion',{value:Object.freeze({status:()=>automation?.status(),export:()=>automation?.library.export()}),configurable:true});
pidTest = installPidTest({ root: $('pidTestPanel'), controller,
  telemetry: () => ({ encoder, encoderAt, speed: wheelSpeeds, speedAt: speedsAt }), calibration: readCalibration,
  foreground: () => !document.hidden && document.hasFocus(), error: showError, log, direct: paChassis });

/* ---------------- Agent 接口（同页、无额外网络端口） ---------------- */
function revokeAgentPermission() {
  if ($('agentWriteAllowed')) $('agentWriteAllowed').checked = false;
  if ($('agentMotionAllowed')) $('agentMotionAllowed').checked = false;
}

dashboardAgent = createDashboardAgent({
  controller,
  telemetry: () => ({ encoder, encoderAt, speed: wheelSpeeds, speedAt: speedsAt }),
  calibration: readCalibration,
  externalMotion: () => !!pidTest?.runner.active||!!initializer?.active,
  permissions: () => ({ write: $('agentWriteAllowed').checked, motion: $('agentMotionAllowed').checked }),
  moveServo: (channel,pulse,reference) => servoPanel.move(channel,pulse,reference),
  foreground: () => !document.hidden && document.hasFocus(),
  clearInput: clearHolds,
  paintInput: (throttle, steer) => { lastIntent = { throttle, steer }; paintIntent(lastIntent); },
  log,
});
Object.defineProperty(window, 'crtcAgent', { value: Object.freeze({ version: 1,
  status: dashboardAgent.snapshot, execute: dashboardAgent.execute }), configurable: true });
let webMcp = false;
try { webMcp = registerAgentTools(navigator.modelContext, dashboardAgent); }
catch (error) { log(`WebMCP 注册不可用，JSON/API 接口仍可用：${error.message}`, true); }
$('agentTransport').textContent = webMcp ? 'WebMCP + window.crtcAgent + JSON 命令均可用' : 'window.crtcAgent + JSON 命令可用（此浏览器未提供 WebMCP）';
let agentSession = controller.state.session;
function renderAgent() {
  if (agentSession !== controller.state.session) { revokeAgentPermission(); agentSession = controller.state.session; }
  const s = dashboardAgent.snapshot();
  $('agentMotionAllowed').disabled = !s.permissions.write;
  if (!s.permissions.write) $('agentMotionAllowed').checked = false;
  $('agentDiagnosis').textContent = paChassis && pwmSelected() ? (s.state.uart?'PWM 直控在线，编码器读数不影响手动行驶':'等待串口在线') : s.blockers.length ? s.blockers.map(b => b.message).join('；')
    : s.state.armed ? '底盘已解锁；运动命令仍需独立许可' : '底盘已就绪，可手动解锁';
  $('agentLiveStatus').textContent = JSON.stringify(s, null, 2);
}
function agentPermissionChanged() {
  if (!$('agentWriteAllowed').checked) $('agentMotionAllowed').checked = false;
  if (dashboardAgent.isDriving() || controller.state.armPending || controller.state.armed) stopNow('Agent 许可变更，已停止并锁定');
  renderAgent();
}
$('agentWriteAllowed').addEventListener('change', agentPermissionChanged);
$('agentMotionAllowed').addEventListener('change', agentPermissionChanged);
$('agentCommandForm').addEventListener('submit', async event => {
  event.preventDefault();
  try {
    const request = JSON.parse($('agentCommand').value);
    const result = await dashboardAgent.execute(request);
    $('agentResult').textContent = JSON.stringify(result, null, 2);
  } catch (error) { $('agentResult').textContent = JSON.stringify({ ok: false, error: { code: 'INVALID_JSON', message: error.message } }, null, 2); }
  renderAgent();
});
$('agentReadStatus').addEventListener('click', () => {
  renderAgent(); $('agentResult').textContent = JSON.stringify({ ok: true, status: dashboardAgent.snapshot() }, null, 2);
});
$('agentStop').addEventListener('click', async () => {
  const result = await dashboardAgent.execute({ command: 'stop' });
  $('agentResult').textContent = JSON.stringify(result, null, 2); renderAgent();
});
setInterval(renderAgent, 250);
renderAgent();

/* Explicit one-click initialization. Never runs on boot/reconnect. */
function renderInitializer(){
  if(!initializer)return;
  $('initializeEncoders').disabled=initializer.active||!controller.state.connected||!controller.state.uart||controller.state.configBusy;
  $('cancelInitialization').disabled=!initializer.active;
  $('initializationStatus').textContent=initializer.message;
}
initializer=new EncoderInitializer({controller,telemetry:()=>({encoder,encoderAt}),
  foreground:()=>!document.hidden&&document.hasFocus(),error:showError,
  takeover:()=>{if(automation?.recorder.active||automation?.isRunning()||pidTest?.runner.active||pidTest?.preparing||dashboardAgent?.isDriving()||directDrive?.job)throw Error('请先停止当前录制/动作，再初始化');clearHolds();},
  completed:()=>{automation?.encoderReset?.();liveMap?.invalidate('微动初始化已清零，重设起点');log('编码器微动初始化完成');},changed:renderInitializer});
$('initializeEncoders').addEventListener('click',()=>{try{initializer.start();}catch(e){showError(e);}});
$('cancelInitialization').addEventListener('click',()=>stopNow('初始化已取消'));
$('pidTestStart')?.addEventListener('click',()=>initializer.cancel('往复测试接管'),true);
$('pauseReconnect').addEventListener('click',()=>{sameCar.pause();stopNow('暂停连接');closeTransport();});
renderInitializer();
if($('reconnectStatus'))$('reconnectStatus').textContent=`本包绑定：${CAR_NAME||'首次手选车辆'} · 首次授权后自动重连。换电脑请先断开旧电脑。`;
if(supported&&sameCar.record){
  if(sameCar.enabled)void connect(true);
  else {sameCar.pause();void sameCar.restore().catch(()=>{});}
}
