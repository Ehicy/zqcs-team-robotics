"""Bounded USB bench input for one, two, three, or all four selected wheels.

Requires pyserial only when opening a real port. Keep the tested wheel clear
of the ground. This sends open-loop duty, not speed or distance commands.
QUEUED replies mean accepted by the ESP32 console, not verified motor motion.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import sys
import time
from typing import Callable


FIRMWARE = "crtc-lower-motor-console-v1"
PWM_SUPPORTED = 8
OUTPUT_INHIBITED = 4
PWM_PERIOD = 0.050
PROBE_PERIOD = 0.100
STATUS_TIMEOUT = 0.300
PREPARE_TIMEOUT = 3.0
STOP_TIMEOUT = 1.0
READ_TIMEOUT = 0.010
WRITE_TIMEOUT = 0.050
MAX_PENDING = 4096


class BenchError(RuntimeError):
    """A precondition, link check or bounded operation failed."""


@dataclass(frozen=True)
class Info:
    connected: bool
    token: str
    flags: int
    uptime_ms: int


@dataclass(frozen=True)
class BenchResult:
    token: str
    pwm_writes: int
    stop_confirmed: bool


def parse_info(line: str, expected_firmware: str = FIRMWARE) -> Info:
    try:
        fields = dict(part.split("=", 1) for part in line.split()[1:])
        if fields["firmware"] != expected_firmware:
            raise ValueError("unexpected firmware")
        if fields["transport"] != "1":
            raise ValueError("UART transport unavailable")
        if fields["connected"] not in ("0", "1"):
            raise ValueError("invalid connected field")
        token = fields["token"].upper()
        if re.fullmatch(r"[0-9A-F]{8}", token) is None:
            raise ValueError("invalid session token")
        connected = fields["connected"] == "1"
        flags = int(fields["flags"], 10)
        uptime = int(fields["uptime_ms"], 10)
        # Bit 4 advertises speed control in newer firmware. It does not
        # replace the PWM capability or inhibited-state checks below.
        if not 0 <= flags <= 31 or not 0 <= uptime <= 0xFFFFFFFF:
            raise ValueError("invalid status range")
        if connected and token == "00000000":
            raise ValueError("connected with a zero session token")
        return Info(connected, token, flags, uptime)
    except (KeyError, ValueError) as exc:
        raise BenchError(f"Invalid INFO reply: {exc}") from exc


def validate_parameters(wheel: int | tuple[int, ...] | None, pwm: int, seconds: float,
                        *, extended: bool = False) -> None:
    if isinstance(wheel, tuple):
        if (len(wheel) not in (2, 3) or any(w not in range(1, 5) for w in wheel) or
                len(set(wheel)) != len(wheel)):
            raise ValueError("selection must contain two or three distinct wheels in 1..4")
    elif wheel is not None and wheel not in range(1, 5):
        raise ValueError("wheel must be 1..4")
    if not -250 <= pwm <= 250 or pwm == 0:
        raise ValueError("pwm must be nonzero and within -250..250 permille")
    limit = 10 if extended else 5
    if not 0 < seconds <= limit:
        raise ValueError(f"seconds must be greater than zero and at most {limit}")


class _Bench:
    def __init__(self, serial_port, wheel: int | tuple[int, ...] | None, pwm: int, seconds: float,
                 clock: Callable[[], float], log: Callable[[str], None],
                 expected_firmware: str = FIRMWARE, extended: bool = False):
        validate_parameters(wheel, pwm, seconds, extended=extended)
        if re.fullmatch(r"[A-Za-z0-9_.-]+", expected_firmware) is None:
            raise ValueError("expected firmware must be an exact nonempty identifier")
        self.expected_firmware = expected_firmware
        self.serial = serial_port
        self.serial.timeout = READ_TIMEOUT
        self.serial.write_timeout = WRITE_TIMEOUT
        self.wheel, self.pwm, self.seconds = wheel, pwm, seconds
        self.clock, self.log = clock, log
        self.pending = bytearray()
        self.last_info: Info | None = None
        self.last_info_time = float("-inf")
        self.pwm_writes = 0

    def _send(self, command: str, deadline: float | None = None) -> None:
        payload = (command + "\n").encode("ascii")
        remaining = WRITE_TIMEOUT if deadline is None else deadline - self.clock()
        if remaining <= 0:
            raise BenchError("Serial write deadline reached")
        self.serial.write_timeout = min(WRITE_TIMEOUT, remaining)
        # One bounded write only: never flush indefinitely or retry any PWM.
        written = self.serial.write(payload)
        if written != len(payload):
            raise BenchError("Short serial write; command will not be retried")
        self.log("TX " + command)

    def _read(self, deadline: float | None = None) -> list[Info]:
        remaining = READ_TIMEOUT if deadline is None else deadline - self.clock()
        if remaining <= 0:
            return []
        self.serial.timeout = min(READ_TIMEOUT, remaining)
        self.pending.extend(self.serial.read(512))
        if len(self.pending) > MAX_PENDING:
            raise BenchError("Serial reply buffer exceeded its bound")
        results = []
        while b"\n" in self.pending:
            raw, _, tail = self.pending.partition(b"\n")
            self.pending[:] = tail
            line = raw.decode("ascii", errors="replace").strip()
            if not line:
                continue
            self.log("RX " + line)
            if line.startswith("ERR"):
                raise BenchError("Console rejected an operation: " + line)
            if not line.startswith("INFO "):
                continue
            info = parse_info(line, self.expected_firmware)
            if self.last_info is not None:
                delta = (info.uptime_ms - self.last_info.uptime_ms) & 0xFFFFFFFF
                if delta >= 0x80000000:
                    raise BenchError("ESP32 uptime moved backwards; refusing to resume")
                if delta == 0:
                    # Duplicate buffered INFO does not refresh the liveness lease.
                    if info != self.last_info:
                        # Propagate a changed connection/token immediately, even
                        # if two console replies were generated in one millisecond.
                        self.last_info = info
                        results.append(info)
                    continue
            self.last_info = info
            self.last_info_time = self.clock()
            results.append(info)
        return results

    @staticmethod
    def _ready(info: Info) -> bool:
        return info.connected and (info.flags & (PWM_SUPPORTED | OUTPUT_INHIBITED)) == (
            PWM_SUPPORTED | OUTPUT_INHIBITED)

    def _prepare(self) -> str:
        end = self.clock() + PREPARE_TIMEOUT
        self._send("STOP", end)
        self._send("INFO", end)
        next_probe = self.clock() + PROBE_PERIOD
        baseline = None
        candidate = None
        candidate_since = 0.0
        while self.clock() < end:
            for info in self._read(end):
                if self.clock() >= end:
                    break
                if info.connected and baseline is None:
                    baseline = info.token
                    # STOP rotates the STM32 session. This second, explicit STOP
                    # gives us a known old token, eliminating stale initial status.
                    self._send("STOP", end)
                    candidate = None
                elif self._ready(info) and baseline is not None and info.token != baseline:
                    if candidate != info.token:
                        candidate, candidate_since = info.token, self.clock()
                    elif (info == self.last_info and
                          self.clock() - candidate_since >= PROBE_PERIOD * 0.9):
                        # _read() can return several INFO lines. Never arm from
                        # an earlier line if a later one already revoked readiness.
                        self.log(f"Ready: new stable session {candidate}; controller reports inhibited")
                        return candidate
                else:
                    candidate = None
            now = self.clock()
            if now >= end:
                break
            if now >= next_probe:
                self._send("INFO", end)
                next_probe = self.clock() + PROBE_PERIOD
        raise BenchError("Preparation timed out: no new stable, inhibited PWM-capable session")

    def _drive(self, token: str) -> None:
        selected = (1, 2, 3, 4) if self.wheel is None else (
            self.wheel if isinstance(self.wheel, tuple) else (self.wheel,))
        values = [self.pwm if wheel in selected else 0 for wheel in range(1, 5)]
        command = "PWM " + token + " " + " ".join(map(str, values))
        end = self.clock() + self.seconds
        next_pwm = self.clock()
        next_probe = self.clock()
        while self.clock() < end:
            # Inspect already-arrived failures before issuing the next command.
            for info in self._read(end):
                if not info.connected:
                    raise BenchError("Board link disconnected; no automatic restart")
                if info.token != token:
                    raise BenchError("Session token changed; no automatic restart")
                if not info.flags & PWM_SUPPORTED:
                    raise BenchError("Controller no longer advertises PWM support")
            now = self.clock()
            if now >= end:
                break
            if now - self.last_info_time >= STATUS_TIMEOUT:
                raise BenchError("No fresh INFO status for 300 ms")
            if now >= next_pwm:
                if self.serial.out_waiting:
                    raise BenchError("Serial output remains queued; refusing to queue more PWM")
                self._send(command, end)
                self.pwm_writes += 1
                # Skip missed slots instead of catching up with a burst of PWM.
                next_pwm = self.clock() + PWM_PERIOD
            now = self.clock()
            if now >= end:
                break
            if now >= next_probe:
                # Probe after PWM so freshly written INFO/ENC bytes are not
                # mistaken for an old, congested output queue in the same slot.
                self._send("INFO", end)
                if self.clock() >= end:
                    break
                self._send("ENC", end)
                next_probe = self.clock() + PROBE_PERIOD

    def _stop_confirm(self) -> bool:
        end = self.clock() + STOP_TIMEOUT
        before = self.last_info.token if self.last_info is not None else None
        candidate = None
        candidate_since = 0.0
        try:
            # Always attempt this before any read, reset, close, or diagnostic log.
            self._send("STOP", end)
            next_probe = self.clock()
            while self.clock() < end:
                if self.clock() >= next_probe:
                    self._send("INFO", end)
                    next_probe = self.clock() + PROBE_PERIOD
                for info in self._read(end):
                    if self.clock() >= end:
                        break
                    if self._ready(info) and (before is None or info.token != before):
                        if candidate != info.token:
                            candidate, candidate_since = info.token, self.clock()
                        elif (info == self.last_info and
                              self.clock() - candidate_since >= PROBE_PERIOD * 0.9):
                            self.log("STOP confirmed: controller reports inhibited in a new session")
                            return True
                    else:
                        candidate = None
        except (Exception, KeyboardInterrupt) as exc:
            self.log(f"STOP confirmation unavailable: {exc}")
            return False
        self.log("STOP sent; controller inhibition was not confirmed within 1 s")
        return False

    def run(self) -> BenchResult:
        token = ""
        confirmed = False
        try:
            token = self._prepare()
            self._drive(token)
        finally:
            # This also runs for Ctrl+C, serial faults, token changes and timeout.
            confirmed = self._stop_confirm()
        if not confirmed:
            raise BenchError("Command interval ended, but STOP status was not confirmed")
        self.log("Bounded command interval ended; QUEUED does not verify physical motor movement")
        return BenchResult(token, self.pwm_writes, confirmed)


def run_bench(serial_port, *, wheel: int | tuple[int, ...] | None, pwm: int, seconds: float = 2.0,
              clock: Callable[[], float] = time.monotonic,
              log: Callable[[str], None] = print,
              expected_firmware: str = FIRMWARE, extended: bool = False) -> BenchResult:
    """Run on an open port; a tuple selects two/three wheels, None all four.

    Equal electrical polarity does not imply all wheels propel the car forward.
    An explicit extended=True raises only the duration cap from 5 to 10 seconds.
    """
    return _Bench(serial_port, wheel, pwm, seconds, clock, log, expected_firmware, extended).run()


def _open_serial(port: str):
    import serial  # --help and fake-port tests do not require pyserial.

    connection = serial.Serial(port=None, baudrate=115200,
                               timeout=READ_TIMEOUT, write_timeout=WRITE_TIMEOUT)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    try:
        connection.open()
    except BaseException:
        connection.close()
        raise
    return connection


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 USB serial port, for example COM8")
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--wheel", type=int, choices=range(1, 5))
    target.add_argument("--pair", type=int, nargs=2, choices=range(1, 5),
                        help="two distinct wheel numbers at equal electrical duty")
    target.add_argument("--triple", type=int, nargs=3, choices=range(1, 5),
                        help="three distinct wheel numbers at equal electrical duty")
    target.add_argument("--all-wheels", action="store_true",
                        help="all four at equal electrical duty; keep all wheels off the ground")
    parser.add_argument("--pwm", required=True, type=int, help="nonzero signed duty, -250..250 permille")
    parser.add_argument("--seconds", type=float, default=2.0,
                        help="command interval in seconds, default 2; maximum 5 unless --extended")
    parser.add_argument("--extended", action="store_true",
                        help="explicitly allow a supervised command interval of up to 10 seconds")
    parser.add_argument("--expected-firmware", default=FIRMWARE,
                        help="exact verified INFO firmware identifier; does not bypass capability checks")
    args = parser.parse_args(argv)
    selected = tuple(args.pair) if args.pair is not None else args.wheel
    if args.triple is not None:
        selected = tuple(args.triple)
    try:
        validate_parameters(selected, args.pwm, args.seconds, extended=args.extended)
    except ValueError as exc:
        parser.error(str(exc))
    connection = None
    try:
        connection = _open_serial(args.port)
        run_bench(connection, wheel=selected, pwm=args.pwm, seconds=args.seconds,
                  expected_firmware=args.expected_firmware, extended=args.extended)
        return 0
    except KeyboardInterrupt:
        print("Bench interrupted; STOP was attempted, with no automatic restart", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"Bench aborted: {exc}", file=sys.stderr)
        return 1
    finally:
        if connection is not None:
            connection.close()


if __name__ == "__main__":
    raise SystemExit(main())
