"""Host-only tests of production update core and Python package codec; no USB."""
from pathlib import Path
import importlib.util
import os
import struct
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
loader=root/'stm32-usb-bootloader'
with tempfile.TemporaryDirectory(prefix='crtc-usb-') as d:
    exe=Path(d)/('usb-test.exe' if os.name=='nt' else 'usb-test')
    subprocess.run([os.environ.get('CC','gcc'),'-std=c99','-Wall','-Wextra','-Werror','-O2',
                    '-I'+str(loader),str(root/'tests/test_usb_update.c'),str(loader/'update_core.c'),
                    '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    dfu=Path(d)/('dfu-test.exe' if os.name=='nt' else 'dfu-test')
    subprocess.run([os.environ.get('CC','gcc'),'-std=c99','-Wall','-Wextra','-Werror','-O2',
                    '-include',str(root/'tests/usb_dfu_env.h'),'-I'+str(loader/'vendor'),'-I'+str(loader),
                    str(root/'tests/test_usb_dfu.c'),str(loader/'dfu.c'),str(loader/'update_core.c'),
                    '-o',str(dfu)],check=True)
    subprocess.run([str(dfu)],check=True)
spec=importlib.util.spec_from_file_location('usb_image',loader/'image.py')
image=importlib.util.module_from_spec(spec); spec.loader.exec_module(image)
app=bytearray(2052); struct.pack_into('<II',app,0,0x20005000,0x08002101)
blob=image.package(app); assert image.unpack(blob)==app
for broken in [blob[:-1],b'X'+blob[1:],blob[:1100]+b'X'+blob[1101:]]:
    try: image.unpack(broken)
    except ValueError: pass
    else: raise AssertionError('accepted corrupt package')
for pc in [0x08000101,0x08010001,0x08002100]:
    struct.pack_into('<I',app,4,pc)
    try: image.package(app)
    except ValueError: pass
    else: raise AssertionError('accepted wrong app vectors')
print('PASS USB Python package roundtrip, corruption, length and wrong-origin guards; UBSan NOT run')
