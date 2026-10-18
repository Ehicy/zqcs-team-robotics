"""Run the production M4 pulse qualifier with the real speed fault detector."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-g', '-O1']
sanitize = os.environ.get('SANITIZE', '1') != '0'
if sanitize:
    flags += ['-fsanitize=undefined', '-fno-sanitize-recover=all']
print('Encoder tests: UBSan enabled' if sanitize else 'Encoder tests: UBSan NOT run', flush=True)
with tempfile.TemporaryDirectory(prefix='crtc-encoder-') as folder:
    executable = Path(folder) / ('encoder.exe' if os.name == 'nt' else 'encoder')
    subprocess.run([os.environ.get('CC', 'gcc'), *flags, '-I'+str(root/'shared'),
                    str(root/'tests/test_encoder_edge_filter.c'),
                    str(root/'shared/encoder_edge_filter.c'),
                    str(root/'shared/speed_control.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
