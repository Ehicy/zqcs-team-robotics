"""Build loader, relocated PID APP and first-install package. NEVER flashes."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from image import BASE, END, PAGE, package

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--toolchain', type=Path)
    p.add_argument('--hashes', action='store_true', help='Optional release SHA256; default records sizes only')
    p.add_argument('--out', type=Path, default=REPO/'output/usb-bootloader-20261018')
    args = p.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    objdir = out/'objects'; objdir.mkdir(exist_ok=True)
    def tool(name):
        name = 'arm-none-eabi-'+name+('.exe' if os.name == 'nt' else '')
        found = shutil.which(str(args.toolchain/name) if args.toolchain else name)
        if not found: raise SystemExit('Missing '+name)
        return found
    common = ['-mcpu=cortex-m3', '-mthumb', '-mfloat-abi=soft', '-Os', '-g3',
              '-ffunction-sections', '-fdata-sections', '-fno-common',
              '-fno-strict-aliasing', '-fno-delete-null-pointer-checks',
              '-Wall', '-Wextra', '-DTARGET_GENERIC_F103_PC13',
              '-I.', '-Ivendor', '-Ivendor/usb_lib', '-Ivendor/stm32_lib']
    sources = ['main.c', 'dfu.c', 'update_core.c', 'vendor/hardware.c', 'vendor/usb.c',
               'vendor/usb_descriptor.c', *['vendor/usb_lib/'+n+'.c' for n in
               ('usb_regs','usb_int','usb_init','usb_core','usb_mem')],
               'vendor/stm32_lib/c_only_startup.s', 'vendor/stm32_lib/cortexm3_macro.s']
    objs = []
    for source in sources:
        src = ROOT/source; obj = objdir/(source.replace('/','_')+'.o')
        flags = ['-std=gnu99'] if src.suffix == '.c' else []
        if not source.startswith('vendor/'):
            flags += ['-Werror']
        subprocess.run([tool('gcc'), *common, *flags, '-c', str(src), '-o', str(obj)], cwd=ROOT, check=True)
        objs.append(str(obj))
    elf = out/'crtc-usb-loader.elf'
    subprocess.run([tool('gcc'), *common, '-nostdlib', '-Tbootloader.ld',
                    '-Wl,--gc-sections', '-Wl,-Map='+str(elf.with_suffix('.map')),
                    *objs, '-Wl,--start-group', '-lc', '-lgcc', '-Wl,--end-group', '-o', str(elf)], cwd=ROOT, check=True)
    for fmt, ext in [('binary','bin'),('ihex','hex')]:
        subprocess.run([tool('objcopy'), '-O', fmt, str(elf), str(elf.with_suffix('.'+ext))], check=True)
    subprocess.run([tool('size'),str(elf)],check=True)
    loader=elf.with_suffix('.bin').read_bytes()
    if len(loader)>0x2000: raise SystemExit('Loader exceeds 8 KiB')
    build=[sys.executable,str(REPO/'sample-mecanum-car-study/build_firmware.py'),
           '--target','lower-motor','--usb-bootloader']
    if args.toolchain: build+=['--toolchain',str(args.toolchain)]
    subprocess.run(build,check=True)
    appdir=REPO/'sample-mecanum-car-study/build/lower-motor/usb'
    for ext in ('bin','hex','elf','map'):
        shutil.copy2(appdir/('lower-motor.'+ext),out/('lower-motor-usb.'+ext))
    app=(out/'lower-motor-usb.bin').read_bytes()
    (out/'lower-motor-usb.dfu.bin').write_bytes(package(app))
    combined=loader.ljust(0x2000,b'\xff')+app
    (out/'first-install.bin').write_bytes(combined)
    subprocess.run([tool('objcopy'), '-I','binary','-O','ihex','--change-addresses=0x08000000',
                    str(out/'first-install.bin'),str(out/'first-install.hex')],check=True)
    hashes={f.name:{'bytes':f.stat().st_size, **({'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} if args.hashes else {})}
            for f in sorted(out.iterdir()) if f.suffix in ('.bin','.hex','.elf')}
    sources_hash={str(f.relative_to(REPO)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest()
                  for folder in [ROOT, REPO/'sample-mecanum-car-study', REPO/'firmware/shared']
                  for f in sorted(folder.rglob('*'))
                  if args.hashes and f.is_file() and f.suffix in ('.c','.h','.s','.S','.ld','.py') and
                  not any(part in ('build','tests','__pycache__') for part in f.relative_to(folder).parts)}
    manifest={'profile':'CRTC F103C8 64KiB / 8MHz PC13 PB2 / Maple DFU alt2 + CRTCDFU1',
              'upstream':'df689808b6030280480c0d151ee9c552ecf6b405',
              'compiler':subprocess.check_output([tool('gcc'),'--version'],text=True).splitlines()[0],
              'loader_base':'0x08000000','app_base':hex(BASE),'app_end_exclusive':hex(END),
              'calibration':'0x0800F800..0x08010000 untouched by USB loader',
              'hardware_verified':False,'flashed':False,
              'artifacts':hashes,'sources_sha256':sources_hash}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print('Candidate built; NOT flashed or USB hardware verified:',out)

if __name__=='__main__': main()
