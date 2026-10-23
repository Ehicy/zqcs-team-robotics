"""Compile, never upload. Avoid older toolchains failing on Chinese source paths."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--pio', required=True, type=Path)
parser.add_argument('--core-dir', type=Path)
parser.add_argument('--output', type=Path, default=ROOT / 'output/pid-20261018/esp32')
parser.add_argument('--env', choices=('board-link', 'c3-dashboard'), default='board-link')
args = parser.parse_args()
env = os.environ.copy()
if args.core_dir:
    env['PLATFORMIO_CORE_DIR'] = str(args.core_dir.resolve())
with tempfile.TemporaryDirectory(prefix='crtc-esp32-pid-') as temp:
    project = Path(temp) / 'esp32'
    shutil.copytree(ROOT / 'firmware/esp32', project, ignore=shutil.ignore_patterns('.pio', '.git'))
    shutil.copytree(ROOT / 'firmware/shared', Path(temp) / 'shared')
    subprocess.run([str(args.pio.resolve()), 'run', '-d', str(project), '-e', args.env],
                   env=env, cwd=temp, check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('firmware.bin', 'firmware.elf', 'partitions.bin', 'bootloader.bin'):
        source = project / '.pio/build' / args.env / name
        if source.exists():
            shutil.copy2(source, args.output / name)
    print('Built (not uploaded):', args.output.resolve())
