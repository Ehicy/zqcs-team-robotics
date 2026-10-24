/* Wire codec and transport-independent safety state. No DOM or BLE globals. */
export const UUID = Object.freeze({
  service: '2c4b0001-6f1a-4c9e-9b72-3c06f42b7d11',
  control: '2c4b0002-6f1a-4c9e-9b72-3c06f42b7d11',
  status: '2c4b0003-6f1a-4c9e-9b72-3c06f42b7d11',
});
export const OP = Object.freeze({ ARM: 1, DRIVE: 2, STOP: 3, LED: 4, MECHANICAL: 5, TUNING: 6, CLEAR_FAULT: 7, SERVO: 8, PID: 9, SERVO_TARGET: 10, PWM: 11, ENCODER_RESET: 12, SERVO_SETTINGS: 13 });
export const WHEELS = Object.freeze(['M1 右前', 'M2 左前', 'M3 右后', 'M4 左后']);
export const servoBounds=(channel,status)=>status?.servoFullRange||channel===0&&status?.armFullRange?[500,2500]:channel===2?[1000,2000]:[1300,1700];
export const DEFAULT_CALIBRATION = Object.freeze(WHEELS.map((_, wheel) => Object.freeze({
  wheel, motorNegative: wheel % 2 === 1, encoderNegative: wheel % 2 === 0, confirmed: false,
  diameterTenthMm: 650, countsPerRev: wheel === 3 ? 884 : 1768,
  kpMilli: 1000, kiMilli: 2000, kdMilli: 0, maxMmS: 300,
})));
// Current PA-side chassis reserves TIM2 for PWM: M1/M2 x4, M3/M4 x2.
// These remain unconfirmed estimates until each output wheel is measured.
export const PA_CHASSIS_CALIBRATION = Object.freeze(DEFAULT_CALIBRATION.map((value, wheel) => Object.freeze({
  ...value, motorNegative:wheel===1, countsPerRev: wheel >= 2 ? 884 : 1768, confirmed: false,
})));

function integer(value, min, max, name) {
  if (!Number.isInteger(value) || value < min || value > max) throw new RangeError(`${name} 应为 ${min}–${max} 的整数`);
  return value;
}
function bit(value, name) {
  if (typeof value !== 'boolean') throw new TypeError(`${name} 应为布尔值`);
  return value;
}
function viewOf(bytes) {
  if (bytes instanceof DataView) return bytes;
  if (bytes instanceof ArrayBuffer) return new DataView(bytes);
  if (ArrayBuffer.isView(bytes)) return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  throw new TypeError('需要字节数组');
}
export function validateCalibration(value) {
  const item = {
    wheel: integer(value.wheel, 0, 3, '轮号'),
    motorNegative: bit(value.motorNegative, '电机方向'),
    encoderNegative: bit(value.encoderNegative, '编码器方向'),
    confirmed: bit(value.confirmed, '实测确认'),
    diameterTenthMm: integer(value.diameterTenthMm, 1, 5000, '轮径 0.1mm'),
    countsPerRev: integer(value.countsPerRev, 1, 1000000, '每轮每圈计数'),
    kpMilli: integer(value.kpMilli, 0, 20000, 'Kp_milli'),
    kiMilli: integer(value.kiMilli, 0, 20000, 'Ki_milli'),
    kdMilli: integer(value.kdMilli ?? 0, 0, 20000, 'Kd_milli'),
    maxMmS: integer(value.maxMmS, 1, 1000, '最大轮速'),
  };
  if (item.kpMilli === 0 && item.kiMilli === 0) throw new RangeError('Kp 与 Ki 不能同时为 0');
  return item;
}
export function encodeCommand(op, args) {
  integer(op, 1, 14, '操作码');
  const bytes = new Uint8Array(20), v = new DataView(bytes.buffer);
  bytes[0] = 1; bytes[1] = op;
  v.setUint32(4, integer(args.session, 1, 0xffffffff, '会话'), true);
  v.setUint32(8, integer(args.sequence, 0, 0xffffffff, '序号'), true);
  if (op === OP.DRIVE || op === OP.PWM) {
    v.setInt16(12, integer(args.throttle, -1000, 1000, '前后量'), true);
    v.setInt16(14, integer(args.steer, -1000, 1000, '转向量'), true);
    v.setUint16(16, integer(args.limit, 0, 1000, '速度上限'), true);
    v.setUint16(18, integer(args.ttl ?? 200, 50, 200, '有效期'), true);
  } else if (op === OP.SERVO || op === OP.SERVO_TARGET) {
    bytes[12] = integer(args.channel, 0, 2, '舵机通道');
    v.setUint16(14, args.pulseUs === 0 ? 0 : integer(args.pulseUs,
      500, 2500, '脉宽 μs'), true);
    if (op === OP.SERVO_TARGET) {
      const referenceUs = args.referenceUs ?? 0;
      if (!args.pulseUs && referenceUs) throw new RangeError('释放时不能设置起点');
      v.setUint16(16, referenceUs === 0 ? 0 : integer(referenceUs,
        500, 2500, '起点参考 μs'), true);
    }
  } else if (op === OP.SERVO_SETTINGS) {
    const channel=integer(args.channel,0,2,'舵机通道'), [lo,hi]=servoBounds(channel,{armFullRange:true,servoFullRange:true});
    bytes[12]=channel; bytes[13]=(args.calibrated?1:0)|(args.reversed?2:0);
    if(channel!==2&&args.reversed)throw new Error('仅门端点支持交换');
    const min=integer(args.minUs,lo,hi,'机械下限'),max=integer(args.maxUs,lo,hi,'机械上限');
    if(min>=max)throw new Error('机械下限必须小于上限');
    v.setUint16(14,min,true);v.setUint16(16,max,true);
    v.setUint16(18,integer(args.referenceUs,min,max,'参考起点'),true);
  } else if (op === OP.LED) {
    bytes[12] = integer(args.on, 0, 1, '灯状态');
  } else if (op === OP.MECHANICAL) {
    bytes[12] = integer(args.wheel, 0, 3, '轮号');
    bytes[13] = Number(bit(args.motorNegative, '电机方向')) |
      (Number(bit(args.encoderNegative, '编码器方向')) << 1) | (Number(bit(args.confirmed, '实测确认')) << 2);
    v.setUint16(14, integer(args.diameterTenthMm, 1, 5000, '轮径'), true);
    v.setUint32(16, integer(args.countsPerRev, 1, 1000000, '每圈计数'), true);
  } else if (op === OP.PID) {
    v.setUint16(14, integer(args.kpMilli, 0, 20000, 'Kp_milli'), true);
    v.setUint16(16, integer(args.kiMilli, 0, 20000, 'Ki_milli'), true);
    v.setUint16(18, integer(args.kdMilli, 0, 20000, 'Kd_milli'), true);
    if (!args.kpMilli && !args.kiMilli) throw new RangeError('Kp 与 Ki 不能同时为 0');
  } else if (op === OP.TUNING) {
    bytes[12] = integer(args.wheel, 0, 3, '轮号');
    v.setUint16(14, integer(args.kpMilli, 0, 20000, 'Kp_milli'), true);
    v.setUint16(16, integer(args.kiMilli, 0, 20000, 'Ki_milli'), true);
    if (!args.kpMilli && !args.kiMilli) throw new RangeError('Kp 与 Ki 不能同时为 0');
    v.setUint16(18, integer(args.maxMmS, 1, 1000, '最大轮速'), true);
  }
  return bytes;
}
export function decodeStatus(bytes) {
  const v = viewOf(bytes);
  if (v.byteLength !== 20 || v.getUint8(0) !== 1) throw new Error('状态包长度或版本不符');
  const kind = v.getUint8(1), flags = v.getUint8(2), extra = v.getUint8(3);
  if (kind === 0) {
    if (flags & ~127 || (extra >> 4) > 6 || v.getUint8(18) & ~15 || v.getUint16(16, true) > 1000) throw new Error('主状态字段越界');
    return { kind, flags, reason: extra & 15, phase: extra >> 4, pidReady: !!(flags & 32), session: v.getUint32(4, true), lastSequence: v.getUint32(8, true),
      uart: !!(flags & 1), armed: !!(flags & 2), active: !!(flags & 4), ready: !!(flags & 8), fault: !!(flags & 16),
      left: v.getInt16(12, true), right: v.getInt16(14, true), limit: v.getUint16(16, true),
      calibratedMask: v.getUint8(18), faultCode: v.getUint8(19), pwmSupported: !!(flags & 64) };
  }
  if (kind === 1) {
    if (flags & ~15 || extra !== 0) throw new Error('编码器状态保留位无效');
    return { kind, validMask: flags, counts: [0, 1, 2, 3].map(i => v.getInt32(4 + 4 * i, true)) };
  }
  if (kind === 2) {
    if (flags & ~15) throw new Error('轮速有效位无效');
    return { kind, validMask: flags, faultCode: extra,
      measured: [0, 1, 2, 3].map(i => v.getInt16(4 + 2 * i, true)),
      targets: [0, 1, 2, 3].map(i => v.getInt16(12 + 2 * i, true)) };
  }
  if (kind === 3) {
    if (flags > 3 && flags !== 252 && flags !== 253 && flags !== 254 && flags !== 255) throw new Error('配置确认轮号无效');
    for (let i = 12; i < 20; i++) if (v.getUint8(i)) throw new Error('配置确认保留位无效');
    return { kind, wheel: flags, result: extra, session: v.getUint32(4, true), sourceSequence: v.getUint32(8, true) };
  }
  if (kind === 4) {
    const pulses = [v.getUint16(8, true), v.getUint16(10, true)];
    if (flags & ~3 || extra & ~7 || pulses.some((p,i) => p !== 0 && (p < 1300 || p > 1700) || !!p !== !!(flags & (1<<i))) ||
        v.getUint16(12,true) !== 1300 || v.getUint16(14,true) !== 1700 ||
        v.getUint16(16,true) !== 25 || v.getUint16(18,true) !== 100) throw new Error('舵机状态字段无效');
    return { kind, session: v.getUint32(4,true), pulses, available: !!(extra & 1), positionMask: (extra>>1)&3 };
  }
  if (kind === 5) {
    const pulses = [8,10,12].map(offset => v.getUint16(offset,true));
    if (flags & ~7 || pulses.some((p,i) =>
        p !== 0 && (p < (i===0&&!(extra&16)?1300:500) || p > (i===0&&!(extra&16)?1700:2500)) || !!p !== !!(flags & (1<<i))) ||
        v.getUint16(14,true)!==1000 || v.getUint16(16,true)!==2000 || v.getUint16(18,true)!==25)
      throw new Error('三舵机状态字段无效');
    return { kind, session:v.getUint32(4,true), pulses, available:!!(extra&1), positionMask:(extra>>1)&7, slew:!!(extra&128), armFullRange:!!(extra&16), quickProfile:!!(extra&32), encoderReset:!!(extra&64) };
  }
  if (kind === 6) {
    const targets = [8,10,12].map(offset => v.getUint16(offset,true));
    if (flags & ~7 || extra & ~7 || targets.some((p,i) => p && (p<500 || p>2500) || !p && (flags&(1<<i))) ||
        !((v.getUint16(14,true)===100&&v.getUint16(16,true)===200)||
          (v.getUint16(14,true)===600&&v.getUint16(16,true)===1200)||
          (v.getUint16(14,true)===1000&&[2500,4000].includes(v.getUint16(16,true)))) || v.getUint16(18,true)!==500)
      throw new Error('舵机轨迹状态字段无效');
    return {kind, session:v.getUint32(4,true), targets, movingMask:flags, errorMask:extra,
      speedUsS:v.getUint16(14,true), accelerationUsS2:v.getUint16(16,true), leaseMs:500};
  }
  if(kind===7){
    if(flags>3||extra>7)throw new Error('轮设置字段无效');
    return {kind,wheel:flags,session:v.getUint32(4,true),revision:integer(v.getUint16(18,true),1,65535,'修订'),
      motorNegative:!!(extra&1),encoderNegative:!!(extra&2),confirmed:!!(extra&4),
      countsPerRev:integer(v.getUint32(8,true),1,1000000,'计数'),diameterTenthMm:integer(v.getUint16(12,true),1,5000,'轮径'),
      maxMmS:integer(v.getUint16(14,true),1,1000,'轮速'),accelMmS2:integer(v.getUint16(16,true),1,5000,'加速度')};
  }
  if(kind===8){
    if(flags>3||extra>31||v.getUint16(14,true)!==200||v.getUint16(16,true)!==50)throw new Error('统一设置字段无效');
    const kpMilli=integer(v.getUint16(8,true),0,20000,'Kp'),kiMilli=integer(v.getUint16(10,true),0,20000,'Ki');
    if(!(kpMilli||kiMilli))throw new Error('PID全零');
    return {kind,session:v.getUint32(4,true),revision:integer(v.getUint16(18,true),1,65535,'修订'),
      loaded:!!(flags&1),synced:!!(flags&2),servoFullRange:!!(extra&1),concurrentDrive:!!(extra&16),kpMilli,kiMilli,kdMilli:integer(v.getUint16(12,true),0,20000,'Kd')};
  }
  if(kind===9){
    if(flags>2||extra>3||flags!==2&&(extra&2))throw new Error('舵机设置字段无效');
    const [lo,hi]=servoBounds(flags,{armFullRange:true,servoFullRange:true}),minUs=integer(v.getUint16(8,true),lo,hi,'机械下限'),maxUs=integer(v.getUint16(10,true),lo,hi,'机械上限');
    if(minUs>=maxUs)throw new Error('舵机机械范围无效');
    const doorClosed=integer(v.getUint16(14,true),500,2500,'关门'),doorOpen=integer(v.getUint16(16,true),500,2500,'开门');
    if(doorClosed===doorOpen)throw new Error('门端点相同');
    return {kind,channel:flags,calibrated:!!(extra&1),reversed:!!(extra&2),session:v.getUint32(4,true),
      revision:integer(v.getUint16(18,true),1,65535,'修订'),minUs,maxUs,referenceUs:integer(v.getUint16(12,true),minUs,maxUs,'起点'),doorClosed,doorOpen};
  }
  throw new Error('未知状态类型');
}

// Accumulate a complete, same-session/revision snapshot. Mixed revisions can
// never overwrite a form or become the source of a subsequent write.
export class DeviceSettings {
  constructor(){this.reset();}
  reset(){this.session=0;this.revision=0;this.wheels=[];this.servos=[];this.pid=null;}
  accept(p,session){
    if(p.session!==session||![7,8,9].includes(p.kind))return false;
    if(this.session!==session||this.revision!==p.revision){
      if(this.session===session&&this.revision&&((p.revision-this.revision)&65535)>=32768)return false;
      this.reset();this.session=session;this.revision=p.revision;
    }
    if(p.kind===7)this.wheels[p.wheel]=p;
    if(p.kind===8)this.pid=p;
    if(p.kind===9)this.servos[p.channel]=p;
    return true;
  }
  calibration(){return this.pid&&[0,1,2,3].every(i=>this.wheels[i])?this.wheels.map(w=>validateCalibration({...w,...this.pid,wheel:w.wheel})):null;}
}

function rejection(message) { return new Error(message); }
/* One write-with-response in flight; a single replaceable DRIVE slot. STOP
 * clears all queued work. A write already handed to the BLE stack cannot be
 * recalled; STOP is sent immediately after that write settles. */
export class WriteQueue {
  constructor({ now = () => performance.now(), onError = () => {} } = {}) {
    this.now = now; this.onError = onError; this.writer = null; this.generation = 0;
    this.flight = null; this.stop = null; this.actions = []; this.drive = null; this.order = 0;
  }
  setWriter(writer) {
    this.cancelPending('连接已改变');
    this.generation++; this.writer = writer; this.flight = null;
  }
  cancelPending(reason = '待发命令已取消') {
    for (const item of [this.stop, ...this.actions, this.drive]) if (item) item.reject(rejection(reason));
    this.stop = null; this.actions = []; this.drive = null;
  }
  offer(bytes, { kind = 'action', expiresAt = this.now() + 500 } = {}) {
    if (!this.writer) return Promise.reject(rejection('蓝牙未连接'));
    const generation = this.generation;
    return new Promise((resolve, reject) => {
      const item = { bytes, kind, expiresAt, resolve, reject, generation, order: ++this.order };
      if (kind === 'stop') {
        this.cancelPending('停止已清除待发命令'); this.stop = item;
      } else if (kind === 'drive') {
        if (this.stop) { reject(rejection('正在停止')); return; }
        if (this.drive) this.drive.reject(rejection('旧运动采样已替换'));
        this.drive = item;
      } else {
        if (this.stop || this.actions.length >= 8) { reject(rejection('控制队列忙')); return; }
        this.actions.push(item);
      }
      this.pump();
    });
  }
  async pump() {
    if (this.flight || !this.writer) return;
    let item;
    for (;;) {
      if (this.stop) { item = this.stop; this.stop = null; }
      // Preserve sequence order across ordinary actions and DRIVE. Prioritizing
      // an LED action over an older DRIVE would send descending source seqs.
      else if (this.actions.length && (!this.drive || this.actions[0].order < this.drive.order)) item = this.actions.shift();
      else { item = this.drive; this.drive = null; }
      if (!item) return;
      if (this.now() < item.expiresAt) break;
      item.reject(rejection('待发命令已过期'));
    }
    const writer = this.writer;
    this.flight = item;
    try {
      await writer(item.bytes);
      if (item.generation === this.generation) item.resolve();
      else item.reject(rejection('旧连接写入已丢弃'));
    } catch (error) {
      item.reject(error);
      if (item.generation === this.generation) this.onError(error);
    } finally {
      if (this.flight === item) { this.flight = null; this.pump(); }
    }
  }
}

export class AckRegistry {
  constructor(now = () => performance.now()) { this.now = now; this.pending = new Map(); }
  expect(session, sequence, wheel, timeout = 1500) {
    if (this.pending.has(sequence)) throw new Error('确认序号重复');
    return new Promise((resolve, reject) => this.pending.set(sequence,
      { session, wheel, deadline: this.now() + timeout, resolve, reject }));
  }
  accept(packet) {
    if (packet.kind !== 3) return false;
    const wait = this.pending.get(packet.sourceSequence);
    if (!wait || wait.session !== packet.session || wait.wheel !== packet.wheel) return false;
    this.pending.delete(packet.sourceSequence);
    if (this.now() >= wait.deadline) wait.reject(rejection('配置确认超时'));
    else if (packet.result) wait.reject(rejection(packet.result===7?'C3 Flash 写入失败，参数未应用':`${packet.wheel === 254 ? "ESP32 拒绝舵机操作" : "设备拒绝配置"}，代码 ${packet.result}`));
    else wait.resolve(packet);
    return true;
  }
  expire() {
    for (const [sequence, wait] of this.pending) if (this.now() >= wait.deadline) {
      this.pending.delete(sequence); wait.reject(rejection('配置确认超时'));
    }
  }
  clear(reason) { for (const wait of this.pending.values()) wait.reject(rejection(reason)); this.pending.clear(); }
}
function atLeast(candidate, reference) { return ((candidate - reference) >>> 0) < 0x80000000; }

export class RemoteController {
  constructor({ now = () => performance.now(), changed = () => {}, locked = () => {}, transportError = () => {}, deviceStorage=false, ackTimeoutMs=1500 } = {}) {
    this.now = now; this.changed = changed; this.locked = locked; this.transportError = transportError;
    this.queue = new WriteQueue({ now, onError: error => { this.disconnect(); transportError(error); } });
    this.acks = new AckRegistry(now); this.sequence = 0; this.generation = 0;
    this.stopGeneration = 0; this.servoOwner = null;
    this.deviceSettings=new DeviceSettings();
    this.requireDeviceStorage=deviceStorage;this.ackTimeoutMs=ackTimeoutMs;
    this.state = { connected: false, session: 0, armed: false, armPending: false, remoteArmed: false,
      uart: false, ready: false, pidReady: false, phase: 0, active: false, fault: false, faultCode: 0, reason: 0,
      calibratedMask: 0, left: 0, right: 0, limit: 100, configBusy: false, note: '尚未连接' };
    this.throttle = 0; this.steer = 0; this.lastTick = now(); this.lastDrive = -Infinity;
    this.servoMotion = null; this.servoMotionAt = -Infinity;
    this.servoStatus = null; this.servoStatusAt = -Infinity; this.appliedCalibration = null;
    this.mainStatusAt = -Infinity; this.armSequence = null; this.unsolicitedArmedStopped = false;
    this.armRequestedAt = -Infinity;
  }
  notify() { this.changed({ ...this.state }); }
  attach(writer) {
    this.disconnect(); this.queue.setWriter(writer); this.state.connected = true;
    this.state.note = '已连接，等待主控状态'; this.lastTick = this.now(); this.notify();
    return this.generation;
  }
  disconnect() {
    this.deviceSettings?.reset();
    this.servoMotion = null; this.servoMotionAt = -Infinity;
    this.servoStatus = null; this.servoStatusAt = -Infinity; this.appliedCalibration = null;
    this.generation++; this.queue.setWriter(null); this.acks.clear('连接已断开');
    Object.assign(this.state, { connected: false, session: 0, armed: false, armPending: false,
      remoteArmed: false, uart: false, ready: false, pidReady: false, phase: 0, active: false, calibratedMask: 0, note: '连接已断开，保持锁定' });
    this.throttle = this.steer = 0; this.armSequence = null; this.mainStatusAt = -Infinity;
    this.locked({recoverable:true,reason:'蓝牙连接中断'}); this.notify();
  }
  nextSequence() { this.sequence = (this.sequence + 1) >>> 0; return this.sequence; }
  packet(op, args = {}) {
    const sequence = this.nextSequence();
    return { bytes: encodeCommand(op, { ...args, session: this.state.session, sequence }), sequence };
  }
  lockLocally(note, event = {}) {
    this.stopGeneration++;
    this.throttle = this.steer = 0; this.armSequence = null;
    this.state.armed = false; this.state.armPending = false; this.state.note = note;
    this.acks.clear(note); this.queue.cancelPending(note); this.locked({reason:note,...event}); this.notify();
  }
  stop(note = '已停止并锁定') {
    this.lockLocally(note);
    if (!this.state.connected || !this.state.session) return Promise.resolve();
    return this.queue.offer(this.packet(OP.STOP).bytes, { kind: 'stop', expiresAt: this.now() + 500 });
  }
  concurrentMotion() {
    const p=this.deviceSettings.pid;
    return !!(p?.concurrentDrive && p.session===this.state.session && this.state.session);
  }
  stopDrive(note = '底盘已停止') {
    if(!this.concurrentMotion()) return this.stop(note);
    this.state.note=note;
    // A zero PWM command replaces pending drive samples without clearing servo
    // ACKs/owners. The C3 converts it to the STM32 quick-stop frame.
    if(!this.state.connected||!this.state.session) return Promise.resolve();
    return this.queue.offer(this.packet(OP.PWM,{throttle:0,steer:0,limit:1000,ttl:200}).bytes,
      {kind:'drive',expiresAt:this.now()+100});
  }
  canArm() {
    return this.state.connected && !!this.state.session && this.state.uart && this.state.ready && this.state.pidReady &&
      !this.servoOwner && !this.servoMotion?.movingMask && !this.state.fault && !this.state.active && !this.state.remoteArmed && !this.state.configBusy &&
      this.state.calibratedMask === 15 && this.now() - this.mainStatusAt < 500;
  }
  async arm() {
    if (!this.canArm()) throw new Error('需要 UART 在线、统一 PID 初始化已确认、四轮已确认且无故障，停稳后才能解锁');
    const packet = this.packet(OP.ARM); this.armSequence = packet.sequence;
    this.armRequestedAt = this.now();
    this.state.armPending = true; this.state.note = '等待主控确认解锁'; this.notify();
    try { await this.queue.offer(packet.bytes, { expiresAt: this.now() + 300 }); }
    catch (error) { this.state.armPending = false; this.armSequence = null; this.notify(); throw error; }
  }
  setIntent(throttle, steer) {
    integer(throttle, -1000, 1000, '前后量'); integer(steer, -1000, 1000, '转向量');
    if (!this.state.armed) return false;
    this.throttle = throttle; this.steer = steer;
    if (throttle === 0 && steer === 0) this.sendDrive();
    return true;
  }
  setLimit(limit) { this.state.limit = integer(limit, 0, 1000, '速度上限'); this.notify(); }
  sendDrive() {
    if (!this.state.armed || !this.state.connected) return;
    const packet = this.packet(OP.DRIVE, { throttle: this.throttle, steer: this.steer, limit: this.state.limit, ttl: 200 });
    this.lastDrive = this.now();
    // Replaced/stale samples are intentionally discarded; transport errors use
    // queue.onError, which disconnects and locks this controller.
    this.queue.offer(packet.bytes, { kind: 'drive', expiresAt: this.now() + 100 }).catch(() => {});
  }
  tick() {
    const now = this.now(), gap = now - this.lastTick; this.lastTick = now; this.acks.expire();
    // This timer owns legacy ARM/DRIVE only. An idle page's previous tick is
    // not the age of a newly pressed PWM key; PwmDrive times its own held job.
    if (gap >= 150 && (this.state.armed || this.state.armPending)) {
      this.stop('页面调度中断，已停止并锁定').catch(() => {}); return;
    }
    if (this.state.connected && this.mainStatusAt !== -Infinity && now - this.mainStatusAt >= 500 &&
        (this.state.armed || this.state.armPending)) {
      this.stop('主控状态过期，已停止并锁定').catch(() => {}); return;
    }
    if (this.state.armPending && now - this.armRequestedAt >= 1000) {
      this.stop('解锁确认超时，保持锁定').catch(() => {}); return;
    }
    if (this.state.armed && now - this.lastDrive >= 50) this.sendDrive();
  }
  ingest(packet, generation) {
    if (generation !== this.generation || !this.state.connected) return false;
    if (packet.kind === 3) { this.acks.accept(packet); return true; }
    if([7,8,9].includes(packet.kind)){
      if(!this.deviceSettings.accept(packet,this.state.session))return false;
      const calibration=this.deviceSettings.calibration();
      if(calibration)this.appliedCalibration=calibration;
      if(this.servoStatus)this.servoStatus={...this.servoStatus,servoFullRange:!!this.deviceSettings.pid?.servoFullRange};
      this.notify();return true;
    }
    if (packet.kind === 6) {
      if (packet.session !== this.state.session) return false;
      this.servoMotion=packet; this.servoMotionAt=this.now(); this.notify(); return true;
    }
    if (packet.kind === 4 || packet.kind === 5) {
      if (packet.session !== this.state.session) return false;
      this.servoStatus = {...packet,servoFullRange:!!this.deviceSettings.pid?.servoFullRange}; this.servoStatusAt = this.now(); this.notify(); return true;
    }
    if (packet.kind !== 0) return !!this.state.session;
    if (!packet.session) return false;
    const changedSession = packet.session !== this.state.session;
    if (changedSession) {
      this.deviceSettings.reset();this.appliedCalibration=null;
      this.servoMotion = null; this.servoMotionAt = -Infinity;
      this.servoStatus = null; this.servoStatusAt = -Infinity;
      this.state.session = packet.session;
      this.unsolicitedArmedStopped = false;
      // Firmware rotates the session when STOP is accepted. Sending STOP in
      // response to every new session would cause an endless STOP/session loop.
      // 人工 STOP 不续跑。仅断链/超时会话变化允许已按下的
      // 一键舵机动作保留目标；底盘、路径复刻仍撤销，不自动驾驶。
      this.lockLocally('会话已更新，等待当前状态',{recoverable:![4,5,8].includes(packet.reason)});
    }
    this.mainStatusAt = this.now();
    Object.assign(this.state, { uart: packet.uart, ready: packet.ready, pidReady: packet.pidReady, phase: packet.phase, remoteArmed: packet.armed,
      active: packet.active, fault: packet.fault, faultCode: packet.faultCode, reason: packet.reason, pwmSupported: packet.pwmSupported,
      calibratedMask: packet.calibratedMask, left: packet.left, right: packet.right });
    if (packet.fault || !packet.uart || !packet.ready || !packet.pidReady || packet.calibratedMask !== 15) {
      if (this.state.armed || this.state.armPending) {
        this.unsolicitedArmedStopped = packet.armed;
        this.stop('底盘未就绪或故障，已锁定').catch(() => {});
      }
    } else if (packet.armed && this.state.armPending && this.armSequence !== null && atLeast(packet.lastSequence, this.armSequence)) {
      this.state.armed = true; this.state.armPending = false; this.state.note = '已解锁：按住方向行驶，松开归零';
      this.lastDrive = -Infinity;
    } else if (!packet.armed && this.state.armed) {
      this.lockLocally('主控已锁定，请重新解锁');
    }
    if (packet.armed && !this.state.armed && !this.state.armPending && !this.unsolicitedArmedStopped) {
      this.unsolicitedArmedStopped = true; this.stop('主控解锁状态未经本页确认，已发送停止').catch(() => {});
    }
    if (!packet.armed) this.unsolicitedArmedStopped = false;
    this.notify(); return true;
  }
  servoSettingsReady(channel) {
    const s=this.deviceSettings.servos[channel];
    return !this.requireDeviceStorage || !!(s && s.session===this.state.session && s.revision===this.deviceSettings.revision);
  }
  servoAvailable(channel, release = false) {
    return !this.servoUnavailableReason(channel,release);
  }
  servoUnavailableReason(channel, release = false) {
    // Keep enable/disable decisions and the visible explanation identical.
    // A command ACK is NOT telemetry and must never refresh these timestamps.
    const s = this.servoStatus;
    if(!Number.isInteger(channel)||channel<0||channel>2)return '舵机通道无效';
    if(!this.state.connected)return '蓝牙未连接';
    if(!this.state.session)return '等待主控会话';
    if(this.now()-this.mainStatusAt>=500)return '主控状态回传已超过500ms';
    if(!this.state.uart)return 'C3 与 STM32 串口未连通';
    if(!this.concurrentMotion()&&(this.state.armed||this.state.armPending||this.state.remoteArmed||this.state.active))return '当前 C3 固件要求底盘停稳；烧入并行版后可同时操作';
    if(this.state.configBusy)return '正在等待上一条指令确认';
    if(!s||s.session!==this.state.session||this.now()-this.servoStatusAt>=500)return '舵机 PWM 状态回传已超过500ms或正在等待新会话';
    if(channel>=s.pulses.length||!s.available)return '主控舵机输出尚未就绪';
    if(release)return '';
    if(!this.servoSettingsReady(channel))return '等待 C3 回读本路舵机参数';
    if(!s.slew)return '需要支持缓启动的舵机固件';
    if(!this.servoMotion||this.now()-this.servoMotionAt>=500)return '舵机轨迹状态回传已超过500ms';
    if(this.servoMotion.errorMask&(1<<channel))return '主控报告本路 PWM 输出异常';
    if(this.state.fault)return '主控报告底盘异常';
    if(!(s.positionMask&(1<<channel)))return '本路未配置为位置舵机';
    return '';
  }
  async servo(channel, pulseUs, owner = null, referenceUs = 0) {
    if (this.servoOwner && this.servoOwner !== owner) throw new Error('其他舵机操作正在运行，请先停止');
    // 2026-10-24：舵机目标不是底盘参数写入，不应等待四轮/PID整套回读。
    // 仅使用当前会话中该通道的C3限位；释放PWM不需要任何机械参数。
    if(!this.concurrentMotion())this.assertStopped(false,false);
    if(pulseUs&&!this.servoSettingsReady(channel))throw new Error('等待 C3 回读本路舵机参数');
    const unavailable=this.servoUnavailableReason(channel,pulseUs===0);
    if(unavailable)throw new Error(unavailable);
    const mechanical=this.deviceSettings.servos[channel];
    const [min,max]=mechanical?[mechanical.minUs,mechanical.maxUs]:servoBounds(channel,this.servoStatus);
    if(pulseUs&&(pulseUs<min||pulseUs>max)||referenceUs&&(referenceUs<min||referenceUs>max))throw new Error(`当前固件范围为${min}–${max}μs；完整270°需新版C3固件`);
    const old = this.servoStatus.pulses[channel];
    if (pulseUs && (!old && !referenceUs || old && referenceUs))
      throw new Error('PWM释放后需要起点参考；已启用时不能重设起点');
    this.state.configBusy = true; this.notify();
    try {
      await this.acknowledged(this.servoStatus.slew ? OP.SERVO_TARGET : OP.SERVO,
        { channel, pulseUs, referenceUs }, 254);
      // ACK accepts a TARGET. Only status5 reports the current PWM command.
      this.state.note = pulseUs ? '目标已接受，ESP32限速跟随；实际角度需观察' : 'PWM释放请求已确认，请支撑机构';
    } finally { this.state.configBusy = false; this.notify(); }
  }
  async led(on) {
    await this.queue.offer(this.packet(OP.LED, { on: on ? 1 : 0 }).bytes, { expiresAt: this.now() + 500 });
  }
  async acknowledged(op, args, wheel) {
    const packet = this.packet(op, args);
    const acknowledged = this.acks.expect(this.state.session, packet.sequence, wheel, this.ackTimeoutMs);
    try { await Promise.all([acknowledged, this.queue.offer(packet.bytes, { expiresAt: this.now() + 500 })]); }
    catch (error) { this.acks.clear(error.message); throw error; }
  }
  assertStopped(allowOffline=false, requireChassisSettings=true) {
    if(requireChassisSettings&&this.requireDeviceStorage&&!this.deviceSettings.calibration())throw new Error('等待 C3 完整参数回读，避免用网页初值覆盖设备');
    if (!this.state.connected || !this.state.session || !allowOffline&&!this.state.uart || this.state.armed ||
        this.state.armPending || this.state.remoteArmed || this.state.active || this.state.configBusy ||
        this.now() - this.mainStatusAt >= 500) throw new Error('请连接主控、停止锁定并等待停稳状态');
  }
  async configurePid(gains) {
    this.assertStopped(!!this.deviceSettings.pid); this.state.configBusy = true; this.notify();
    try {
      await this.acknowledged(OP.PID, gains, 253);
      if (this.appliedCalibration) this.appliedCalibration = this.appliedCalibration.map(v => ({ ...v, ...gains }));
      this.state.note = this.deviceSettings.pid?'统一 PID 已写入 C3 Flash，待回读；C3空闲时同步底盘':'统一 PID 已由 STM32 确认';
    } finally { this.state.configBusy = false; this.notify(); }
  }
  async configure(values) {
    this.assertStopped(!!this.deviceSettings.pid);
    if (!Array.isArray(values) || values.length !== 4) throw new Error('需要四轮配置');
    const items = values.map(validateCalibration);
    if (items.some((item, index) => item.wheel !== index)) throw new Error('配置必须按 M1–M4 顺序');
    if (items.some(item => ['kpMilli', 'kiMilli', 'kdMilli'].some(key => item[key] !== items[0][key])))
      throw new Error('四轮使用同一组 Kp/Ki/Kd，请在统一 PID 区调整');
    this.appliedCalibration = null;
    this.state.configBusy = true; this.notify();
    try {
      this.state.note = '正在下发统一 PID 参数'; this.notify();
      await this.acknowledged(OP.PID, items[0], 253);
      for (const item of items) {
        this.state.note = `正在写入 ${WHEELS[item.wheel]} 配置`; this.notify();
        await this.acknowledged(OP.MECHANICAL, item, item.wheel);
        await this.acknowledged(OP.TUNING, item, item.wheel);
      }
      this.appliedCalibration = items.map(v => ({ ...v }));
      this.state.note = this.deviceSettings.pid?'统一 PID 与四轮参数已写入 C3 Flash，待回读':'统一 PID 与四轮配置已确认，仅保存 RAM';
    } finally { this.state.configBusy = false; this.notify(); }
  }
  async clearFault() {
    this.assertStopped(); this.state.configBusy = true; this.notify();
    try { await this.acknowledged(OP.CLEAR_FAULT, {}, 255); this.state.note = '清故障请求已确认，请检查状态'; }
    finally { this.state.configBusy = false; this.notify(); }
  }
  async configureServo(channel,settings){
    this.assertStopped(true,false);
    if(!this.servoSettingsReady(channel))throw new Error('等待 C3 回读本路舵机参数');
    this.state.configBusy=true;this.notify();
    try{await this.acknowledged(OP.SERVO_SETTINGS,{channel,...settings},254);this.state.note='舵机设置已写 C3 Flash；未执行位置';}
    finally{this.state.configBusy=false;this.notify();}
  }
  async captureServoReference(channel){
    // Store the exact returned pulse, not the rounded degree label. This is
    // configuration only: never release PWM, issue a TARGET, or move to centre.
    this.assertStopped(true,false);
    const s=this.servoStatus,m=this.servoMotion,setting=this.deviceSettings.servos[channel];
    if(!Number.isInteger(channel)||channel<0||channel>2||!setting||!this.servoSettingsReady(channel))throw new Error('等待 C3 回读本路舵机参数');
    if(this.servoOwner||!s||s.session!==this.state.session||this.now()-this.servoStatusAt>=500||
       !m||m.session!==this.state.session||this.now()-this.servoMotionAt>=500||m.movingMask&(1<<channel))throw new Error('请等本路目标停止变化并收到最新 PWM，再记录初始态');
    const referenceUs=s.pulses[channel];
    if(!referenceUs)throw new Error('本路 PWM 尚未启用，不能把界面默认角度当作当前位置');
    await this.configureServo(channel,{minUs:setting.minUs,maxUs:setting.maxUs,referenceUs,
      calibrated:setting.calibrated,reversed:setting.reversed});
    return referenceUs;
  }
  async resetEncoders(){
    this.assertStopped();if(!this.servoStatus?.encoderReset)throw new Error('底盘固件不支持清零，需要更新 STM32');
    this.state.configBusy=true;this.notify();
    try{await this.acknowledged(OP.ENCODER_RESET,{},252);this.state.note='四轮编码器已清零';}
    finally{this.state.configBusy=false;this.notify();}
  }
}
