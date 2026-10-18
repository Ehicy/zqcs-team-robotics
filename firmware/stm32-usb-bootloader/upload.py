"""Validate package. USB writes require --write --vm-disconnected explicitly."""
from pathlib import Path
import argparse
import shutil
import subprocess
from image import unpack

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('package',type=Path)
    p.add_argument('--dfu-util',default='dfu-util')
    p.add_argument('--serial',help='USB serial selector; only one matching DFU device may be connected')
    p.add_argument('--write',action='store_true')
    p.add_argument('--motor-power-off','--vm-disconnected',dest='motor_power_off',action='store_true',
                   help='Operator confirms battery disconnected or both driver VM supplies isolated')
    args=p.parse_args()
    blob=args.package.read_bytes(); app=unpack(blob)
    print(f'Valid CRTCDFU1: APP {len(app)} bytes @ 0x08002000, package CRC checked')
    if not args.write:
        print('Validation only. No USB connection or write.'); return
    if not args.motor_power_off: p.error('Disconnect battery or both TB6612 VM supplies before --write')
    exe=shutil.which(args.dfu_util)
    if not exe: p.error('dfu-util missing; provide --dfu-util path. No driver is installed by this script.')
    cmd=[exe,'-d','1eaf:0003','-a','2','-i','0','-t','1024']
    if args.serial: cmd+=['-S',args.serial]
    subprocess.run([*cmd,'-D',str(args.package.resolve()),'-R'],check=True)
    print('DFU tool completed. Verify APP identity and locked state via ESP telemetry before reconnecting VM.')

if __name__=='__main__': main()
