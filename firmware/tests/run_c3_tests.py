"""Exercise the C3/PA pin profile and direct PWM against real UART peers."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
         "-DCRTC_C3_DASHBOARD=1", "-DCRTC_PA_CHASSIS=1",
         "-I" + str(root / "shared")]
if os.environ.get("SANITIZE", "1") != "0":
    flags += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
else:
    print("C3 tests: SANITIZE=0; UBSan NOT run", flush=True)
with tempfile.TemporaryDirectory(prefix="crtc-c3-tests-") as folder:
    for name in ("test_pa_chassis", "test_c3_manual"):
        exe = Path(folder) / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([os.environ.get("CC", "gcc"), *flags,
                        str(root / "tests" / (name + ".c")),
                        *map(str, sorted((root / "shared").glob("*.c"))),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
