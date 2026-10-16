"""当前板上固件（crtc-lower-pid-v2 source=BLE）的链路复测。

由 tools/verify_live_chip_link_20261016.py 派生，改动只有两处，其余逐字保留：
  1) 输出写到新文件，不覆盖 2026-10-16 19:02 那份成功报告；
  2) 固件名断言放宽为 crtc-lower-pid-v2（当晚 ESP32 已按用户授权烧成 board-link），
     并把 firmware/source 记进报告，而不是要求旧灯控名。

仍然只做：读身份/状态 + 诊断 LED 往返。不发任何运动、PWM、ARM、DRIVE 指令。
判定标准与原脚本一致：connected=1、输出禁止位(flags&4)、六次 LED ACK 序号递增且
token/parser_errors/rejected/uart_faults 全程不变。
"""
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports

out = Path(r'D:/DEVELOP/CQU_AI/CRTC2026备赛/方案可视化/下层车接线-20261016/chip-link-test-after-esp32-flash.json')
ports = [p for p in list_ports.comports()
         if (p.vid, p.pid) == (0x303A, 0x1001) and (p.serial_number or '').upper() == 'C0:4E:30:2B:D9:A4']
if len(ports) != 1:
    raise SystemExit('Expected ESP32 native USB identity unavailable')
p = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
p.dtr = False
p.rts = False
p.port = '\\\\.\\' + ports[0].device
report = {'port': ports[0].device, 'commands': [], 'passed': False,
          'scope': 'USB to ESP32, UART LED command and STM32 ACK; no optical or motor validation'}
pending = bytearray()


def request(command, pattern):
    record = {'command': command, 'lines': []}
    report['commands'].append(record)
    p.write((command + '\n').encode('ascii'))
    p.flush()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        pending.extend(p.read(1024))
        while b'\n' in pending:
            raw, _, rest = pending.partition(b'\n')
            pending[:] = rest
            line = raw.decode('ascii', errors='replace').strip()
            record['lines'].append(line)
            if line.startswith('ERR'):
                raise RuntimeError(line)
            found = re.fullmatch(pattern, line)
            if found:
                return found
    raise TimeoutError('No expected reply to ' + command)


def info():
    fields = dict(part.split('=', 1) for part in request('INFO', r'INFO (.*)')[1].split())
    report['firmware'] = fields.get('firmware')
    report['source'] = fields.get('source')
    if fields.get('firmware') != 'crtc-lower-pid-v2':
        raise RuntimeError('Expected crtc-lower-pid-v2, got ' + str(fields.get('firmware')))
    if fields.get('connected') != '1':
        raise RuntimeError('Peer not connected: connected=' + str(fields.get('connected')))
    return fields


try:
    p.open()
    report['before'] = info()
    previous = None
    for value in (1, 0, 1, 0, 1, 0):
        ack = request('LED ON' if value else 'LED OFF', rf'ACK LED={value} seq=(\d+)')
        seq = int(ack[1])
        if previous is not None and not 0 < ((seq - previous) & 0xffffffff) < 0x80000000:
            raise RuntimeError('Non-progressing ACK sequence')
        previous = seq
        print(f'STM32 ACK LED={value} seq={seq}', flush=True)
        time.sleep(.6)
    report['after'] = info()
    for name in ('token', 'parser_errors', 'rejected', 'uart_faults'):
        if report['before'][name] != report['after'][name]:
            raise RuntimeError(name + ' changed during test')
    report['passed'] = True
    print('PASS: six matching STM32 LED ACKs; peer connected, error counters stable', flush=True)
except Exception as exc:
    report['error'] = str(exc)
    print('FAIL: ' + str(exc), flush=True)
finally:
    if p.is_open:
        p.close()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
