/* CRTC 2026 manual V1.5, PDF pages 15–16. Coordinates in mm:
 * origin at red-view a1's lower-left corner, columns a..h, rows 1..8.
 * These are terrain footprints, NOT certified traversable/free space.
 */
export const VEHICLE = Object.freeze({ trackMm: 165, wheelbaseMm: 130, lengthMm: 240, widthMm: 190, diameterMm: 65 });
export const FIELD = Object.freeze({ size: 2400, cell: 300 });
export const TERRAIN = Object.freeze([
  { kind: '高台', cells: ['a4', 'd4', 'd5', 'e4', 'e5', 'h5'], fill: '#725b2f' },
  { kind: '台阶', cells: ['d1', 'e3', 'd6', 'e8'], fill: '#a17c30' },
  { kind: '坡道', cells: ['a5', 'a6', 'h3', 'h4', 'e1', 'd8'], fill: '#2e6145' },
  { kind: '桥梁', cells: ['b4', 'c4', 'f5', 'g5'], fill: '#566776' },
]);
export function cellCenter(cell) {
  if (!/^[a-h][1-8]$/.test(cell)) throw new Error('无效场地格');
  return { x: (cell.charCodeAt(0) - 97 + .5) * 300, y: (Number(cell[1]) - .5) * 300 };
}
export function fieldPose(pose, side = 'red') {
  if (!['red', 'blue'].includes(side)) throw new Error('请选择红方或蓝方');
  const start = cellCenter(side === 'red' ? 'a1' : 'h8');
  const sign = side === 'red' ? 1 : -1;
  return { x: start.x - sign * pose.y, y: start.y + sign * pose.x,
    screenAngle: (side === 'red' ? 0 : 180) - pose.yaw * 180 / Math.PI };
}
export function calibrationRoute(side) {
  return [0, 600, 0].map(x => fieldPose({ x, y: 0, yaw: 0 }, side));
}
// User-supplied V1.5 screenshot, retained unchanged (535×480 px).
// Only the inner 8×8 field is 2400 mm: pixel bounds [78,38]..[438,398].
// Outer row/column labels are margins, not additional field area.
export const FIELD_IMAGE = Object.freeze({ width: 535, height: 480, left: 78, top: 38, fieldPixels: 360 });
export function fieldToImage(x, y) {
  return { x: FIELD_IMAGE.left + x * FIELD_IMAGE.fieldPixels / FIELD.size,
    y: FIELD_IMAGE.top + (FIELD.size - y) * FIELD_IMAGE.fieldPixels / FIELD.size };
}
export function drawCompetitionField(group) {
  const NS = 'http://www.w3.org/2000/svg';
  const image = document.createElementNS(NS, 'image');
  const ratio = FIELD.size / FIELD_IMAGE.fieldPixels;
  const attrs = { id: 'mapFieldImage', href: 'assets/crtc-field-v15.png',
    x: -FIELD_IMAGE.left * ratio, y: -FIELD_IMAGE.top * ratio,
    width: FIELD_IMAGE.width * ratio, height: FIELD_IMAGE.height * ratio,
    preserveAspectRatio: 'none' };
  for (const [key, value] of Object.entries(attrs)) image.setAttribute(key, String(value));
  group.append(image);
}
