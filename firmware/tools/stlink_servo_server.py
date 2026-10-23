"""Local-only PB6 position console. Host watchdog only; lost SWD may retain PWM.
Run: python firmware/tools/stlink_servo_server.py; open http://127.0.0.1:8766
No flash writes. One serialized SWD owner. Never exposes arbitrary addresses.
"""
import json
import msvcrt
import re
import secrets
import struct
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = r'D:\programe\stm32\bin\STM32_Programmer_CLI.exe'
NM = r'D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin\arm-none-eabi-nm.exe'
ELF = ROOT / 'sample-mecanum-car-study/build/fallback-remote/fallback-remote.elf'
HTML = ROOT / 'firmware/web/stlink-servo.html'
ORIGIN = 'http://127.0.0.1:8766'
TOKEN = secrets.token_urlsafe(32)

class LegacyContinuousBridge:
    def __init__(self):
        self.guard = threading.Lock()
        self.connected = False
        self.action = None
        self.target = self.actual = 1500
        self.expires = 0
        self.started = 0
        self.dragging = False
        self.request_id = -1
        self.sequence = 0
        self.owner = None
        self.error = ''
        self.updated = 0
        self.alive = True
        self.worker = threading.Thread(target=self.loop, daemon=True)
        self.worker.start()

    def cli(self, *args):
        r = subprocess.run([CLI, '-c', 'port=SWD',
            'sn=37FF71064E57343655231443', 'mode=HOTPLUG', 'freq=1000', *map(str,args)],
            capture_output=True, text=True, errors='replace', timeout=3,
            creationflags=subprocess.CREATE_NO_WINDOW)
        if r.returncode or 'Error:' in r.stdout:
            raise RuntimeError((r.stdout+r.stderr)[-600:])
        return r.stdout

    def read(self, addr, count):
        output = self.cli('-r8', hex(addr), count)
        data = bytes.fromhex(' '.join(re.findall(
            r'^0x[0-9a-fA-F]+\s*:\s*([0-9a-fA-F ]+)', output, re.M)))
        if len(data) != count:
            raise RuntimeError('ST-Link 数据读取不完整')
        return data

    def write(self, pulse):
        self.sequence = (self.sequence + 1) & 255
        self.cli('-w32', hex(self.mailbox), hex(0x53000000 | self.sequence << 16 | pulse))

    def open(self):
        raise RuntimeError('此连续旋转调试页已停用：大臂疑似位置型，1500μs 不是停止指令。需先确认型号并建立位置参考。')
        # Historical continuous-rotation bridge retained below; no hardware I/O.
        path = ROOT.parent / 'work/fallback-20261019/main-a-stlink/bench-stop.lock'
        path.parent.mkdir(parents=True, exist_ok=True)
        self.owner = path.open('a+b')
        self.owner.seek(0)
        msvcrt.locking(self.owner.fileno(), msvcrt.LK_NBLCK, 1)
        symbols = subprocess.check_output([NM, str(ELF)], text=True,
                                         creationflags=subprocess.CREATE_NO_WINDOW)
        def symbol(name):
            match = re.search(r'^([0-9a-fA-F]+)\s+\w\s+'+name+r'$', symbols, re.M)
            if not match: raise RuntimeError('需要先烧入滑块调试版 A 固件')
            return int(match[1],16)
        self.mailbox = symbol('fallback_stlink_test')
        address = symbol('fb_servo_probe_poll') & ~1
        offset = address - 0x08000000
        signature = ELF.with_suffix('.bin').read_bytes()[offset:offset+64]
        if len(signature)!=64 or self.read(address,64)!=signature:
            raise RuntimeError('连接板固件不匹配，未发送动作')
        command, flags, *outputs = struct.unpack('<2I4h',self.read(self.mailbox,16))
        if not flags & 256 or flags & (512|1024|2048) or any(outputs):
            raise RuntimeError('底盘尚未停妥或板间通信异常，未接管')
        regs = struct.unpack('<16I',self.read(0x40000800,64))
        if regs[10]!=71 or regs[11]!=19999 or regs[13]!=1500:
            raise RuntimeError('PB6 不在预期的 1500μs 停止状态，未接管')
        self.write(1500)
        time.sleep(.03)
        self.actual = struct.unpack('<I',self.read(0x40000834,4))[0]
        if self.actual!=1500: raise RuntimeError('未确认停止输出')
        with self.guard:
            self.connected = True
            self.error = ''
            self.target = 1500
            self.dragging = False
            self.updated = time.monotonic()

    def submit(self, data):
        action = data.get('action')
        number = data.get('id')
        if type(number) is not int: raise ValueError('缺少请求序号')
        with self.guard:
            # STOP must never be discarded because a different tab/agent used
            # a newer sequence. Older motion packets remain rejected.
            if number <= self.request_id and action != 'stop': return
            self.request_id = max(number, self.request_id)
            if action in ('connect','release'):
                if self.action: raise ValueError('正在连接或交还，请稍等')
                if action=='connect' and self.connected: return
                if action=='release' and not self.connected: return
                self.action = action
                self.target = 1500
                self.dragging = False
            elif action=='stop':
                self.target = 1500
                self.dragging = False
                self.expires = 0
            elif action=='pulse':
                pulse = data.get('pulse')
                if type(pulse) is not int or not 1470<=pulse<=1530:
                    raise ValueError('脉宽仅允许 1470～1530μs')
                if not self.connected or self.action: raise ValueError('请先连接 ST-Link')
                now = time.monotonic()
                if not self.dragging:
                    self.started = now
                    self.dragging = True
                # Do not renew the absolute burst limit with browser heartbeats.
                self.target = pulse if now-self.started < .75 else 1500
                self.expires = now+.3
            else: raise ValueError('未知操作')

    def status(self):
        with self.guard:
            return dict(connected=self.connected, pending=self.action,
                        target=self.target, actual=self.actual, error=self.error,
                        age=round(time.monotonic()-self.updated,2) if self.updated else None)

    def loop(self):
        while self.alive:
            try:
                with self.guard: action, connected = self.action, self.connected
                if action=='connect': self.open()
                elif action=='release' and connected:
                    self.write(1500)
                    time.sleep(.03)
                    if struct.unpack('<I',self.read(0x40000834,4))[0]!=1500:
                        raise RuntimeError('尚未确认停止，未交还手柄')
                    self.cli('-w32',hex(self.mailbox),'0')
                    with self.guard: self.connected=False
                    if self.owner: self.owner.close(); self.owner=None
                if action:
                    with self.guard: self.action=None
                with self.guard:
                    connected = self.connected
                    now=time.monotonic()
                    if now>=self.expires or (self.dragging and now-self.started>=.75):
                        self.target=1500
                    pulse=self.target
                if connected:
                    self.write(pulse)
                    actual=struct.unpack('<I',self.read(0x40000834,4))[0]
                    with self.guard: self.actual=actual; self.updated=time.monotonic()
                time.sleep(.05)
            except Exception as exc:
                # No retry of a non-neutral command. MCU expires it within 500ms.
                with self.guard:
                    self.connected=False; self.action=None; self.target=1500
                    self.dragging=False; self.error=str(exc)[-650:]
                if self.owner: self.owner.close(); self.owner=None
                time.sleep(.2)

bridge = None
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def send(self, code, body, kind='application/json; charset=utf-8'):
        if not isinstance(body,bytes): body=json.dumps(body,ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header('Content-Type',kind)
        self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'")
        self.end_headers(); self.wfile.write(body)
    def valid_host(self): return self.headers.get('Host')=='127.0.0.1:8766'
    def do_GET(self):
        if not self.valid_host(): return self.send(403,{'error':'local only'})
        if self.path=='/':
            return self.send(200,HTML.read_text(encoding='utf-8').replace('__TOKEN__',TOKEN).encode(), 'text/html; charset=utf-8')
        if self.path=='/api/state': return self.send(200,bridge.status())
        self.send(404,{})
    def do_POST(self):
        if (not self.valid_host() or self.headers.get('Origin')!=ORIGIN or
            self.headers.get('X-Control-Token')!=TOKEN):
            return self.send(403,{'error':'来源验证失败'})
        if self.path!='/api/control': return self.send(404,{})
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=1024: raise ValueError('请求过大')
            bridge.submit(json.loads(self.rfile.read(size)))
            self.send(200,bridge.status())
        except (ValueError,TypeError,AttributeError) as exc: self.send(400,{'error':str(exc)})

if __name__=='__main__':
    from stlink_position_bridge import PositionBridge
    bridge=PositionBridge()
    server=ThreadingHTTPServer(('127.0.0.1',8766),Handler)
    print('Servo console: '+ORIGIN,flush=True)
    try: server.serve_forever()
    finally:
        bridge.close()
        server.server_close()
