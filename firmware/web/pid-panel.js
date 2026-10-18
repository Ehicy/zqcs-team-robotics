export const PHASE_NAMES = Object.freeze(['等待链路', '等待 PID 初始化', '等待四轮校准', '锁定', '已解锁', '运行', '故障']);
export function installPidPanel({ root, controller, error = () => {}, log = () => {} }) {
  root.innerHTML = `<h2>统一 PID 参数</h2><p class="hint">四轮使用同一组系数，各自独立计算。1000 表示 1.000；Kd 默认 0，D 项对测速变化求导并作 30ms 低通滤波。</p>
    <div class="config-grid"><label>Kp_milli<input id="pidKp" type="number" min="0" max="20000" step="1" value="1000"></label>
    <label>Ki_milli<input id="pidKi" type="number" min="0" max="20000" step="1" value="2000"></label>
    <label>Kd_milli<input id="pidKd" type="number" min="0" max="20000" step="1" value="0"></label></div>
    <button id="writePid" type="button" class="primary">停稳后写入统一 PID</button>
    <p id="pidPhase" class="hint" role="status">等待连接</p><p class="hint">输入框是待写表单，不是PID回读值。系数仅存 ESP32 / STM32 RAM，ESP32 重启恢复 1000 / 2000 / 0 并自动下发。参数 ACK 不代表调参已通过实车验收；四轮机械参数仍需实测确认。</p>`;
  const field = id => root.querySelector(`#${id}`);
  function read() {
    const gains = { kpMilli: Number(field('pidKp').value), kiMilli: Number(field('pidKi').value), kdMilli: Number(field('pidKd').value) };
    if (Object.values(gains).some(v => !Number.isInteger(v) || v < 0 || v > 20000) || !(gains.kpMilli || gains.kiMilli))
      throw new Error('PID 系数应为 0–20000 的整数，Kp、Ki 不能同时为 0');
    return gains;
  }
  function set(gains) { field('pidKp').value = gains.kpMilli; field('pidKi').value = gains.kiMilli; field('pidKd').value = gains.kdMilli ?? 0; }
  function render() {
    const s = controller.state;
    field('pidPhase').textContent = `${PHASE_NAMES[s.phase] ?? '状态未知'} · PID 初始化${s.pidReady ? '已确认' : '未确认（需配套新版双板固件）'}`;
    const busy = s.armed || s.armPending || s.remoteArmed || s.active || s.configBusy;
    for (const id of ['pidKp', 'pidKi', 'pidKd']) field(id).disabled = busy;
    field('writePid').disabled = busy || !s.connected || !s.uart || !s.session;
  }
  field('writePid').onclick = async () => {
    try { await controller.configurePid(read()); log('统一 PID 已由 STM32 确认，保持锁定'); }
    catch (e) { error(e); }
    render();
  };
  render();
  return { read, set, render };
}
