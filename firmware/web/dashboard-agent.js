/* Agent facade: the existing controller owns BLE, ACKs, session and safety.
 * No raw packet writer, auto-calibration, background motion or network listener.
 */
import { WHEELS } from './protocol.js';

export const AGENT_VERSION = 1;
export const AGENT_COMMANDS = Object.freeze(['status', 'stop', 'led', 'configure', 'clearFault', 'arm', 'drive', 'servo']);

function fail(code, message) { throw Object.assign(new Error(message), { code }); }
function integer(value, min, max, name) {
  if (!Number.isInteger(value) || value < min || value > max) fail('INVALID_ARGUMENT', `${name} 必须是 ${min}–${max} 的整数`);
  return value;
}
function age(now, at) { return Number.isFinite(at) ? Math.max(0, now - at) : null; }
function clone(value) { return JSON.parse(JSON.stringify(value)); }

export function createDashboardAgent({ controller, telemetry, calibration,
  permissions = () => ({ write: false, motion: false }),
  moveServo = (channel,pulse,reference) => controller.servo(channel,pulse,null,reference),
  foreground = () => true, clearInput = () => {}, paintInput = () => {},
  log = () => {}, now = () => performance.now(), externalMotion = () => false,
}) {
  let lease = null, busy = false, epoch = 0;
  const audit = [];

  function snapshot() {
    const s = controller.state, t = telemetry(), time = now();
    const mainAge = age(time, controller.mainStatusAt), encoderAge = age(time, t.encoderAt), speedAge = age(time, t.speedAt);
    const blockers = [];
    const add = (code, message) => blockers.push({ code, message });
    if (!s.connected) add('BLE_DISCONNECTED', '尚未连接蓝牙：点右上角“连接蓝牙”选择 CRTC-Lower-Car');
    else {
      if (!s.session) add('NO_SESSION', '等待 ESP32 会话');
      if (mainAge === null || mainAge >= 500) add('STATUS_STALE', '主控状态缺失或已过期');
      if (!s.uart) add('UART_OFFLINE', 'ESP32 与 STM32 的 UART 未连通');
      if (s.calibratedMask !== 15) add('CALIBRATION_REQUIRED', `未确认：${WHEELS.filter((_, i) => !(s.calibratedMask & (1 << i))).join('、')}`);
      if (!s.pidReady) add('PID_NOT_READY', '统一 PID 尚未由 STM32 确认');
      if (externalMotion()) add('MOTION_OWNED', '用户往复测试占用控制，请先停止测试');
      if (!s.ready) add('NOT_READY', '底盘闭环尚未就绪');
      if (s.fault || s.faultCode) add('FAULT', `底盘故障码 ${s.faultCode}，原因 ${s.reason}`);
      if (s.active) add('ACTIVE', '底盘仍处于运动状态，请先停止');
      if (s.configBusy) add('CONFIG_BUSY', '等待上一条配置/舵机命令回执');
      if (s.armPending) add('ARM_PENDING', '正在等待解锁回执');
      if (s.remoteArmed && !s.armed) add('REMOTE_ARMED', '主控解锁未被本页面确认');
    }
    let form = null, formError = null;
    try { form = calibration(); } catch (error) { formError = error.message; }
    return clone({ version: AGENT_VERSION, commands: AGENT_COMMANDS,
      state: s, canArm: controller.canArm() && !s.armPending && !s.armed,
      blockers, permissions: permissions(), foreground: foreground(), busy,
      motion: lease ? { remainingMs: Math.max(0, lease.until - time), throttle: lease.throttle, steer: lease.steer, limitMmS: lease.limitMmS } : null,
      feedback: { mainAgeMs: mainAge, encoderAgeMs: encoderAge, speedAgeMs: speedAge,
        encoder: t.encoder ?? null, speeds: t.speed ?? null, servo: controller.servoStatus,
        servoMotion: controller.servoMotion,
        servoJob: controller.servoOwner ? {channel:controller.servoOwner.channel,target:controller.servoOwner.target} : null,
        servoAgeMs: age(time, controller.servoStatusAt) },
      calibrationForm: form, calibrationFormError: formError, audit: audit.slice(-20),
    });
  }

  function record(command, ok, message) {
    audit.push({ atMs: Math.round(now()), command, ok, message });
    if (audit.length > 20) audit.shift();
    log(`Agent ${command}: ${message}`, !ok);
  }

  // Used by manual STOP, disconnect, blur and session changes. No recursive STOP.
  function onLock() { epoch++; lease = null; }

  async function stop(note = 'Agent 停止并锁定') {
    onLock(); clearInput(); paintInput(0, 0);
    await controller.stop(note);
  }

  function stoppedFeedback() {
    const t = telemetry(), time = now();
    if (!t.speed || age(time, t.speedAt) === null || age(time, t.speedAt) >= 200 || t.speed.validMask !== 15)
      fail('FEEDBACK_STALE', '需要四轮有效且不足 200ms 的轮速反馈');
    if (t.speed.measured.some(v => v !== 0) || t.speed.targets.some(v => v !== 0))
      fail('NOT_STOPPED', '请等待四轮目标与实测速度全部归零');
  }

  async function execute(request) {
    const command = request?.command;
    let ownsBusy = false;
    try {
      if (!request || Array.isArray(request) || typeof request !== 'object' || !AGENT_COMMANDS.includes(command))
        fail('UNKNOWN_COMMAND', '未知命令；先执行 {"command":"status"} 查看接口');
      const fields = { status: [], stop: [], led: ['on'], configure: [], clearFault: [], arm: [],
        drive: ['throttle', 'steer', 'limitMmS', 'durationMs'], servo: ['channel', 'pulseUs', 'referenceUs'] };
      if (Object.keys(request).some(key => key !== 'command' && !fields[command].includes(key)))
        fail('INVALID_ARGUMENT', '命令包含未知字段');
      if (command === 'status') return { ok: true, status: snapshot() };
      if (command === 'stop') { await stop(); record(command, true, '已请求停止；是否停稳以反馈为准'); return { ok: true, status: snapshot() }; }
      if (externalMotion() && (command === 'arm' || command === 'drive')) fail('MOTION_OWNED', '用户往复测试占用控制，请先停止测试');
      if (!permissions().write) fail('WRITE_DISABLED', '请先在页面开启“允许 Agent 控制”');
      if (!foreground()) fail('PAGE_INACTIVE', '页面必须保持可见且有焦点');
      if (busy || lease) fail('BUSY', '上一条命令或短测尚未结束；可随时执行 stop');
      if (!controller.state.connected || !controller.state.session) fail('BLE_DISCONNECTED', '蓝牙尚未连接或没有有效会话');
      busy = ownsBusy = true;
      const startedEpoch = epoch;
      if (command === 'led') {
        if (typeof request.on !== 'boolean') fail('INVALID_ARGUMENT', 'on 必须为 true 或 false');
        await controller.led(request.on);
      } else if (command === 'configure') {
        stoppedFeedback();
        const values = calibration();
        if (!values || values.length !== 4 || values.some(v => !v.confirmed))
          fail('CALIBRATION_UNCONFIRMED', '只允许写入表单内已经逐轮实测确认的配置；Agent 不代勾确认');
        await controller.configure(values);
      } else if (command === 'clearFault') {
        stoppedFeedback(); await controller.clearFault();
      } else if (command === 'arm') {
        if (!permissions().motion) fail('MOTION_DISABLED', '请单独开启“允许 Agent 解锁和短时运动”');
        stoppedFeedback(); await controller.arm();
      } else if (command === 'drive') {
        if (!permissions().motion) fail('MOTION_DISABLED', '请单独开启“允许 Agent 解锁和短时运动”');
        const throttle = integer(request.throttle, -1000, 1000, 'throttle');
        const steer = integer(request.steer, -1000, 1000, 'steer');
        const limitMmS = integer(request.limitMmS, 1, 100, 'limitMmS');
        const durationMs = integer(request.durationMs, 50, 1000, 'durationMs');
        if (!throttle && !steer) fail('INVALID_ARGUMENT', '零意图请使用 stop');
        if (!controller.state.armed || controller.state.armPending) fail('NOT_ARMED', '必须先解锁并收到主控确认；drive 不会自动解锁');
        if (!controller.state.uart || !controller.state.ready || controller.state.fault || controller.state.faultCode ||
            controller.state.calibratedMask !== 15 || age(now(), controller.mainStatusAt) === null || age(now(), controller.mainStatusAt) >= 500)
          fail('NOT_READY', '主控状态无效或底盘未就绪');
        stoppedFeedback(); clearInput();
        controller.setLimit(limitMmS);
        if (!controller.setIntent(throttle, steer)) fail('NOT_ARMED', '解锁已失效');
        lease = { until: now() + durationMs, session: controller.state.session, generation: controller.generation, throttle, steer, limitMmS };
        paintInput(throttle, steer);
      } else if (command === 'servo') {
        integer(request.channel, 0, 2, 'channel');
        if (request.pulseUs !== 0) integer(request.pulseUs, request.channel===2?1000:1300, request.channel===2?2000:1700, 'pulseUs');
        const reference=request.referenceUs??0;
        if(reference) integer(reference,request.channel===2?1000:1300,request.channel===2?2000:1700,'referenceUs');
        if(!request.pulseUs && reference) fail('INVALID_ARGUMENT','释放时不能设置起点');
        if(request.pulseUs) stoppedFeedback();
        await moveServo(request.channel, request.pulseUs, reference);
      }
      if (startedEpoch !== epoch) fail('CANCELLED', '操作期间发生 STOP、断连或会话变化；结果不可沿用');
      record(command, true, command === 'drive' ? '已启动有限时短测；到期自动停止锁定' : '请求已提交；以状态/固件回执为准');
      busy = ownsBusy = false;
      return { ok: true, status: snapshot() };
    } catch (error) {
      const message = error?.message || String(error);
      record(String(command), false, message);
      if (ownsBusy) busy = ownsBusy = false;
      return { ok: false, error: { code: error.code || 'REJECTED', message }, status: snapshot() };
    } finally { if (ownsBusy) busy = false; }
  }

  // Run BEFORE controller.tick(): an expired lease must never renew a DRIVE.
  function tick() {
    if (!lease) return;
    const s = controller.state, p = permissions();
    const expired = now() >= lease.until;
    if (expired || !p.write || !p.motion || !foreground() || !s.connected || !s.armed || s.fault ||
        s.session !== lease.session || controller.generation !== lease.generation ||
        controller.state.limit !== lease.limitMmS || controller.throttle !== lease.throttle || controller.steer !== lease.steer) {
      stop(expired ? 'Agent 短测到期，已停止并锁定' : 'Agent 短测中断，已停止并锁定').catch(error => log(error.message, true));
    }
  }
  return Object.freeze({ snapshot, execute, tick, onLock, isDriving: () => !!lease });
}

export function registerAgentTools(modelContext, agent) {
  if (!modelContext?.registerTool) return false;
  const result = value => ({ content: [{ type: 'text', text: JSON.stringify(value) }] });
  modelContext.registerTool({ name: 'crtc_dashboard_status', description: '只读：底盘连接、校准、反馈和解锁阻塞原因。不会让车运动。',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false }, annotations: { readOnlyHint: true },
    execute: async () => result({ ok: true, status: agent.snapshot() }) });
  modelContext.registerTool({ name: 'crtc_dashboard_command', description: '通过现有安全门禁执行底盘命令。除 status/stop 外需页面许可；不得代替用户实测确认校准。drive 最多100mm/s、1000ms且需已解锁，到期停止锁定。',
    inputSchema: { type: 'object', properties: {
      command: { type: 'string', enum: AGENT_COMMANDS }, on: { type: 'boolean' },
      throttle: { type: 'integer', minimum: -1000, maximum: 1000 }, steer: { type: 'integer', minimum: -1000, maximum: 1000 },
      limitMmS: { type: 'integer', minimum: 1, maximum: 100 }, durationMs: { type: 'integer', minimum: 50, maximum: 1000 },
      referenceUs: { type: 'integer', minimum: 0, maximum: 2000 },
      channel: { type: 'integer', minimum: 0, maximum: 2 }, pulseUs: { type: 'integer', minimum: 0, maximum: 2000 },
    }, required: ['command'], additionalProperties: false },
    execute: async request => result(await agent.execute(request)) });
  return true;
}
