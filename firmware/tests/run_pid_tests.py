"""Production PID/state/transport integration; no hardware access."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-O1', '-g']
sanitize = os.environ.get('SANITIZE', '1') != '0'
if sanitize:
    flags += ['-fsanitize=undefined', '-fno-sanitize-recover=all']
print('PID tests: UBSan enabled' if sanitize else 'PID tests: SANITIZE=0; UBSan NOT run', flush=True)
with tempfile.TemporaryDirectory(prefix='crtc-pid-') as temp:
    exe = Path(temp) / ('pid.exe' if os.name == 'nt' else 'pid')
    subprocess.run([os.environ.get('CC', 'gcc'), *flags, '-I'+str(root/'shared'),
                    str(root/'tests/test_pid.c'), *map(str, sorted((root/'shared').glob('*.c'))),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
