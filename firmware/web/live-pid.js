/* Coalesced PID writes through the existing stopped-only firmware command.
 * UI changes are immediate; ACK is acceptance, not proof of STM32 sync.
 * Never send on loading a page, reading parameters or reconnecting. */
export function validateGains(value) {
  const gains = {kpMilli:value.kpMilli, kiMilli:value.kiMilli, kdMilli:value.kdMilli ?? 0};
  if (Object.values(gains).some(v=>!Number.isInteger(v)||v<0||v>20000)||!(gains.kpMilli||gains.kiMilli))
    throw new Error('PID 系数应为 0–20000 的整数，Kp、Ki 不能同时为 0');
  return gains;
}
const key = gains => JSON.stringify(validateGains(gains));
export class LivePidWriter {
  constructor({controller, direct=false, changed=()=>{}, written=()=>{}, error=()=>{}, quietMs=200, intervalMs=500}) {
    Object.assign(this,{controller,direct,changed,written,error,quietMs,intervalMs});
    this.pending=null; this.busy=false; this.failed=false; this.invalid=false; this.lastKey=null;
    this.waitingKey=null; this.lastWrite=-Infinity; this.message='拖动滑块自动写入，无需点击保存';
    this.context=this.connection();
  }
  connection() {const c=this.controller; return `${c.generation}/${c.state.session}/${c.state.connected}`;}
  checkContext() {
    const context=this.connection();
    if(context!==this.context) {
      const had=this.pending||this.busy;
      this.context=context;this.pending=null;this.failed=false;this.invalid=false;this.waitingKey=null;this.lastKey=null;
      if(had)this.message='连接已变化，旧待写值已取消；重新读取设备后再拖动';
    }
  }
  accept(gains) {
    this.checkContext(); const value=key(gains);
    if(this.pending||this.busy||this.invalid||this.waitingKey&&this.waitingKey!==value)return false;
    this.lastKey=value;this.waitingKey=null;return true;
  }
  edit(value) {
    this.checkContext();let gains;
    try{gains=validateGains(value);}catch(e){this.rejectDraft(e.message);throw e;}
    const c=this.controller;this.invalid=false;
    if(!c.state.connected||!c.state.session) {
      this.message='离线预览，未写入；连接并回读后再拖动';this.changed();return;
    }
    this.failed=false;
    if(!this.busy&&key(gains)===this.lastKey) {
      this.pending=null;this.message='与已确认参数相同，无需重复写入';
    } else {
      this.pending={gains,context:this.context,due:c.now()+this.quietMs};
      this.message='新值待自动写入';
    }
    this.changed();
  }
  rejectDraft(message) {this.pending=null;this.failed=false;this.invalid=true;this.message=message+'；未写入';this.changed();}
  ready() {
    const c=this.controller,s=c.state;
    return s.connected&&s.session&&!s.armed&&!s.armPending&&!s.remoteArmed&&!s.active&&!s.configBusy&&
      c.now()-c.mainStatusAt<500&&(this.direct?!!c.deviceSettings.calibration():s.uart);
  }
  release() {if(this.pending)this.pending.due=this.controller.now();return this.tick();}
  retry() {this.failed=false;if(this.pending)this.pending.due=this.controller.now();return this.tick();}
  async tick() {
    this.checkContext();
    if(!this.pending||this.busy||this.failed)return;
    const c=this.controller;
    if(!this.ready()) {
      this.message=c.state.armed||c.state.active||c.state.remoteArmed?
        '新值待写：现有固件需停稳，停止后自动写入':'新值待写：等待链路、完整回读或当前事务结束';
      this.changed();return;
    }
    if(c.now()<this.pending.due||c.now()-this.lastWrite<this.intervalMs)return;
    const attempt=this.pending;this.pending=null;this.busy=true;this.lastWrite=c.now();
    this.message='正在写入最新 PID…';this.changed();
    try {
      await c.configurePid(attempt.gains);
      if(this.connection()!==attempt.context)return;
      this.lastKey=key(attempt.gains);this.waitingKey=this.direct?this.lastKey:null;
      if(this.pending&&key(this.pending.gains)===this.lastKey)this.pending=null;
      if(!this.invalid)this.message=this.pending?'上一组已接受，正在合并最新拖动值':this.direct?
        'C3 已接受并保存；底盘空闲时同步（以设备回读为准）':'STM32 已确认 PID（RAM）';
      this.written({...attempt.gains},{pending:!!this.pending||this.invalid});
    } catch(e) {
      if(this.connection()!==attempt.context)return;
      if(!this.invalid){this.pending??=attempt;this.failed=true;this.message=`未写入：${e.message}；再拖动或点击重试`;}
      this.error(e);
    } finally {this.busy=false;this.checkContext();this.changed();}
  }
}
