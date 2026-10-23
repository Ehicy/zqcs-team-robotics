"""Explicit, leased bench control through A's RAM; no flash or GPIO writes.

run: fixed full-output test, two-minute maximum; stop/status: existing test.
release: exit a stopped bench hold and restore the normal, locked input path.
Firmware independently stops after 750 ms without a new token, or any fault.
"""
import argparse
import json
import re
import struct
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('run', 'stop', 'status', 'release'))
parser.add_argument('--seconds', type=float, default=120)
args = parser.parse_args()
if not 0 < args.seconds <= 120:
    parser.error('seconds must be within (0, 120]')
CLI = r'D:\programe\stm32\bin\STM32_Programmer_CLI.exe'
NM = r'D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin\arm-none-eabi-nm.exe'
elf = ROOT / 'sample-mecanum-car-study/build/fallback-remote/fallback-remote.elf'
symbols = subprocess.check_output([NM, str(elf)], text=True, errors='replace')
def address(name):
    m = re.search(r'^([0-9a-fA-F]+)\s+\w\s+' + re.escape(name) + r'$', symbols, re.M)
    if not m:
        raise RuntimeError('Missing firmware symbol ' + name)
    return int(m[1], 16)
mailbox = address('fallback_stlink_test')
identity = address('fb_bench_poll') & ~1
binary = elf.with_suffix('.bin').read_bytes()
signature = binary[identity - 0x08000000:identity - 0x08000000 + 32]
stop_file = ROOT.parent / 'work/fallback-20261019/main-a-stlink/bench-stop.request'
def cli(*command):
    r = subprocess.run([CLI, '-c', 'port=SWD', 'sn=37FF71064E57343655231443',
                        'mode=HOTPLUG', 'freq=1000', *map(str, command)],
                       capture_output=True, text=True, errors='replace', timeout=3)
    if r.returncode or 'Error:' in r.stdout:
        raise RuntimeError(r.stdout[-1600:] + r.stderr[-800:])
    return r.stdout
def read(addr, size):
    text = cli('-r8', hex(addr), size)
    chunks = re.findall(r'^0x[0-9a-fA-F]+\s*:\s*([0-9a-fA-F ]+)', text, re.M)
    data = bytes.fromhex(' '.join(chunks))
    if len(data) != size:
        raise RuntimeError('Incomplete SWD read: ' + text[-1200:])
    return data
def write(token):
    cli('-w32', hex(mailbox), hex(token))
def state():
    b = read(mailbox, 16)
    token, flags = struct.unpack_from('<2I', b)
    return dict(command=hex(token), state=flags & 255, connected=bool(flags & 256),
                enabled=bool(flags & 512), local_fault=bool(flags & 1024),
                peer_fault=bool(flags & 2048), output=list(struct.unpack_from('<4h', b, 8)))

def release():
    before = state()
    if int(before['command'], 16) not in (0, 0xDEAD0000):
        raise RuntimeError('Stop the bench command before release: ' + json.dumps(before))
    if not before['connected'] or before['enabled'] or any(before['output']):
        raise RuntimeError('B output-off not confirmed; retaining hold: ' + json.dumps(before))
    # A direct servo probe temporarily owns TIM4 outside the normal servo state.
    # Do not return to that state machine while a direct pulse is still present.
    if any(struct.unpack('<3I', read(0x40000834, 12))):
        raise RuntimeError('Servo PWM is still active; retaining bench hold.')
    write(0)
    time.sleep(.15)
    after = state()
    if int(after['command'], 16) or after['state']:
        raise RuntimeError('Bench release not confirmed: ' + json.dumps(after))
    print('RELEASED ' + json.dumps(after), flush=True)

if len(signature) != 32 or read(identity, 32) != signature:
    raise SystemExit('Connected board does not match A bench firmware; no command written.')
# A separate release process must not clear the latch while a keeper can still
# write a final heartbeat. Windows releases this lock even if the process dies.
control_lock = None
if args.action in ('run', 'release'):
    import msvcrt
    control_lock = stop_file.with_suffix('.lock').open('a+b')
    control_lock.seek(0)
    try:
        msvcrt.locking(control_lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        control_lock.close()
        raise SystemExit('Bench keeper is still active. Use stop, then wait for it to exit.')
if args.action == 'status':
    print(json.dumps(state()), flush=True)
elif args.action == 'stop':
    stop_file.write_text('stop', encoding='ascii')
    # Leave a latched non-run token so a concurrent keeper notices and exits.
    write(0xDEAD0000)
    time.sleep(.15)
    print('STOP ' + json.dumps(state()), flush=True)
    print('Stop is latched. After the keeper exits, use release to restore normal input.', flush=True)
elif args.action == 'release':
    stop_file.write_text('stop', encoding='ascii')
    release()
else:
    before = state()
    if int(before['command'], 16) & 0xffff0000 == 0xB37C0000 and before['state'] == 1:
        raise SystemExit('A test is already running. Use stop first.')
    if not before['connected'] or before['enabled'] or before['local_fault'] or before['peer_fault']:
        raise SystemExit('Not ready for bench start: ' + json.dumps(before))
    print('START requested: 100% PWM, 500ms ramp, max ' + str(args.seconds) + 's', flush=True)
    stop_file.write_text('run', encoding='ascii')
    write(0)
    time.sleep(.05)
    started = time.monotonic()
    seq = 1
    next_report = 0
    acknowledged = False
    try:
        while time.monotonic() - started < args.seconds:
            if stop_file.read_text(encoding='ascii').strip() != 'run':
                print('Operator stop requested.', flush=True)
                break
            write(0xB37C0000 | (seq & 0xffff))
            seq += 1
            now = time.monotonic() - started
            if now >= next_report:
                s = state()
                print(json.dumps(dict(seconds=round(now, 2), **s)), flush=True)
                if s['state'] in (2, 3, 4, 5) or not s['connected'] or s['local_fault'] or s['peer_fault']:
                    print('Stopped by firmware/link/lease; no automatic restart.', flush=True)
                    break
                if s['enabled'] and any(s['output']):
                    acknowledged = True
                if now > 5 and not acknowledged:
                    print('B did not report output within 5s; stopping.', flush=True)
                    break
                next_report = now + 1
            time.sleep(.12)
    finally:
        try:
            stop_file.write_text('stop', encoding='ascii')
            write(0xDEAD0000)
            time.sleep(.2)
            final = state()
            print('FINAL ' + json.dumps(final), flush=True)
            if not final['connected'] or final['enabled'] or any(final['output']):
                print('Output-off not yet confirmed; firmware lease remains bounded.', flush=True)
            else:
                release()
        except Exception as exc:
            print('Stop readback unavailable; firmware lease expires without heartbeats: ' + str(exc), flush=True)
