"""Build independent F103 USB-CDC/UART bench firmware; never flashes hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--toolchain', type=Path, required=True)
parser.add_argument('--tinyusb', type=Path, required=True)
parser.add_argument('--cmsis-device', type=Path, required=True)
parser.add_argument('--cmsis-core', type=Path, required=True)
parser.add_argument('--role', choices=['bridge', 'slave'], default='bridge')
args = parser.parse_args()
basename = 'usb-slave-test' if args.role == 'slave' else 'usb-board-test'
out = ROOT / 'build' / basename
out.mkdir(parents=True, exist_ok=True)

def pinned_commit(directory, expected):
    actual = subprocess.check_output(
        ['git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != expected:
        raise SystemExit(f'Wrong dependency revision: {directory}: {actual}, expected {expected}')
    subprocess.run(['git', '-C', str(directory), 'diff', '--exit-code', 'HEAD', '--',
                    'src', 'Include'], check=True, stdout=subprocess.DEVNULL)
    return actual

tinyusb_commit = pinned_commit(args.tinyusb, '86ad6e56c1700e85f1c5678607a762cfe3aa2f47')
cmsis_device_commit = pinned_commit(args.cmsis_device, 'c8e9a4a4f16b6d2cb2a2083cbe5161025280fb22')

def tool(name):
    path = shutil.which(str(args.toolchain / ('arm-none-eabi-' + name)))
    if not path:
        raise SystemExit('Missing tool: ' + name)
    return path

app = ROOT / 'examples/usb-board-test'
flags = ['-mcpu=cortex-m3', '-mthumb', '-mfloat-abi=soft', '-std=gnu11', '-Os', '-g3',
         '-ffunction-sections', '-fdata-sections', '-fno-common', '-Wall', '-Wextra',
         '-DSTM32F103xB', '-I' + str(app), '-I' + str(args.tinyusb / 'src'),
         '-I' + str(args.cmsis_device / 'Include'), '-I' + str(args.cmsis_core / 'Include')]
sources = [app / 'main.c', app / 'usb_descriptors.c', ROOT / 'gcc/startup_stm32f103.S']
if args.role == 'slave':
    flags += ['-DUSB_TEST_SLAVE=1']
    sources += [app / 'slave_protocol.c']
sources += [args.tinyusb / 'src' / x for x in
            ['tusb.c', 'common/tusb_fifo.c', 'device/usbd.c', 'device/usbd_control.c',
             'class/cdc/cdc_device.c', 'portable/st/stm32_fsdev/dcd_stm32_fsdev.c']]
objects = []
for i, source in enumerate(sources):
    obj = out / (str(i) + '_' + source.stem + '.o')
    strict = ['-Werror'] if source.parent == app else []
    subprocess.run([tool('gcc'), *flags, *strict, '-c', str(source), '-o', str(obj)], check=True)
    objects.append(str(obj))
elf = out / (basename + '.elf')
linker_script = app / 'stm32f103c8.ld'
subprocess.run([tool('gcc'), *flags, '-nostdlib', '-T' + str(linker_script),
                '-Wl,--gc-sections', '-Wl,-Map=' + str(elf.with_suffix('.map')),
                *objects, '-Wl,--start-group', '-lc', '-lgcc', '-Wl,--end-group', '-o', str(elf)], check=True)
for fmt, suffix in [('ihex', '.hex'), ('binary', '.bin')]:
    subprocess.run([tool('objcopy'), '-O', fmt, str(elf), str(elf.with_suffix(suffix))], check=True)
subprocess.run([tool('size'), str(elf)], check=True)
manifest = {'firmware': basename + '-v1', 'role': args.role,
            'flash_limit': 65536, 'ram_limit': 20480,
            'linker_script_sha256': hashlib.sha256(linker_script.read_bytes()).hexdigest(),
            'tinyusb_commit': tinyusb_commit, 'cmsis_device_commit': cmsis_device_commit,
            'compiler': subprocess.check_output([tool('gcc'), '--version'], text=True).splitlines()[0],
            'cmsis_core_headers': {name: hashlib.sha256(
                (args.cmsis_core / 'Include' / name).read_bytes()).hexdigest()
                for name in ['core_cm3.h', 'cmsis_version.h', 'cmsis_compiler.h',
                             'cmsis_gcc.h', 'mpu_armv7.h']}}
for suffix in ['.bin', '.hex']:
    content = elf.with_suffix(suffix).read_bytes()
    manifest[suffix[1:]] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
out.joinpath('manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print('Built:', elf.with_suffix('.hex'))
