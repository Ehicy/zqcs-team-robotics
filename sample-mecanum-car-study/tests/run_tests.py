"""用 Python 3 + GCC 运行；仅电脑测试，不烧录。"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
CC = os.environ.get("CC", "gcc")
if not shutil.which(CC):
    raise SystemExit("Please install GCC or set CC to a compatible C compiler.")
flags = [CC, "-std=c99", "-Wall", "-Wextra", "-Werror",
         "-DSTM32F10X_MD", "-DUSE_STDPERIPH_DRIVER",
         "-Ihardware/Inc", "-Ilib/cmsis", "-Ilib/fwlib/inc"]

# 使用真实的 STM32 头文件，检查所有应用源文件。
sources = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "hardware/Src").glob("*.c"))
subprocess.run(flags + ["-fsyntax-only"] + sources, check=True)
print(f"PASS: all {len(sources)} application C files, real STM32 headers, warnings as errors", flush=True)

project = ET.parse("user/rmcic.uvprojx")
for node in project.findall(".//FilePath"):
    path = ROOT / "user" / node.text.replace("\\", "/")
    assert path.exists(), f"Keil source missing: {path}"
for node in project.findall(".//Cads/VariousControls/IncludePath"):
    for entry in node.text.split(";"):
        assert (ROOT / "user" / entry.replace("\\", "/")).is_dir(), entry
print("PASS: Keil source/include paths exist", flush=True)

with tempfile.TemporaryDirectory(prefix="mecanum-tests-") as temp:
    exe = str(Path(temp) / ("test_control.exe" if os.name == "nt" else "test_control"))
    extra = []
    # SANITIZE=0 可用于缺少 UBSan 运行库的 Windows GCC；本次 Linux 验证启用。
    if os.environ.get("SANITIZE", "1") != "0":
        extra = ["-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all"]
    subprocess.run(flags + extra + ["-Itests", "tests/test_control.c",
                   "tests/mock_hardware.c", "hardware/Src/ps2.c",
                   "tests/mock_calibration_flash.c", "hardware/Src/calibration_store.c",
                   "hardware/Src/calibration_control.c",
                   "hardware/Src/motor.c", "hardware/Src/pwm.c", "-lm", "-o", exe],
                   check=True)
    subprocess.run([exe], check=True)
    button_exe = str(Path(temp) / ("test_motor_buttons.exe" if os.name == "nt" else "test_motor_buttons"))
    subprocess.run(flags + extra + ["-Itests", "tests/test_motor_buttons.c",
                   "tests/mock_hardware.c", "hardware/Src/ps2.c", "hardware/Src/pwm.c",
                   "-o", button_exe], check=True)
    subprocess.run([button_exe], check=True)
    tuning_exe = str(Path(temp) / ("test_motor_tuning.exe" if os.name == "nt" else "test_motor_tuning"))
    subprocess.run(flags + extra + ["-Itests", "tests/test_motor_tuning.c",
                   "tests/mock_hardware.c", "hardware/Src/motor.c", "hardware/Src/pwm.c",
                   "-o", tuning_exe], check=True)
    subprocess.run([tuning_exe], check=True)
    mapping_exe = str(Path(temp) / ("test_joystick_mapping.exe" if os.name == "nt" else "test_joystick_mapping"))
    subprocess.run(flags + extra + ["-Itests", "tests/test_joystick_mapping.c",
                   "tests/mock_hardware.c", "hardware/Src/motor.c", "hardware/Src/pwm.c",
                   "-lm", "-o", mapping_exe], check=True)
    subprocess.run([mapping_exe], check=True)
    clock_exe = str(Path(temp) / ("test_delay_clock.exe" if os.name == "nt" else "test_delay_clock"))
    subprocess.run(flags + extra + ["tests/test_delay_clock.c", "-o", clock_exe], check=True)
    subprocess.run([clock_exe], check=True)
    store_exe = str(Path(temp) / ("test_calibration_store.exe" if os.name == "nt" else "test_calibration_store"))
    subprocess.run(flags + extra + ["-Itests", "tests/test_calibration_store.c",
                   "tests/mock_calibration_flash.c", "hardware/Src/calibration_store.c",
                   "-o", store_exe], check=True)
    subprocess.run([store_exe], check=True)
    calibration_exe = str(Path(temp) / ("test_calibration_control.exe" if os.name == "nt" else "test_calibration_control"))
    subprocess.run(flags + extra + ["-Itests", "tests/test_calibration_control.c",
                   "tests/mock_hardware.c", "tests/mock_calibration_flash.c",
                   "hardware/Src/calibration_store.c", "hardware/Src/calibration_control.c",
                   "hardware/Src/ps2.c", "hardware/Src/motor.c", "hardware/Src/pwm.c",
                   "-o", calibration_exe], check=True)
    subprocess.run([calibration_exe], check=True)
