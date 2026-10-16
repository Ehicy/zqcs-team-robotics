"""USB -> ESP32 -> board-link UART -> STM32 PC13 integration check.

Runs only explicit diagnostic LED commands; never submits motor inputs.
Requires pyserial. ACK confirms the STM32 command/GPIO path, not optical output.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", default="C0:4E:30:2B:D9:A4")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--swd-cli", type=Path, help="Optional STM32CubeProgrammer CLI for GPIO readback")
    parser.add_argument("--stlink", default="37FF71064E57343655231443")
    args = parser.parse_args()
    matches = [p for p in list_ports.comports() if p.device == args.port
               and (p.vid, p.pid) in ((0x303A, 0x1001), (0x1A86, 0x55D3))
               and (p.serial_number or "").upper() == args.serial.upper()]
    if len(matches) != 1:
        raise SystemExit("Expected ESP32 USB serial identity not found")
    report = {"port": args.port, "serial": args.serial, "commands": [],
              "passed": False, "optical_confirmation": "user observation required"}
    pending = bytearray()
    ser = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
    ser.dtr = True
    ser.rts = False
    ser.port = args.port

    def request(command, pattern, timeout=2):
        entry = {"command": command, "lines": []}
        report["commands"].append(entry)
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            pending.extend(ser.read(256))
            while b"\n" in pending:
                raw, _, remainder = pending.partition(b"\n")
                pending[:] = remainder
                line = raw.decode("ascii", errors="replace").strip()
                entry["lines"].append(line)
                if line.startswith("ERR"):
                    raise RuntimeError(line)
                found = re.fullmatch(pattern, line)
                if found:
                    return found
        raise TimeoutError(f"No matching reply to {command!r}")

    def info():
        found = request("INFO", r"INFO (.*)")
        fields = dict(part.split("=", 1) for part in found[1].split())
        if fields.get("firmware") != "crtc-board-link-led-v1":
            raise RuntimeError("Unexpected ESP32 firmware")
        return fields

    try:
        with ser:
            time.sleep(0.3)
            ser.reset_input_buffer()
            deadline = time.monotonic() + 5
            while True:
                before = info()
                if before["connected"] == "1" and int(before["flags"]) & 4:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("STM32 board-link handshake/status unavailable")
                time.sleep(0.2)
            report["before"] = before
            last_sequence = None
            for on in (1, 0, 1, 0, 1, 0):
                ack = request("LED ON" if on else "LED OFF", rf"ACK LED={on} seq=(\d+)")
                sequence = int(ack[1])
                if last_sequence is not None and not 0 < ((sequence-last_sequence) & 0xffffffff) < 0x80000000:
                    raise RuntimeError("Repeated/out-of-order LED acknowledgement")
                last_sequence = sequence
                if args.swd_cli:
                    result = subprocess.run([
                        str(args.swd_cli), "-c", "port=SWD", "sn=" + args.stlink,
                        "freq=100", "mode=HOTPLUG", "-r32", "0x40011000", "16",
                        "-run"], capture_output=True, text=True, errors="replace", timeout=15)
                    report["commands"][-1]["swd"] = result.stdout + result.stderr
                    registers = re.search(r"0x40011000\s*:\s*([0-9A-F]{8})\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+([0-9A-F]{8})",
                                          result.stdout, re.IGNORECASE)
                    if result.returncode or not registers:
                        raise RuntimeError("GPIOC SWD readback unavailable")
                    crh, idr, odr = (int(registers[i], 16) for i in (2, 3, 4))
                    if (crh >> 20) & 15 != 2 or ((odr >> 13) & 1) != (1-on):
                        raise RuntimeError("PC13 output configuration/latch disagrees with LED ACK")
                    if ((idr >> 13) & 1) != (1-on):
                        raise RuntimeError("PC13 sampled pin level disagrees with LED ACK")
                print(f"STM32 acknowledged LED {'ON' if on else 'OFF'}, sequence={sequence}", flush=True)
                time.sleep(1)
            after = info()
            report["after"] = after
            if after["connected"] != "1" or not int(after["flags"]) & 4:
                raise RuntimeError("Link lost or output-inhibited status missing")
            for name in ("parser_errors", "rejected"):
                if after[name] != before[name]:
                    raise RuntimeError(f"ESP32 {name} changed during LED test")
            if after["token"] != before["token"]:
                raise RuntimeError("Connection token changed during LED test")
            report["passed"] = True
            print("PASS: three acknowledged ON/OFF cycles; motors remain inhibited")
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
