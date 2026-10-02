"""Build ELF/HEX/BIN with Arm GNU GCC. Does not flash the board."""
from pathlib import Path
import argparse
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--toolchain", type=Path, help="Directory containing arm-none-eabi-gcc")
parser.add_argument("--target", choices=("car", "ps2-led", "motor-buttons"), default="car",
                    help="car: normal firmware; ps2-led: LED test; motor-buttons: four motor toggles")
args = parser.parse_args()

def tool(name):
    base = "arm-none-eabi-" + name
    candidate = str(args.toolchain / base) if args.toolchain else base
    found = shutil.which(candidate)
    if not found:
        raise SystemExit("Missing tool: " + candidate)
    return found

out = ROOT / "build"
if args.target != "car":
    out = out / args.target
out.mkdir(parents=True, exist_ok=True)
flags = ["-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-std=c99", "-Os", "-g3",
         "-ffunction-sections", "-fdata-sections", "-fno-common", "-Wall", "-Wextra",
         "-DSTM32F10X_MD", "-DUSE_STDPERIPH_DRIVER",
         "-Ihardware/Inc", "-Ilib/cmsis", "-Ilib/fwlib/inc"]
sources = sorted((ROOT / "hardware/Src").glob("*.c"))
if args.target == "ps2-led":
    sources = [ROOT / "hardware/Src" / name for name in ("delay.c", "ps2.c")]
    sources += [ROOT / "examples/ps2-led/main.c"]
elif args.target == "motor-buttons":
    sources = [ROOT / "hardware/Src" / name for name in ("delay.c", "ps2.c", "pwm.c")]
    sources += [ROOT / "examples/motor-buttons/main.c"]
sources += [ROOT / "lib/cmsis" / name for name in ("system_stm32f10x.c", "stm32f10x_it.c")]
sources += [ROOT / "lib/fwlib/src" / name for name in
            ("misc.c", "stm32f10x_gpio.c", "stm32f10x_rcc.c", "stm32f10x_tim.c")]
sources += [ROOT / "gcc/startup_stm32f103.S"]
objects = []
for src in sources:
    obj = out / (src.stem + ".o")
    strict = ["-Werror"] if "hardware" in src.parts or "examples" in src.parts else []
    subprocess.run([tool("gcc"), *flags, *strict, "-c", str(src), "-o", str(obj)], cwd=ROOT, check=True)
    objects.append(str(obj))
elf = out / ("crtc-motor.elf" if args.target == "car" else args.target + ".elf")
subprocess.run([tool("gcc"), *flags, "-nostdlib", "-Tgcc/stm32f103c8.ld",
                "-Wl,--gc-sections", "-Wl,-Map=" + str(elf.with_suffix(".map")),
                *objects, "-Wl,--start-group", "-lc", "-lgcc", "-Wl,--end-group", "-o", str(elf)], cwd=ROOT, check=True)
for fmt, ext in (("ihex", "hex"), ("binary", "bin")):
    subprocess.run([tool("objcopy"), "-O", fmt, str(elf), str(elf.with_suffix("." + ext))], check=True)
subprocess.run([tool("size"), str(elf)], check=True)
print("Built:", elf.with_suffix(".hex"))
