"""Exercise actual BLE parser, bridge and framed UART peers without hardware."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
if os.environ.get("SANITIZE", "1") != "0":
    flags += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
with tempfile.TemporaryDirectory(prefix="crtc-bridge-") as d:
    exe = Path(d) / ("bridge.exe" if os.name == "nt" else "bridge")
    subprocess.run([os.environ.get("CC", "gcc"), *flags, "-I"+str(root/"shared"),
                    str(root/"tests/test_remote_bridge.c"),
                    *map(str, sorted((root/"shared").glob("*.c"))), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
