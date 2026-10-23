"""A/PB6 unloaded servo probe: 1500 +/- 20 us, two 4-second cycles, then PWM off.

This cannot measure shaft angle. When initial PWM is zero, the first 1500 us
command can move far. Only run after the user confirms horn/linkage disconnected.
No Flash/config writes, no chassis commands, no changes to other servo channels.
"""
import argparse
import math
import re
import struct
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--run-unloaded', action='store_true')
args = parser.parse_args()
CLI = r'D:\programe\stm32\bin\STM32_Programmer_CLI.exe'
ROOT = Path(__file__).resolve().parents[2]
NM = r'D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin\arm-none-eabi-nm.exe'
elf = ROOT / 'sample-mecanum-car-study/build/fallback-remote/fallback-remote.elf'
symbols = subprocess.check_output([NM, str(elf)], text=True, errors='replace')
def symbol(name):
    return int(re.search(r'^([0-9A-Fa-f]+)\s+\w\s+'+name+'$', symbols, re.M)[1], 16)
def cli(*cmd):
    r = subprocess.run([CLI,'-c','port=SWD','sn=37FF71064E57343655231443',
                        'mode=HOTPLUG','freq=1000',*map(str,cmd)],
                       capture_output=True,text=True,errors='replace',timeout=3)
    if r.returncode or 'Error:' in r.stdout:
        raise RuntimeError(r.stdout[-1500:] + r.stderr[-500:])
    return r.stdout
def read(addr,size):
    s=cli('-r8',hex(addr),size)
    b=bytes.fromhex(' '.join(re.findall(r'^0x[0-9A-Fa-f]+\s*:\s*([0-9A-Fa-f ]+)',s,re.M)))
    if len(b)!=size: raise RuntimeError('Incomplete read')
    return b
def write(addr,value): cli('-w32',hex(addr),hex(value))
code=symbol('fb_bench_poll') & ~1
if read(code,32)!=elf.with_suffix('.bin').read_bytes()[code-0x8000000:code-0x8000000+32]:
    raise SystemExit('A firmware mismatch; no write performed')
regs=struct.unpack('<16I',read(0x40000800,64))
if regs[10]!=71 or regs[11]!=19999 or not regs[0]&1 or regs[6]&0x78!=0x68 or not regs[8]&1:
    raise SystemExit('Unexpected TIM4 configuration; no write performed')
mailbox=symbol('fallback_stlink_test')
command,status,*output=struct.unpack('<2I4h',read(mailbox,16))
if command!=0xDEAD0000 or status&0x200 or any(output):
    raise SystemExit('Chassis must already be stopped in bench hold; no write performed')
if regs[13]!=0:
    raise SystemExit('Servo already enabled; use normal motion owner instead of this unloaded probe')
print('Prepared: PB6, first reference 1500us (unknown physical angle), 1480..1520us, 8s, then PWM off.',flush=True)
if not args.run_unloaded:
    raise SystemExit(0)
try:
    write(0x40000834,1500)
    start=time.monotonic()
    while True:
        age=time.monotonic()-start
        if age>=8: break
        pulse=round(1500+20*math.sin(2*math.pi*age/4))
        assert 1480<=pulse<=1520
        write(0x40000834,pulse)
        print(f'{age:.2f}s: {pulse}us',flush=True)
        time.sleep(.1)
finally:
    write(0x40000834,0)
    print('PWM OFF readback: '+str(struct.unpack('<I',read(0x40000834,4))[0]),flush=True)
