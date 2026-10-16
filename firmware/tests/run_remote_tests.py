"""Build and run the production BLE input parser without hardware or Bluetooth."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
cc = os.environ.get("CC", "gcc")
if not shutil.which(cc):
    raise SystemExit("GCC missing: set CC to the installed host compiler")
flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-g", "-O1"]
sanitize = os.environ.get("SANITIZE", "1") != "0"
if sanitize:
    flags += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
if os.environ.get("ASAN", "0") == "1":
    flags += ["-fsanitize=address"]
print("Remote tests: UBSan enabled" if sanitize
      else "Remote tests: SANITIZE=0; UBSan NOT run", flush=True)
with tempfile.TemporaryDirectory(prefix="crtc-remote-control-") as folder:
    exe = Path(folder) / ("test_remote_control.exe" if os.name == "nt"
                          else "test_remote_control")
    subprocess.run([cc, *flags, "-I" + str(ROOT / "shared"),
                    str(ROOT / "tests" / "test_remote_control.c"),
                    str(ROOT / "shared" / "remote_control.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
