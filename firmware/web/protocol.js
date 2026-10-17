/* Wire codec and transport-independent safety state. No DOM or BLE globals. */
export const UUID = Object.freeze({
  service: '2c4b0001-6f1a-4c9e-9b72-3c06f42b7d11',
  control: '2c4b0002-6f1a-4c9e-9b72-3c06f42b7d11',
  status: '2c4b0003-6f1a-4c9e-9b72-3c06f42b7d11',
});
export const OP = Object.freeze({ ARM: 1, DRIVE: 2, STOP: 3, LED: 4, MECHANICAL: 5, TUNING: 6, CLEAR_FAULT: 7 });
export const WHEELS = Object.freeze(['M1 右前', 'M2 左前', 'M3 右后', 'M4 左后']);
export const DEFAULT_CALIBRATION = Object.freeze(WHEELS.map((_, wheel) => Object.freeze({
  wheel, motorNegative: wheel % 2 === 1, encoderNegative: wheel % 2 === 0, confirmed: false,
  diameterTenthMm: 650, countsPerRev: wheel === 3 ? 884 : 1768,
  kpMilli: 1000, kiMilli: 2000, maxMmS: 300,
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
    maxMmS: integer(value.maxMmS, 1, 300, '最大轮速'),
  };
  if (item.kpMilli === 0 && item.kiMilli === 0) throw new RangeError('Kp 与 Ki 不能同时为 0');
  return item;
}
export function encodeCommand(op, args) {
  integer(op, 1, 7, '操作码');
  const bytes = new Uint8Array(20), v = new DataView(bytes.buffer);
  bytes[0] = 1; bytes[1] = op;
  v.setUint32(4, integer(args.session, 1, 0xffffffff, '会话'), true);
  v.setUint32(8, integer(args.sequence, 0, 0xffffffff, '序号'), true);
  if (op === OP.DRIVE) {
    v.setInt16(12, integer(args.throttle, -1000, 1000, '前后量'), true);
    v.setInt16(14, integer(args.steer, -1000, 1000, '转向量'), true);
    v.setUint16(16, integer(args.limit, 0, 300, '速度上限'), true);
    v.setUint16(18, integer(args.ttl ?? 200, 50, 200, '有效期'), true);
  } else if (op === OP.LED) {
    bytes[12] = integer(args.on, 0, 1, '灯状态');
  } else if (op === OP.MECHANICAL) {
    bytes[12] = integer(args.wheel, 0, 3, '轮号');
    bytes[13] = Number(bit(args.motorNegative, '电机方向')) |
      (Number(bit(args.encoderNegative, '编码器方向')) << 1) | (Number(bit(args.confirmed, '实测确认')) << 2);
    v.setUint16(14, integer(args.diameterTenthMm, 1, 5000, '轮径'), true);
    v.setUint32(16, integer(args.countsPerRev, 1, 1000000, '每圈计数'), true);
  } else if (op === OP.TUNING) {
    bytes[12] = integer(args.wheel, 0, 3, '轮号');
    v.setUint16(14, integer(args.kpMilli, 0, 20000, 'Kp_milli'), true);
    v.setUint16(16, integer(args.kiMilli, 0, 20000, 'Ki_milli'), true);
    if (!args.kpMilli && !args.kiMilli) throw new RangeError('Kp 与 Ki 不能同时为 0');
    v.setUint16(18, integer(args.maxMmS, 1, 300, '最大轮速'), true);
  }
  return bytes;
}
export function decodeStatus(bytes) {
  const v = viewOf(bytes);
  if (v.byteLength !== 20 || v.getUint8(0) !== 1) throw new Error('状态包长度或版本不符');
  const kind = v.getUint8(1), flags = v.getUint8(2), extra = v.getUint8(3);
  if (kind === 0) {
    if (flags & ~31 || v.getUint8(18) & ~15 || v.getUint16(16, true) > 300) throw new Error('主状态字段越界');
    return { kind, flags, reason: extra, session: v.getUint32(4, true), lastSequence: v.getUint32(8, true),
      uart: !!(flags & 1), armed: !!(flags & 2), active: !!(flags & 4), ready: !!(flags & 8), fault: !!(flags & 16),
      left: v.getInt16(12, true), right: v.getInt16(14, true), limit: v.getUint16(16, true),
      calibratedMask: v.getUint8(18), faultCode: v.getUint8(19) };
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
    if (flags > 3 && flags !== 255) throw new Error('配置确认轮号无效');
    for (let i = 12; i < 20; i++) if (v.getUint8(i)) throw new Error('配置确认保留位无效');
    return { kind, wheel: flags, result: extra, session: v.getUint32(4, true), sourceSequence: v.getUint32(8, true) };
  }
  throw new Error('未知状态类型');
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
    else if (packet.result) wait.reject(rejection(`STM32 拒绝配置，代码 ${packet.result}`));
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
  constructor({ now = () => performance.now(), changed = () => {}, locked = () => {}, transportError = () => {} } = {}) {
    this.now = now; this.changed = changed; this.locked = locked; this.transportError = transportError;
    this.queue = new WriteQueue({ now, onError: error => { this.disconnect(); transportError(error); } });
    this.acks = new AckRegistry(now); this.sequence = 0; this.generation = 0;
    this.state = { connected: false, session: 0, armed: false, armPending: false, remoteArmed: false,
      uart: false, ready: false, active: false, fault: false, faultCode: 0, reason: 0,
      calibratedMask: 0, left: 0, right: 0, limit: 100, configBusy: false, note: '尚未连接' };
    this.throttle = 0; this.steer = 0; this.lastTick = now(); this.lastDrive = -Infinity;
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
    this.generation++; this.queue.setWriter(null); this.acks.clear('连接已断开');
    Object.assign(this.state, { connected: false, session: 0, armed: false, armPending: false,
      remoteArmed: false, uart: false, ready: false, active: false, calibratedMask: 0, note: '连接已断开，保持锁定' });
    this.throttle = this.steer = 0; this.armSequence = null; this.mainStatusAt = -Infinity;
    this.locked(); this.notify();
  }
  nextSequence() { this.sequence = (this.sequence + 1) >>> 0; return this.sequence; }
  packet(op, args = {}) {
    const sequence = this.nextSequence();
    return { bytes: encodeCommand(op, { ...args, session: this.state.session, sequence }), sequence };
  }
  lockLocally(note) {
    this.throttle = this.steer = 0; this.armSequence = null;
    this.state.armed = false; this.state.armPending = false; this.state.note = note;
    this.acks.clear(note); this.queue.cancelPending(note); this.locked(); this.notify();
  }
  stop(note = '已停止并锁定') {
    this.lockLocally(note);
    if (!this.state.connected || !this.state.session) return Promise.resolve();
    return this.queue.offer(this.packet(OP.STOP).bytes, { kind: 'stop', expiresAt: this.now() + 500 });
  }
  canArm() {
    return this.state.connected && !!this.state.session && this.state.uart && this.state.ready &&
      !this.state.fault && !this.state.active && !this.state.remoteArmed && !this.state.configBusy &&
      this.state.calibratedMask === 15 && this.now() - this.mainStatusAt < 500;
  }
  async arm() {
    if (!this.canArm()) throw new Error('需要 UART 在线、四轮已确认、闭环就绪且无故障，停稳后才能解锁');
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
  setLimit(limit) { this.state.limit = integer(limit, 0, 300, '速度上限'); this.notify(); }
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
    if (gap >= 150 && (this.state.connected || this.state.armed)) {
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
    if (packet.kind !== 0) return !!this.state.session;
    if (!packet.session) return false;
    const changedSession = packet.session !== this.state.session;
    if (changedSession) {
      this.state.session = packet.session;
      this.unsolicitedArmedStopped = false;
      // Firmware rotates the session when STOP is accepted. Sending STOP in
      // response to every new session would cause an endless STOP/session loop.
      this.lockLocally('会话已更新，请手动解锁');
    }
    this.mainStatusAt = this.now();
    Object.assign(this.state, { uart: packet.uart, ready: packet.ready, remoteArmed: packet.armed,
      active: packet.active, fault: packet.fault, faultCode: packet.faultCode, reason: packet.reason,
      calibratedMask: packet.calibratedMask, left: packet.left, right: packet.right });
    if (packet.fault || !packet.uart || !packet.ready || packet.calibratedMask !== 15) {
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
  async led(on) {
    await this.queue.offer(this.packet(OP.LED, { on: on ? 1 : 0 }).bytes, { expiresAt: this.now() + 500 });
  }
  async acknowledged(op, args, wheel) {
    const packet = this.packet(op, args);
    const acknowledged = this.acks.expect(this.state.session, packet.sequence, wheel);
    try { await Promise.all([acknowledged, this.queue.offer(packet.bytes, { expiresAt: this.now() + 500 })]); }
    catch (error) { this.acks.clear(error.message); throw error; }
  }
  assertStopped() {
    if (!this.state.connected || !this.state.session || !this.state.uart || this.state.armed ||
        this.state.armPending || this.state.remoteArmed || this.state.active || this.state.configBusy ||
        this.now() - this.mainStatusAt >= 500) throw new Error('请连接主控、停止锁定并等待停稳状态');
  }
  async configure(values) {
    this.assertStopped();
    if (!Array.isArray(values) || values.length !== 4) throw new Error('需要四轮配置');
    const items = values.map(validateCalibration);
    if (items.some((item, index) => item.wheel !== index)) throw new Error('配置必须按 M1–M4 顺序');
    this.state.configBusy = true; this.notify();
    try {
      for (const item of items) {
        this.state.note = `正在写入 ${WHEELS[item.wheel]} 配置`; this.notify();
        await this.acknowledged(OP.MECHANICAL, item, item.wheel);
        await this.acknowledged(OP.TUNING, item, item.wheel);
      }
      this.state.note = '四轮配置已确认，仅保存至 STM32 RAM；仍保持锁定';
    } finally { this.state.configBusy = false; this.notify(); }
  }
  async clearFault() {
    this.assertStopped(); this.state.configBusy = true; this.notify();
    try { await this.acknowledged(OP.CLEAR_FAULT, {}, 255); this.state.note = '清故障请求已确认，请检查状态'; }
    finally { this.state.configBusy = false; this.notify(); }
  }
}
