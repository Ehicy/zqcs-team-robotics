"""No-hardware tests for the bounded USB bench runner."""

import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import types
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "motor_bench.py"
SPEC = importlib.util.spec_from_file_location("crtc_motor_bench", MODULE_PATH)
bench = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bench
SPEC.loader.exec_module(bench)


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class FakeSerial:
    """Replies evolve only through an injected clock; no serial module is used."""

    def __init__(self, clock, *, connected=True, supported=True, mode="normal"):
        self.clock = clock
        self.connected = connected
        self.supported = supported
        self.mode = mode
        self.timeout = self.write_timeout = None
        self.out_waiting = 0
        self.token = 0x10203040
        self.inhibited = True
        self.commands = []
        self.buffer = bytearray()
        self.pwm_count = 0
        self.interrupted = False
        self.running = False
        self.stop_count = 0
        self.info_since_stop = 0

    def _reply(self, line):
        self.buffer.extend((line + "\n").encode("ascii"))

    def _info(self):
        flags = (1 if self.connected else 0) | (8 if self.supported else 0)
        flags |= 4 if self.inhibited else 0
        token = self.token if self.connected else 0
        return (f"INFO firmware={bench.FIRMWARE} transport=1 connected={int(self.connected)} "
                f"token={token:08X} flags={flags} uptime_ms={int(self.clock() * 1000)}")

    def write(self, payload):
        command = payload.decode("ascii").strip()
        self.commands.append((self.clock(), command))
        if command == "STOP":
            self.stop_count += 1
            self.info_since_stop = 0
            self.token += 1
            self.inhibited = True
            self.running = False
            self._reply("QUEUED STOP")
        elif command == "INFO":
            self.info_since_stop += 1
            if not (self.running and self.mode == "stale"):
                self._reply(self._info())
                if (self.mode == "prepare_disconnect" and self.stop_count == 2 and
                        self.info_since_stop == 2):
                    self.connected = False
                    self._reply(self._info())
        elif command == "ENC":
            self._reply("ENC mask=15 age_ms=5 counts=0,0,0,0")
        elif command.startswith("PWM "):
            self.pwm_count += 1
            self.running = True
            self.inhibited = False
            self._reply("QUEUED PWM ttl_ms=200")
            if self.mode == "token_change" and self.pwm_count == 2:
                self.token += 1
                self._reply(self._info())
            elif self.mode == "error":
                self._reply("ERR PWM LINK_OR_UNSUPPORTED")
            elif self.mode == "disconnect":
                self.connected = False
                self._reply(self._info())
        return len(payload)

    def read(self, count):
        self.clock.now += 0.010
        if self.mode == "interrupt" and self.pwm_count and not self.interrupted:
            self.interrupted = True
            raise KeyboardInterrupt()
        chunk = bytes(self.buffer[:count])
        del self.buffer[:count]
        return chunk


class MotorBenchTests(unittest.TestCase):
    def run_fake(self, serial, seconds=0.25):
        return bench.run_bench(serial, wheel=3, pwm=-250, seconds=seconds,
                               clock=serial.clock, log=lambda line: None)

    def assert_final_stop(self, serial):
        pwm_indices = [i for i, (_, command) in enumerate(serial.commands)
                       if command.startswith("PWM ")]
        stop_indices = [i for i, (_, command) in enumerate(serial.commands) if command == "STOP"]
        self.assertTrue(stop_indices)
        if pwm_indices:
            self.assertGreater(stop_indices[-1], pwm_indices[-1])

    def test_disconnected_preparation_never_sends_pwm(self):
        serial = FakeSerial(Clock(), connected=False)
        with self.assertRaisesRegex(bench.BenchError, "Preparation timed out"):
            self.run_fake(serial)
        self.assertEqual(serial.pwm_count, 0)
        self.assertLessEqual(serial.clock(), 4.1)
        self.assert_final_stop(serial)

    def test_missing_capability_never_sends_pwm(self):
        serial = FakeSerial(Clock(), supported=False)
        with self.assertRaisesRegex(bench.BenchError, "Preparation timed out"):
            self.run_fake(serial)
        self.assertEqual(serial.pwm_count, 0)
        self.assert_final_stop(serial)

    def test_last_info_in_read_batch_can_revoke_preparation(self):
        serial = FakeSerial(Clock(), mode="prepare_disconnect")
        with self.assertRaisesRegex(bench.BenchError, "Preparation timed out"):
            self.run_fake(serial)
        self.assertEqual(serial.pwm_count, 0)
        self.assert_final_stop(serial)

    def test_token_change_stops_without_following_new_token(self):
        serial = FakeSerial(Clock(), mode="token_change")
        with self.assertRaisesRegex(bench.BenchError, "Session token changed"):
            self.run_fake(serial, seconds=1)
        commands = [command for _, command in serial.commands if command.startswith("PWM ")]
        self.assertEqual(len(commands), 2)
        self.assertEqual(len({command.split()[1] for command in commands}), 1)
        self.assert_final_stop(serial)

    def test_normal_deadline_single_wheel_and_final_stop(self):
        serial = FakeSerial(Clock())
        result = self.run_fake(serial)
        sent = [(when, command) for when, command in serial.commands if command.startswith("PWM ")]
        self.assertTrue(result.stop_confirmed)
        self.assertGreaterEqual(len(sent), 3)
        self.assertLessEqual(len(sent), 5)
        for _, command in sent:
            self.assertEqual(command.split()[2:], ["0", "0", "-250", "0"])
            self.assertEqual(command.split()[1], result.token)
        for (previous, _), (current, _) in zip(sent, sent[1:]):
            self.assertGreaterEqual(current - previous, 0.050 - 1e-9)
        self.assertLess(sent[-1][0] - sent[0][0], 0.25)
        self.assert_final_stop(serial)

    def test_any_console_error_stops(self):
        serial = FakeSerial(Clock(), mode="error")
        with self.assertRaisesRegex(bench.BenchError, "Console rejected"):
            self.run_fake(serial, seconds=1)
        self.assertEqual(serial.pwm_count, 1)
        self.assert_final_stop(serial)

    def test_all_wheels_share_deadline_and_stop(self):
        serial = FakeSerial(Clock())
        result = bench.run_bench(serial, wheel=None, pwm=150, seconds=.6,
                                 clock=serial.clock, log=lambda line: None)
        sent = [(when, cmd) for when, cmd in serial.commands if cmd.startswith("PWM ")]
        self.assertTrue(result.stop_confirmed)
        self.assertGreater(len(sent), 1)
        self.assertLess(sent[-1][0] - sent[0][0], .6)
        for _, cmd in sent:
            self.assertEqual(cmd.split()[2:], ["150"] * 4)
        self.assert_final_stop(serial)

    def test_all_wheels_disconnect_stops_without_retry(self):
        serial = FakeSerial(Clock(), mode="disconnect")
        with self.assertRaisesRegex(bench.BenchError, "Board link disconnected"):
            bench.run_bench(serial, wheel=None, pwm=150, seconds=.6,
                            clock=serial.clock, log=lambda line: None)
        self.assertEqual(serial.pwm_count, 1)
        self.assert_final_stop(serial)

    def test_pair_only_drives_selected_wheels_and_stops(self):
        for pair, values in (((1, 2), ["150", "150", "0", "0"]),
                             ((3, 4), ["0", "0", "150", "150"])):
            with self.subTest(pair=pair):
                serial = FakeSerial(Clock())
                result = bench.run_bench(serial, wheel=pair, pwm=150, seconds=.6,
                                         clock=serial.clock, log=lambda line: None)
                sent = [(when, cmd) for when, cmd in serial.commands if cmd.startswith("PWM ")]
                self.assertTrue(result.stop_confirmed)
                self.assertGreater(len(sent), 1)
                self.assertLess(sent[-1][0] - sent[0][0], .6)
                self.assertTrue(all(cmd.split()[2:] == values for _, cmd in sent))
                self.assert_final_stop(serial)

    def test_pair_token_change_stops_without_retry(self):
        serial = FakeSerial(Clock(), mode="token_change")
        with self.assertRaisesRegex(bench.BenchError, "Session token changed"):
            bench.run_bench(serial, wheel=(1, 2), pwm=150, seconds=.6,
                            clock=serial.clock, log=lambda line: None)
        self.assertEqual(serial.pwm_count, 2)
        self.assert_final_stop(serial)

    def test_invalid_selection_rejected_before_any_write(self):
        for pair in ((1, 1), (0, 2), (1, 5), (1,), (1, 2, 3, 4),
                     (1, 2, 2), (0, 2, 3), (1, 2, 5)):
            serial = FakeSerial(Clock())
            with self.assertRaises(ValueError):
                bench.run_bench(serial, wheel=pair, pwm=150, seconds=.6,
                                clock=serial.clock, log=lambda line: None)
            self.assertEqual(serial.commands, [])

    def test_triple_only_drives_selected_wheels_and_stops(self):
        serial = FakeSerial(Clock())
        result = bench.run_bench(serial, wheel=(1, 2, 3), pwm=150, seconds=5,
                                 clock=serial.clock, log=lambda line: None)
        sent = [(when, cmd) for when, cmd in serial.commands if cmd.startswith("PWM ")]
        self.assertTrue(result.stop_confirmed)
        self.assertGreater(len(sent), 1)
        self.assertLess(sent[-1][0] - sent[0][0], 5)
        self.assertTrue(all(cmd.split()[2:] == ["150", "150", "150", "0"] for _, cmd in sent))
        self.assert_final_stop(serial)

    def test_triple_disconnect_stops_without_retry(self):
        serial = FakeSerial(Clock(), mode="disconnect")
        with self.assertRaisesRegex(bench.BenchError, "Board link disconnected"):
            bench.run_bench(serial, wheel=(1, 2, 3), pwm=150, seconds=5,
                            clock=serial.clock, log=lambda line: None)
        self.assertEqual(serial.pwm_count, 1)
        self.assert_final_stop(serial)

    def test_cli_triple_passes_exact_selection(self):
        connection = mock.Mock()
        with mock.patch.object(bench, "_open_serial", return_value=connection):
            with mock.patch.object(bench, "run_bench") as runner:
                self.assertEqual(bench.main(["--port", "FAKE", "--triple", "1", "2", "3",
                                             "--pwm", "150", "--seconds", "5"]), 0)
        self.assertEqual(runner.call_args.kwargs["wheel"], (1, 2, 3))
        connection.close.assert_called_once()

    def test_cli_requires_one_explicit_target(self):
        base = ["--port", "FAKE", "--pwm", "150"]
        for extra in ([], ["--wheel", "1", "--all-wheels"],
                      ["--pair", "1", "2", "--all-wheels"],
                      ["--pair", "1", "2", "--wheel", "1"], ["--pair", "1", "1"],
                      ["--triple", "1", "2", "3", "--all-wheels"],
                      ["--triple", "1", "2", "2"], ["--triple", "1", "2"]):
            with mock.patch.object(bench, "_open_serial") as opener:
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as result:
                        bench.main(base + extra)
                self.assertEqual(result.exception.code, 2)
                opener.assert_not_called()

    def test_disconnect_during_run_stops(self):
        serial = FakeSerial(Clock(), mode="disconnect")
        with self.assertRaisesRegex(bench.BenchError, "Board link disconnected"):
            self.run_fake(serial, seconds=1)
        self.assertEqual(serial.pwm_count, 1)
        self.assert_final_stop(serial)

    def test_enc_and_queued_replies_do_not_refresh_info_timeout(self):
        serial = FakeSerial(Clock(), mode="stale")
        with self.assertRaisesRegex(bench.BenchError, "No fresh INFO"):
            self.run_fake(serial, seconds=2)
        pwm = [when for when, command in serial.commands if command.startswith("PWM ")]
        # The final queued INFO is read one READ_TIMEOUT after the first PWM.
        self.assertLessEqual(pwm[-1] - pwm[0], 0.300 + bench.READ_TIMEOUT + 1e-9)
        self.assert_final_stop(serial)

    def test_ctrl_c_always_attempts_final_stop(self):
        serial = FakeSerial(Clock(), mode="interrupt")
        with self.assertRaises(KeyboardInterrupt):
            self.run_fake(serial, seconds=1)
        self.assertEqual(serial.pwm_count, 1)
        self.assert_final_stop(serial)

    def test_help_never_opens_a_port(self):
        with mock.patch.object(bench, "_open_serial") as opener:
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as result:
                    bench.main(["--help"])
        self.assertEqual(result.exception.code, 0)
        opener.assert_not_called()

    def test_parameter_bounds_reject_before_any_write(self):
        for wheel, pwm, seconds in ((0, 100, 2), (5, 100, 2), (1, 0, 2),
                                    (1, 251, 2), (1, -251, 2), (1, 50, 0),
                                    (1, 50, 5.01), (1, 50, float("nan"))):
            with self.subTest(wheel=wheel, pwm=pwm, seconds=seconds):
                serial = FakeSerial(Clock())
                with self.assertRaises(ValueError):
                    bench.run_bench(serial, wheel=wheel, pwm=pwm, seconds=seconds,
                                    clock=serial.clock, log=lambda line: None)
                self.assertEqual(serial.commands, [])

    def test_new_firmware_requires_explicit_exact_identity(self):
        line = ("INFO firmware=crtc-lower-pid-v2 transport=1 connected=1 "
                "token=10203040 flags=31 uptime_ms=123")
        with self.assertRaisesRegex(bench.BenchError, "unexpected firmware"):
            bench.parse_info(line)
        info = bench.parse_info(line, "crtc-lower-pid-v2")
        self.assertTrue(bench._Bench._ready(info))
        for flags in (23, 27):  # Missing PWM support or missing inhibition.
            info = bench.parse_info(line.replace("flags=31", f"flags={flags}"),
                                    "crtc-lower-pid-v2")
            self.assertFalse(bench._Bench._ready(info))
        with self.assertRaisesRegex(bench.BenchError, "invalid status range"):
            bench.parse_info(line.replace("flags=31", "flags=63"), "crtc-lower-pid-v2")

    def test_extended_ten_second_window_and_stop(self):
        serial = FakeSerial(Clock())
        result = bench.run_bench(serial, wheel=None, pwm=150, seconds=10, extended=True,
                                 clock=serial.clock, log=lambda line: None)
        sent = [(when, cmd) for when, cmd in serial.commands if cmd.startswith("PWM ")]
        self.assertTrue(result.stop_confirmed)
        self.assertGreater(sent[-1][0] - sent[0][0], 9.8)
        self.assertLess(sent[-1][0] - sent[0][0], 10)
        self.assertTrue(all(cmd.split()[2:] == ["150"] * 4 for _, cmd in sent))
        self.assert_final_stop(serial)

    def test_extended_still_rejects_out_of_bounds_before_write(self):
        for seconds in (0, 10.01, float("nan"), float("inf")):
            serial = FakeSerial(Clock())
            with self.assertRaises(ValueError):
                bench.run_bench(serial, wheel=None, pwm=150, seconds=seconds, extended=True,
                                clock=serial.clock, log=lambda line: None)
            self.assertEqual(serial.commands, [])

    def test_extended_disconnect_does_not_wait_out_interval(self):
        serial = FakeSerial(Clock(), mode="disconnect")
        with self.assertRaisesRegex(bench.BenchError, "Board link disconnected"):
            bench.run_bench(serial, wheel=None, pwm=150, seconds=10, extended=True,
                            clock=serial.clock, log=lambda line: None)
        self.assertEqual(serial.pwm_count, 1)
        self.assert_final_stop(serial)

    def test_cli_extended_is_explicit(self):
        base = ["--port", "FAKE", "--all-wheels", "--pwm", "150", "--seconds", "10"]
        with mock.patch.object(bench, "_open_serial") as opener:
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    bench.main(base)
            opener.assert_not_called()
        connection = mock.Mock()
        with mock.patch.object(bench, "_open_serial", return_value=connection):
            with mock.patch.object(bench, "run_bench") as runner:
                self.assertEqual(bench.main(base + ["--extended"]), 0)
        self.assertTrue(runner.call_args.kwargs["extended"])
        self.assertEqual(runner.call_args.kwargs["seconds"], 10)
        connection.close.assert_called_once()

    def test_wrong_firmware_never_sends_pwm(self):
        serial = FakeSerial(Clock())
        with self.assertRaisesRegex(bench.BenchError, "unexpected firmware"):
            bench.run_bench(serial, wheel=1, pwm=100, seconds=.25,
                            clock=serial.clock, log=lambda line: None,
                            expected_firmware="different-firmware")
        self.assertEqual(serial.pwm_count, 0)
        self.assert_final_stop(serial)

    def test_dtr_rts_are_false_before_open(self):
        events = []

        class Port:
            def __init__(self, **kwargs):
                events.append(("construct", kwargs))
                self.dtr = self.rts = True

            def open(self):
                events.append(("open", self.dtr, self.rts, self.port))

            def close(self):
                events.append(("close",))

        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=Port)}):
            bench._open_serial("FAKE")
        self.assertEqual(events[-1], ("open", False, False, "FAKE"))
        self.assertIsNone(events[0][1]["port"])
        self.assertEqual(events[0][1]["baudrate"], 115200)
        self.assertLessEqual(events[0][1]["timeout"], 0.050)
        self.assertLessEqual(events[0][1]["write_timeout"], 0.050)


if __name__ == "__main__":
    unittest.main()
