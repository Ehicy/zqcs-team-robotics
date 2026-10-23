"""Offline C3 pin/servo-capability and real shared UART peer tests."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
         '-DCRTC_C3_DASHBOARD=1', '-DCRTC_PA_CHASSIS=1', '-I' + str(root/'shared')]
if os.environ.get('SANITIZE', '1') != '0':
    flags += ['-fsanitize=undefined', '-fno-sanitize-recover=all']
else:
    print('C3 tests: SANITIZE=0; UBSan NOT run', flush=True)
with tempfile.TemporaryDirectory(prefix='crtc-c3-tests-') as folder:
    for name in ('test_pa_chassis', 'test_c3_manual', 'test_adaptive_pwm', 'test_quick_profile', 'test_device_settings'):
        exe = Path(folder)/(name+('.exe' if os.name=='nt' else ''))
        subprocess.run([os.environ.get('CC','gcc'), *flags, str(root/'tests'/(name+'.c')),
                        *map(str,sorted((root/'shared').glob('*.c'))), '-o', str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    exe=Path(folder)/('test_servo_bootquiet.exe' if os.name=='nt' else 'test_servo_bootquiet')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',
                    '-DCRTC_C3_DASHBOARD=1','-I'+str(root/'shared'),'-I'+str(root/'tests/servo_hal_mocks'),
                    str(root/'tests/test_servo_bootquiet.cpp'),str(root/'esp32/src/servo_output.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
