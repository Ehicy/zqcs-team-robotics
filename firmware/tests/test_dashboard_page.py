#!/usr/bin/env python3
"""真实浏览器验收 firmware/web/dashboard.html。

做法：本地起一个 http://127.0.0.1 静态服务（安全上下文，Web Bluetooth 才可用），
用 Playwright 拉真实 Edge/Chromium 打开页面，注入一个"假 ESP32"蓝牙外设
（同一套 20 字节 GATT 协议 + 会话/解锁/ACK 行为），然后用**真实键盘事件**
驱动页面，最后断言真正写到控制特征上的字节。

这能覆盖单元测试覆盖不到的部分：模块加载、DOM 装配、键盘生命周期、
协议层与页面的接线，以及窄屏排版与运行时报错。

依赖：python -m pip install playwright && python -m playwright install chromium
运行：python firmware/tests/test_dashboard_page.py
"""

from __future__ import annotations

import functools
import http.server
import json
import pathlib
import socketserver
import sys
import threading

# Windows 控制台默认 GBK，打印 ✔/中文会抛 UnicodeEncodeError，先统一切到 UTF-8。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

WEB_DIR = pathlib.Path(__file__).resolve().parents[1] / "web"
OUT_DIR = pathlib.Path(__file__).resolve().parents[3] / "方案可视化" / "BLE遥控台-20261016"

FAILURES: list[str] = []
CHECKS = 0


def check(condition: bool, label: str, detail: str = "") -> None:
    global CHECKS
    CHECKS += 1
    if condition:
        print(f"  ✔ {label}")
    else:
        FAILURES.append(label + (f" —— {detail}" if detail else ""))
        print(f"  ✖ {label}" + (f" —— {detail}" if detail else ""))


# 注入到页面最早的假蓝牙外设 + 固件行为模拟。
FAKE_BLE = r"""
(() => {
  const writes = [];
  window.__writes = writes;
  window.__firmware = { session: 0x1234abcd, seq: 0, armed: false, calibratedMask: 0, limit: 100, drives: [] };
  let statusCharacteristic = null;
  const log = [];

  function mainStatus() {
    const fw = window.__firmware;
    const b = new Uint8Array(20), v = new DataView(b.buffer);
    b[0] = 1; b[1] = 0;
    b[2] = 1 | (fw.armed ? 2 : 0) | (fw.active ? 4 : 0) | 8;   // uart | armed | active | ready
    v.setUint32(4, fw.session, true);
    v.setUint32(8, fw.seq, true);
    v.setInt16(12, fw.left || 0, true);
    v.setInt16(14, fw.right || 0, true);
    v.setUint16(16, fw.limit, true);
    b[18] = fw.calibratedMask;
    b[19] = 0;
    return b;
  }
  function encoderStatus() {
    const b = new Uint8Array(20), v = new DataView(b.buffer);
    b[0] = 1; b[1] = 1; b[2] = 15;
    [0, 1, 2, 3].forEach(i => v.setInt32(4 + 4 * i, window.__firmware.counts?.[i] ?? 0, true));
    return b;
  }
  function speedStatus() {
    const b = new Uint8Array(20), v = new DataView(b.buffer);
    b[0] = 1; b[1] = 2; b[2] = 15; b[3] = 0;
    const fw = window.__firmware;
    const target = fw.lastDrive ? fw.lastDrive.limit : 0;
    [0, 1, 2, 3].forEach(i => {
      v.setInt16(4 + 2 * i, fw.armed ? target : 0, true);
      v.setInt16(12 + 2 * i, fw.armed ? target : 0, true);
    });
    return b;
  }
  window.__emitMain = () => statusCharacteristic && statusCharacteristic.emit(mainStatus());

  class FakeCharacteristic {
    constructor(uuid) {
      this.uuid = uuid; this.listeners = []; this.emitLog = [];
      this.properties = { write: true, writeWithoutResponse: false, read: true, notify: true, indicate: false };
      this.value = new DataView(new ArrayBuffer(20));
    }
    addEventListener(type, fn) { this.listeners.push(fn); }
    removeEventListener(type, fn) { this.listeners = this.listeners.filter(item => item !== fn); }
    async startNotifications() { return this; }
    async stopNotifications() { return this; }
    async readValue() { return this.value; }
    emit(bytes) {
      this.emitLog.push(Array.from(bytes));
      this.value = new DataView(Uint8Array.from(bytes).buffer);
      for (const fn of this.listeners.slice()) fn({ target: this });
    }
    async writeValueWithResponse(bytes) {
      const raw = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes.buffer ?? bytes);
      const copy = Array.from(raw);
      writes.push(copy);
      const v = new DataView(Uint8Array.from(copy).buffer);
      const fw = window.__firmware, op = copy[1], sequence = v.getUint32(8, true);
      if (op === 1) {                       // ARM
        fw.armed = true; fw.seq = sequence;
        log.push(['ARM', sequence]);
        setTimeout(() => statusCharacteristic.emit(mainStatus()), 8);
      } else if (op === 2) {                // DRIVE
        fw.lastDrive = { throttle: v.getInt16(12, true), steer: v.getInt16(14, true), limit: v.getUint16(16, true), ttl: v.getUint16(18, true), sequence };
        fw.drives.push(fw.lastDrive);
      } else if (op === 3) {                // STOP
        fw.armed = false; fw.session = (fw.session + 1) >>> 0; fw.seq = sequence;
        log.push(['STOP', sequence]);
        setTimeout(() => statusCharacteristic.emit(mainStatus()), 8);
      } else if (op === 5 || op === 6) {     // MECHANICAL / TUNING → 逐包 ACK
        const wheel = copy[12];
        if (op === 5) fw.calibratedMask |= (1 << wheel);
        const b = new Uint8Array(20), dv = new DataView(b.buffer);
        b[0] = 1; b[1] = 3; b[2] = wheel; b[3] = 0;
        dv.setUint32(4, fw.session, true);
        dv.setUint32(8, sequence, true);
        setTimeout(() => statusCharacteristic.emit(b), 6);
      } else if (op === 4) {                // LED
        log.push(['LED', copy[12]]);
      }
    }
  }

  statusCharacteristic = new FakeCharacteristic('2c4b0003-6f1a-4c9e-9b72-3c06f42b7d11');
  const controlCharacteristic = new FakeCharacteristic('2c4b0002-6f1a-4c9e-9b72-3c06f42b7d11');
  statusCharacteristic.emit(mainStatus());
  // 真实固件按 LINK_STATUS_PERIOD_MS=100 周期推送主状态，这里照做：
  // 页面的 canArm() 要求主状态在 500ms 内新鲜，配置完成后也必须收到新的 calibratedMask。
  setInterval(() => statusCharacteristic.emit(mainStatus()), 100);

  const service = {
    getCharacteristic: async uuid => (uuid.includes('0002') ? controlCharacteristic : statusCharacteristic),
  };
  const device = {
    name: 'CRTC-Lower-Car', id: 'fake-crtc',
    gatt: {
      connected: false,
      connect: async () => { device.gatt.connected = true; return { getPrimaryService: async () => service }; },
      disconnect: () => { device.gatt.connected = false; device.dispatch('gattserverdisconnected'); },
    },
    listeners: {},
    addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); },
    removeEventListener(type, fn) { this.listeners[type] = (this.listeners[type] || []).filter(item => item !== fn); },
    dispatch(type) { for (const fn of (this.listeners[type] || []).slice()) fn({ target: device }); },
  };
  const bluetooth = {
    requestDevice: async () => device,
    getAvailability: async () => true,
  };
  Object.defineProperty(navigator, 'bluetooth', { value: bluetooth, configurable: true });

  window.__tick = () => {
    if (!window.__firmware.armed) return;
    statusCharacteristic.emit(speedStatus());
    statusCharacteristic.emit(encoderStatus());
  };
  window.__fakeWrites = () => writes;
  window.__drives = () => window.__firmware.drives;
})();
"""


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args) -> None:  # 静默静态服务
        pass


def serve(directory: pathlib.Path):
    handler = functools.partial(QuietHandler, directory=str(directory))
    httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, httpd.server_address[1]


def drives(page) -> list[dict]:
    return page.evaluate("() => window.__drives()")


def last_drive(page) -> dict | None:
    items = drives(page)
    return items[-1] if items else None


def state(page) -> dict:
    return page.evaluate("() => window.__crtcDashboard.state()")


def wait_for(fn, timeout_ms: int = 4000, interval: int = 50):
    import time
    deadline = time.time() + timeout_ms / 1000
    while time.time() < deadline:
        value = fn()
        if value:
            return value
        time.sleep(interval / 1000)
    return None


def main() -> int:
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("需要 playwright：python -m pip install playwright && python -m playwright install chromium")
        return 2
    if not (WEB_DIR / "dashboard.html").exists():
        print(f"找不到 {WEB_DIR / 'dashboard.html'}")
        return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    httpd, port = serve(WEB_DIR)
    url = f"http://127.0.0.1:{port}/dashboard.html"
    print(f"服务目录 {WEB_DIR}\n打开 {url}\n")

    errors: list[str] = []
    try:
        with sync_playwright() as p:
            try:
                browser = p.chromium.launch(channel="msedge", headless=True)
                engine = "Edge (msedge)"
            except Exception:
                browser = p.chromium.launch(headless=True)
                engine = "Playwright Chromium"
            print(f"浏览器：{engine}\n")

            page = browser.new_page(viewport={"width": 1440, "height": 950})
            page.on("pageerror", lambda exc: errors.append(f"pageerror: {exc}"))

            def on_console(msg):
                # 浏览器对缺失 favicon 也会记 console.error，与控制逻辑无关，忽略。
                if msg.type == "error" and "favicon" not in msg.text.lower():
                    errors.append(f"console.error: {msg.text}")

            page.on("console", on_console)
            page.add_init_script(FAKE_BLE)
            page.goto(url, wait_until="load")

            # ---------- 1. 页面加载与能力检查 ----------
            print("[1] 页面加载与能力检查")
            page.wait_for_function("() => !!window.__crtcDashboard", timeout=8000)
            check(page.evaluate("() => window.isSecureContext") is True, "安全上下文成立（localhost）")
            check(page.is_hidden("#supportWarning"), "未出现不支持警告")
            check(page.inner_text("#connectionText").strip() == "尚未连接", "初始状态为尚未连接")
            check(page.inner_text("#modeBadge").strip() == "已锁定", "初始为已锁定")
            check(page.inner_text("#sessionText").strip() == "会话 —", "初始无会话")
            check(page.evaluate("() => document.querySelectorAll('.key').length") == 4, "虚拟键盘有 W/A/S/D 四键")
            check(page.evaluate("() => [...document.querySelectorAll('.key')].every(b => b.disabled)"), "未连接时方向键禁用")

            # ---------- 2. 连接 ----------
            print("\n[2] Web Bluetooth 连接")
            page.click("#connectButton")
            connected = wait_for(lambda: state(page).get("connected"))
            check(bool(connected), "点击后已连接")
            current = state(page)
            check(current.get("session") == 0x1234ABCD, f"读到会话 0x1234abcd（实际 {current.get('session'):#x}）")
            check(current.get("uart") is True, "主控链路显示在线")
            check(current.get("armed") is False, "新连接默认锁定")
            check(page.inner_text("#connectionText").strip() == "CRTC-Lower-Car", "顶栏显示设备名")
            check(page.evaluate("() => document.getElementById('connectionDot').classList.contains('online')"), "连接指示灯点亮")

            # ---------- 3. 未解锁时按键不产生运动 ----------
            print("\n[3] 未解锁时按键只记录意图")
            page.keyboard.down("w")
            page.wait_for_timeout(200)
            check(last_drive(page) is None, "未解锁按住 W 不发 DRIVE")
            check("未解锁" in page.inner_text("#intentText"), "界面提示尚未解锁")
            page.keyboard.up("w")
            page.wait_for_timeout(120)
            check(last_drive(page) is None, "松开后依然没有 DRIVE")

            # ---------- 4. 写入四轮配置（逐包 ACK） ----------
            print("\n[4] 四轮配置逐包确认")
            for wheel in range(4):
                page.check(f"#wheel{wheel}_confirmed")
            page.click("#saveConfigButton")
            calibrated = wait_for(lambda: state(page).get("calibratedMask") == 15, timeout_ms=8000)
            check(bool(calibrated), "四轮 ACK 后 calibratedMask=15")
            check(page.inner_text("#calibrationStatus").strip() == "4 / 4", "界面显示 4 / 4")
            ops = page.evaluate("() => window.__fakeWrites().map(b => b[1])")
            check(ops.count(5) == 4 and ops.count(6) == 4, f"每轮各发一条机械与调参包（5×{ops.count(5)} / 6×{ops.count(6)}）")

            # ---------- 5. 解锁 ----------
            print("\n[5] ARM 解锁")
            page.wait_for_function("() => !document.getElementById('armButton').disabled", timeout=5000)
            page.click("#armButton")
            armed = wait_for(lambda: state(page).get("armed") is True)
            check(bool(armed), "主控确认后进入已解锁")
            check(page.inner_text("#modeBadge").strip() == "已解锁", "徽标显示已解锁")
            check(page.evaluate("() => [...document.querySelectorAll('.key')].every(b => !b.disabled)"), "解锁后方向键可用")

            # ---------- 6. WASD 驱动 ----------
            print("\n[6] WASD 键盘驱动")
            page.keyboard.down("w")
            got = wait_for(lambda: (last_drive(page) or {}).get("throttle") == 1000, timeout_ms=3000)
            check(bool(got), "按住 W 发出 throttle=1000 / steer=0 的 DRIVE")
            drive = last_drive(page)
            check(drive["steer"] == 0 and drive["ttl"] == 200 and drive["limit"] == 100,
                  f"DRIVE 字段正确 steer={drive['steer']} ttl={drive['ttl']} limit={drive['limit']}")
            key_held = page.evaluate("() => document.querySelector('.key-w').classList.contains('held')")
            check(key_held, "虚拟键盘 W 键同步高亮")
            check("前进" in page.inner_text("#intentText"), "意图文本显示前进")
            check("左 100 / 右 100" in page.inner_text("#mixText"), "预计轮速显示 左 100 / 右 100")

            page.keyboard.down("a")
            got = wait_for(lambda: (last_drive(page) or {}).get("steer") == -1000, timeout_ms=3000)
            check(bool(got), "加按 A 后 steer=-1000（W+A 斜向）")
            check("左 0 / 右 100" in page.inner_text("#mixText"), "W+A 轮速预览 左 0 / 右 100")
            # 行驶中的实拍：此刻确实按着 W+A、底盘处于已解锁
            page.evaluate("() => window.scrollTo(0, 0)")
            page.wait_for_timeout(150)
            page.screenshot(path=str(OUT_DIR / "03-遥控台-行驶中-W加A.png"))

            page.keyboard.up("a")
            page.keyboard.up("w")
            got = wait_for(lambda: (last_drive(page) or {}).get("throttle") == 0 and (last_drive(page) or {}).get("steer") == 0, timeout_ms=3000)
            check(bool(got), "松开全部按键立即发零 DRIVE")
            check(state(page).get("armed") is True, "松开归零后仍保持解锁")

            # 方向键等价
            page.keyboard.down("ArrowUp")
            got = wait_for(lambda: (last_drive(page) or {}).get("throttle") == 1000, timeout_ms=3000)
            check(bool(got), "方向键 ↑ 等效于 W")
            page.keyboard.up("ArrowUp")
            wait_for(lambda: (last_drive(page) or {}).get("throttle") == 0, timeout_ms=3000)

            # 表单里打字不驱动
            before = len(drives(page))
            page.click("#wheel0_cpr")
            page.keyboard.type("w")
            page.wait_for_timeout(250)
            after = [d for d in drives(page)[before:] if d["throttle"] != 0 or d["steer"] != 0]
            check(after == [], "在校准输入框内打字不会驱动底盘")
            page.evaluate("() => document.getElementById('wheel0_cpr').blur()")

            # ---------- 7. 遥测 ----------
            print("\n[7] 遥测呈现")
            page.evaluate("() => { window.__firmware.counts = [10, -20, 30, -40]; }")
            page.evaluate("() => window.__tick()")
            page.wait_for_timeout(200)
            check(page.inner_text("#count0").strip() == "10", "编码器计数写入表格")
            check(page.inner_text("#count1").strip() == "-20", "负计数按原值显示")
            check("样本在线" in page.inner_text("#telemetryFreshness"), "采样新鲜度提示在线")

            # ---------- 8. 急停 ----------
            print("\n[8] 急停与锁定")
            page.keyboard.down("w")
            wait_for(lambda: (last_drive(page) or {}).get("throttle") == 1000, timeout_ms=3000)
            page.keyboard.up("w")
            page.keyboard.press("Space")
            stopped = wait_for(lambda: state(page).get("armed") is False, timeout_ms=3000)
            check(bool(stopped), "空格立即停止并锁定")
            ops = page.evaluate("() => window.__fakeWrites().map(b => b[1])")
            check(3 in ops, "向设备发出了 STOP(3) 包")
            check(page.inner_text("#modeBadge").strip() == "已锁定", "徽标回到已锁定")
            # 停止是本地立即锁定，设备侧的会话轮换随后才推过来，必须等
            rotated = wait_for(lambda: state(page).get("session") != 0x1234ABCD, timeout_ms=3000)
            check(bool(rotated), f"STOP 后会话已轮换（当前 {state(page).get('session'):#x}）")

            page.keyboard.down("w")
            page.wait_for_timeout(250)
            page.keyboard.up("w")
            check((state(page).get("armed")) is False, "锁定后按键无法重新驱动")

            # ---------- 9. 布局与截图 ----------
            print("\n[9] 排版与截图")
            page.evaluate("() => window.scrollTo(0, 0)")
            page.wait_for_timeout(150)
            wide = page.evaluate("() => ({ sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth })")
            check(wide["sw"] <= wide["cw"] + 1, f"1440px 无横向溢出（{wide['sw']} ≤ {wide['cw']}）")
            page.screenshot(path=str(OUT_DIR / "01-遥控台-全览.png"), full_page=True)

            page.set_viewport_size({"width": 390, "height": 844})
            page.wait_for_timeout(250)
            narrow = page.evaluate("() => ({ sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth })")
            check(narrow["sw"] <= narrow["cw"] + 1, f"390px 窄屏无横向溢出（{narrow['sw']} ≤ {narrow['cw']}）")
            page.screenshot(path=str(OUT_DIR / "02-遥控台-窄屏.png"), full_page=True)

            browser.close()
    except Exception:
        httpd.shutdown()
        raise

    print("\n[10] 运行时报错")
    check(not errors, "无 pageerror / console.error", "; ".join(errors[:4]))

    # ---------- 11. 真实浏览器能力（不注入假设备） ----------
    # 上面所有连接测试都用假外设，会掩盖"这台机器上 Web Bluetooth 到底有没有"。
    # 这里另开一个干净页面，确认安全上下文与 navigator.bluetooth 都是真的。
    print("\n[11] 真实浏览器能力（不注入假蓝牙）")
    capability_errors: list[str] = []
    try:
        with sync_playwright() as p:
            try:
                browser = p.chromium.launch(channel="msedge", headless=True)
            except Exception:
                browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1280, "height": 800})
            page.on("pageerror", lambda exc: capability_errors.append(str(exc)))
            page.goto(f"http://127.0.0.1:{port}/dashboard.html", wait_until="load")
            page.wait_for_function("() => !!window.__crtcDashboard", timeout=8000)
            check(page.evaluate("() => typeof navigator.bluetooth !== 'undefined'"),
                  "真实浏览器暴露 navigator.bluetooth")
            check(page.evaluate("() => window.isSecureContext") is True, "真实浏览器安全上下文成立")
            check(page.is_hidden("#supportWarning"), "真实浏览器下不显示不支持警告")
            check(page.evaluate("() => document.getElementById('connectButton').disabled") is False,
                  "真实浏览器下连接按钮可点")
            check(not capability_errors, "真实浏览器无脚本错误", "; ".join(capability_errors[:3]))
            browser.close()
    except Exception as exc:  # noqa: BLE001 - 能力检查失败要报出来而不是吞掉
        check(False, "真实浏览器能力检查执行成功", str(exc))

    httpd.shutdown()
    print(f"\n{'=' * 56}\n检查项 {CHECKS} 个，失败 {len(FAILURES)} 个")
    for item in FAILURES:
        print(f"  ✖ {item}")
    if not FAILURES:
        print("全部通过。截图已保存到：" + str(OUT_DIR))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
