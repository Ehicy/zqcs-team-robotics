"""Host tests for the dual-F103 emergency profile; never accesses hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import re
import json
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
cc = os.environ.get("CC", "gcc")
if not shutil.which(cc):
    raise SystemExit("GCC missing: set CC to the installed host compiler")
flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-g", "-O1"]
sanitize = os.environ.get("SANITIZE", "1") != "0"
if sanitize:
    flags += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
print("Fallback tests: UBSan enabled" if sanitize
      else "Fallback tests: SANITIZE=0; UBSan NOT run", flush=True)
sources = list((ROOT / "fallback").glob("*.c"))
sources += [ROOT / "shared" / name for name in ("board_link.c", "servo_motion.c")]
with tempfile.TemporaryDirectory(prefix="crtc-fallback-") as folder:
    exe = Path(folder) / ("test_fallback.exe" if os.name == "nt" else "test_fallback")
    subprocess.run([cc, *flags, "-I" + str(ROOT / "fallback"),
                    "-I" + str(ROOT / "shared"),
                    "-I" + str(REPO / "sample-mecanum-car-study/hardware/Inc"),
                    str(ROOT / "tests/test_fallback.c"), *map(str, sources),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The connection table is a human safety interface: compare it with C pin data.
class Tables(HTMLParser):
    def __init__(self):
        super().__init__()
        self.tables, self.rows, self.row, self.cell = [], None, None, None

    def handle_starttag(self, tag, attrs):
        if tag == "table":
            self.rows = []
        elif tag == "tr" and self.rows is not None:
            self.row = []
        elif tag in ("th", "td") and self.row is not None:
            self.cell = ""

    def handle_data(self, data):
        if self.cell is not None:
            self.cell += data

    def handle_endtag(self, tag):
        if tag in ("th", "td") and self.cell is not None:
            self.row.append(self.cell.strip())
            self.cell = None
        elif tag == "tr" and self.row is not None:
            self.rows.append(self.row)
            self.row = None
        elif tag == "table" and self.rows is not None:
            self.tables.append(self.rows)
            self.rows = None

pin_text = (ROOT / "fallback/fallback_pins.h").read_text(encoding="utf-8")
def pins(name):
    return [int(v) for v in re.search(r"#define " + name + r"\s+\{([^}]+)\}", pin_text)[1].split(",")]

doc = (ROOT / "web/fallback-wiring.html").read_text(encoding="utf-8")
model = json.loads(re.search(r'<script[^>]*id="seed-data"[^>]*>(.*?)</script>', doc, re.S)[1])
modules = {m["id"]: m for m in model["modules"]}
pairs = {(w["from"], w["to"]) for w in model["wires"]}
pwm, a1, a2 = pins("FB_PWM_PINS"), pins("FB_IN1_PINS"), pins("FB_IN2_PINS")
for i in range(4):
    driver, channel = ("tb1" if i < 2 else "tb2"), ("A" if i % 2 == 0 else "B")
    for pin, endpoint in [(pwm[i], "PWM"+channel), (a1[i], channel+"IN1"), (a2[i], channel+"IN2")]:
        assert (f"stm.PA{pin}", driver+"."+endpoint) in pairs
    assert ("stm.PB15", driver+".STBY") in pairs
assert ("remote.PA2", "stm.PB11") in pairs and ("stm.PB10", "remote.PA3") in pairs
for m in model["modules"]:
    for pin in m["pins"]:
        assert 0 <= pin["offset"] <= m["w" if pin["side"] in ("top", "bottom") else "h"]
for w in model["wires"]:
    for end in (w["from"], w["to"]):
        mid, pid = end.split(".")
        assert any(p["id"] == pid for p in modules[mid]["pins"]), end
assert "esp" not in modules
for driver in ("tb1", "tb2"):
    assert len(modules[driver]["pins"]) == 16
# Interactive drawing scripts are allowed; hardware/network control APIs are not.
assert not re.search(r"navigator\.(bluetooth|serial|usb)|WebSocket|fetch\s*\(|XMLHttpRequest", doc)
print("Wiring: original editor netlist matches production PA mapping; all endpoints valid")
