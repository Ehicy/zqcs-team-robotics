import { WheelOdometry } from './odometry.js';
import { VEHICLE, fieldPose, calibrationRoute, drawCompetitionField } from './competition-field.js';

// This view only observes telemetry. The supplied snapshot is read-only.
export function installLiveMap({ snapshot, calibration, now = () => performance.now() }) {
  const $ = id => document.getElementById(id);
  const odom = new WheelOdometry();
  drawCompetitionField($('competitionTerrain'));
  let lastDraw = -Infinity;
  let message = '';
  function health(s, time) {
    return s.connected && s.uart && s.ready && !s.fault && !s.faultCode &&
      !s.configBusy && s.calibratedMask === 15 && time - s.mainAt < 500 &&
      time - s.speedAt < 500 && s.speed?.validMask === 15 && !s.speed.faultCode;
  }
  function canReset(s, time) {
    return health(s, time) && !s.armed && !s.armPending && !s.remoteArmed && !s.active &&
      time - s.encoderAt < 500 && s.encoder?.validMask === 15 &&
      s.speed.measured.every(v => v === 0) && s.speed.targets.every(v => v === 0);
  }
  function draw(force = false) {
    const time = now();
    const s = snapshot();
    odom.check({ now: time, session: s.session, healthy: health(s, time) });
    if (!force && time - lastDraw < 100) return;
    lastDraw = time;
    $('mapOrigin').disabled = !canReset(s, time);
    $('mapStatus').textContent = message || odom.reason;
    $('mapStatus').classList.toggle('fault', !odom.active);
    const positioned = odom.track !== null && odom.path.length > 0;
    const side = $('mapSide').value;
    const competition = $('mapView').value === 'competition';
    const world = fieldPose(odom, side);
    $('mapPose').textContent = positioned
      ? `${odom.active ? '估计' : '最后估计（已失效）'} X ${odom.x.toFixed(0)} mm · Y ${odom.y.toFixed(0)} mm · 朝向 ${(odom.yaw * 180 / Math.PI).toFixed(1)}°`
      : '位置 — · 朝向 —（需轮距及起点）';
    $('mapFieldPose').textContent = positioned
      ? `场地估计：横向 ${world.x.toFixed(0)} mm / 纵向 ${world.y.toFixed(0)} mm（${side === 'red' ? '红方 a1' : '蓝方 h8'} 起点映射）`
      : `待确认当前位置 → ${side === 'red' ? '红方 a1 中心，车头朝行号增大方向' : '蓝方 h8 中心，车头朝行号减小方向'}`;
    $('mapWheelTravel').textContent = odom.lastAt > -Infinity
      ? `轮轴累计路程 M1–M4：${odom.travel.map(v => v.toFixed(0)).join(' / ')} mm${odom.active ? '' : '（已冻结）'}`
      : '轮轴累计路程：等待起点';
    const scale = competition ? 1 : Number($('mapScale').value);
    const follow = $('mapFollow').checked && positioned;
    const cx = follow ? odom.x : 0, cy = follow ? odom.y : 0;
    const project = p => {
      if (competition) { const w = fieldPose({ ...p, yaw: p.yaw ?? 0 }, side); return [w.x, 2400 - w.y]; }
      return [-(p.y - cy) / scale, -(p.x - cx) / scale];
    };
    $('mapCanvas').setAttribute('viewBox', competition ? '-220 -80 2700 2700' : '-300 -200 600 400');
    $('competitionTerrain').setAttribute('visibility', competition ? 'visible' : 'hidden');
    $('mapLocalGrid').setAttribute('visibility', competition ? 'hidden' : 'visible');
    $('mapScale').disabled = competition;
    $('mapFollow').disabled = competition;
    $('mapEmpty').setAttribute('x', competition ? '1200' : '0');
    $('mapEmpty').setAttribute('y', competition ? '-30' : '0');
    $('mapEmpty').setAttribute('font-size', competition ? '40' : '15');
    $('mapTrail').setAttribute('stroke-width', competition ? '12' : '2.5');
    const route = calibrationRoute(side);
    $('mapPlanned').setAttribute('points', route.map(p => `${p.x},${2400-p.y}`).join(' '));
    $('mapPlanned').setAttribute('visibility', competition ? 'visible' : 'hidden');
    $('mapCarBody').setAttribute('width', String(VEHICLE.widthMm / scale));
    $('mapCarBody').setAttribute('height', String(VEHICLE.lengthMm / scale));
    $('mapCarBody').setAttribute('x', String(-VEHICLE.widthMm / scale / 2));
    $('mapCarBody').setAttribute('y', String(-VEHICLE.lengthMm / scale / 2));
    $('mapCarBody').setAttribute('stroke-width', competition ? '8' : '1.5');
    $('mapCarArrow').setAttribute('transform', `scale(${competition ? 6 : 1})`);
    // Each grid cell is 50 SVG units. Pan the grid with world coordinates.
    $('mapGridPattern').setAttribute('x', String((cy / scale) % 50));
    $('mapGridPattern').setAttribute('y', String((cx / scale) % 50));
    $('mapGridLabel').textContent = competition ? 'V1.5 场地 2400×2400 mm · 每格 300 mm · 下侧为红方视角' : `每格 ${50 * scale} mm · 上为初始车头方向`;
    $('mapTrail').setAttribute('points', odom.path.map(p => project(p).join(',')).join(' '));
    const [ox, oy] = project({ x: 0, y: 0 });
    $('mapStart').setAttribute('transform', `translate(${ox} ${oy}) scale(${competition ? 5 : 1})`);
    $('mapStart').setAttribute('visibility', positioned ? 'visible' : 'hidden');
    const [px, py] = project(odom);
    $('mapCar').setAttribute('transform', `translate(${px} ${py}) rotate(${competition ? world.screenAngle : -odom.yaw * 180 / Math.PI})`);
    $('mapCar').setAttribute('visibility', positioned ? 'visible' : 'hidden');
    $('mapCar').setAttribute('opacity', odom.active ? '1' : '.35');
    $('mapEmpty').setAttribute('visibility', positioned ? 'hidden' : 'visible');
  }
  $('mapOrigin').addEventListener('click', () => {
    try {
      const s = snapshot(), time = now();
      if (!canReset(s, time)) throw new Error('需要连接、锁定并确认四轮停稳后设置地图起点');
      const raw = $('mapTrack').value.trim();
      odom.start({ counts: s.encoder.counts, calibration: calibration(), trackMm: raw ? Number(raw) : null, session: s.session, now: time });
      message = '';
    } catch (e) { message = e.message; }
    draw(true);
  });
  function invalidate(reason) { message = ''; odom.invalidate(reason); draw(true); }
  $('mapTrack').addEventListener('input', () => invalidate('轮距已修改'));
  $('mapSide').addEventListener('change', () => invalidate('阵营映射已修改'));
  $('mapView').addEventListener('change', () => draw(true));
  $('calibrationForm').addEventListener('input', () => invalidate('轮参数表单已修改'));
  $('calibrationForm').addEventListener('submit', () => invalidate('正在重写轮参数'));
  $('resetFormButton').addEventListener('click', () => invalidate('轮参数表单已重置'));
  $('mapScale').addEventListener('change', () => draw(true));
  $('mapFollow').addEventListener('change', () => draw(true));
  draw(true);
  return {
    tick: draw,
    invalidate,
    receive(packet) {
      const s = snapshot(), time = now();
      odom.update({ counts: packet.counts, validMask: packet.validMask, session: s.session, now: time, healthy: health(s, time) });
      draw();
    },
  };
}
