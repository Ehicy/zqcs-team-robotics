"""Build/package the two ST-Link emergency images. No serial, USB or flashing calls."""
from pathlib import Path
import argparse
import importlib.util
import json
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def validate_vectors(data, base):
    if len(data) < 256 or len(data) % 4 or base + len(data) > 0x0800F800:
        raise ValueError("APP size/alignment exceeds the reserved code region")
    sp, pc = struct.unpack_from("<II", data)
    if not (0x20000000 < sp <= 0x20005000 and sp % 8 == 0 and pc & 1
            and base + 256 <= (pc & ~1) < base + len(data)):
        raise ValueError("APP vectors do not match the target load address")
    return sp, pc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path)
    parser.add_argument("--include-legacy-usb", action="store_true",
                        help="Also build the inactive historical USB application")
    args = parser.parse_args()
    out = ROOT / "output/fallback-20261019"
    manifest = {"profile": "dual-f103-open-loop-pa", "build_only": True, "images": []}
    spec = importlib.util.spec_from_file_location(
        "crtc_dfu_image", ROOT / "firmware/stm32-usb-bootloader/image.py")
    codec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(codec)
    images = [
        ("remote-direct", "fallback-remote", False, 0x08000000),
        ("motor-direct", "fallback-motor", False, 0x08000000),
    ]
    if args.include_legacy_usb:
        images.append(("motor-usb", "fallback-motor", True, 0x08002000))
    for name, target, usb, base in images:
        cmd = [sys.executable, str(ROOT / "sample-mecanum-car-study/build_firmware.py"),
               "--target", target]
        if args.toolchain:
            cmd += ["--toolchain", str(args.toolchain)]
        if usb:
            cmd += ["--usb-bootloader"]
        subprocess.run(cmd, check=True)
        source = ROOT / "sample-mecanum-car-study/build" / target
        if usb:
            source /= "usb"
        data = (source / (target + ".bin")).read_bytes()
        sp, pc = validate_vectors(data, base)
        dest = out / name
        dest.mkdir(parents=True, exist_ok=True)
        for ext in ("bin", "hex", "elf"):
            shutil.copy2(source / (target + "." + ext), dest)
        if usb:
            package = codec.package(data)
            if codec.unpack(package) != data:
                raise ValueError("USB package roundtrip failed")
            (dest / (target + ".dfu.bin")).write_bytes(package)
        manifest["images"].append({"directory": name, "target": target,
                                   "origin": hex(base), "bytes": len(data),
                                   "initial_sp": hex(sp), "reset_vector": hex(pc)})
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Prepared A direct / B direct ST-Link images. No device was opened or flashed.")


if __name__ == "__main__":
    main()
