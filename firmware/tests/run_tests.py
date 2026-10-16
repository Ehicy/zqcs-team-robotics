"""Compile and execute real shared C protocol/state machines, without hardware."""
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
if os.environ.get("SANITIZE", "1") != "0":
    flags += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
if os.environ.get("ASAN", "0") == "1":
    flags += ["-fsanitize=address"]
with tempfile.TemporaryDirectory(prefix="crtc-board-link-") as folder:
    exe = Path(folder) / ("test_board_link.exe" if os.name == "nt" else "test_board_link")
    subprocess.run([cc, *flags, "-I" + str(ROOT / "shared"),
                    str(ROOT / "tests/test_board_link.c"),
                    *map(str, sorted((ROOT / "shared").glob("*.c"))), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
