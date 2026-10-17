"""Verify lower-motor watchdog diagnostics with USB + ST-Link (no drive input).

Requires a freshly built/flashed matching ELF/BIN. --inject-fault writes only
watchdog state in RAM, tests fail-closed diagnostics, then software-resets.
It never writes flash, option bytes or calibration, or sends ARM/DRIVE/PWM.
"""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

import serial
from serial.tools import list_ports


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--serial', required=True)
    p.add_argument('--stlink', required=True)
    p.add_argument('--cli', type=Path, required=True)
    p.add_argument('--nm', type=Path, required=True)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--inject-fault', action='store_true')
    args = p.parse_args()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    assert any(x.device == args.port and (x.serial_number or '').upper() == args.serial.upper()
               and (x.vid, x.pid) in ((0x303A, 0x1001), (0x1A86, 0x55D3))
               for x in list_ports.comports()), 'Wrong ESP USB identity'
    symbols = {}
    for line in subprocess.check_output([str(args.nm), '-n', str(args.elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    report = {'port': args.port, 'serial': args.serial, 'stlink': args.stlink,
              'elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(),
              'symbols': {n: hex(symbols[n]) for n in ('iwdg_ready', 'iwdg_started',
                          'timing_ready', 'milliseconds', 'board_link_transport_errors')},
              'phases': [], 'swd': [], 'passed': False}
    s = serial.Serial(port=None, baudrate=115200, timeout=.05, write_timeout=1)
    s.dtr = False
    s.rts = False
    s.port = args.port
    pending = bytearray()
    injected = False

    def swd(*extra):
        cmd = [str(args.cli), '-c', 'port=SWD', 'sn='+args.stlink, 'freq=100',
               'mode=HOTPLUG', *map(str, extra), '-run']
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors='replace', timeout=20)
        report['swd'].append({'args': cmd[1:], 'output': r.stdout, 'returncode': r.returncode})
        if r.returncode or 'Error:' in r.stdout:
            raise RuntimeError('ST-Link operation failed; see report')
        return r.stdout

    def read(address, size):
        output = swd('-r32', hex(address), size)
        values = []
        for line in output.splitlines():
            match = re.fullmatch(r'0x[0-9a-fA-F]+\s*:\s*([0-9a-fA-F ]+)', line.strip())
            if match:
                values.extend(int(x, 16) for x in match[1].split())
        if len(values)*4 != size:
            raise RuntimeError('Incomplete SWD register read')
        return values

    def write_byte(symbol, value):
        address = symbols[symbol]
        # CubeProgrammer's data argument is hexadecimal. A bare "1" on the
        # tested CLI returned success without injecting 0x01; verify the byte.
        swd('-w8', hex(address), f'0x{value:02X}')
        actual = (read(address & ~3, 4)[0] >> (8 * (address & 3))) & 255
        assert actual == value, 'Fault injection was not applied: '+symbol

    def request(command, prefix):
        entry = {'command': command, 'lines': []}
        report['phases'][-1]['commands'].append(entry)
        s.write((command+'\n').encode('ascii'))
        end = time.monotonic()+3
        while time.monotonic() < end:
            pending.extend(s.read(2048))
            while b'\n' in pending:
                line, _, rest = pending.partition(b'\n')
                pending[:] = rest
                line = line.decode('ascii', errors='replace').strip()
                entry['lines'].append(line)
                if line.startswith(prefix):
                    return line
                if line.startswith('ERR'):
                    raise RuntimeError(line)
        raise TimeoutError(command)

    def speed_packets(line):
        wire = bytes.fromhex(line.split('hex=', 1)[1])
        # The trace ring can begin/end with partial frames. Accept only complete
        # COBS frames with exact version/length/reserved fields and valid CRC.
        result = []
        for encoded in wire.split(b'\0')[1:-1]:
            body = bytearray()
            pos = 0
            while pos < len(encoded):
                code = encoded[pos]
                pos += 1
                if not code or pos+code-1 > len(encoded):
                    break
                body.extend(encoded[pos:pos+code-1])
                pos += code-1
                if code != 255 and pos < len(encoded):
                    body.append(0)
            else:
                if (len(body) == 34 and body[:4] == bytes((1, 12, 20, 0)) and
                        binascii.crc_hqx(body[:-2], 0xffff) == int.from_bytes(body[-2:], 'little')):
                    result.append({'token': f'{int.from_bytes(body[8:12], "little"):08X}',
                                   'fault': body[13], 'ready_mask': body[12], 'valid_mask': body[14]})
        return result

    def phase(name, expected_fault):
        phase_report = {'name': name, 'commands': [], 'samples': [], 'speed': []}
        report['phases'].append(phase_report)
        for i in range(8):
            line = request('INFO', 'INFO firmware=')
            fields = dict(x.split('=', 1) for x in line.split()[1:])
            phase_report['samples'].append(fields)
            assert fields['connected'] == '1' and int(fields['flags']) & 4
            assert int(fields['flags']) & 24 == 24, 'Not a motor+speed target'
            phase_report['speed'].extend(x for x in speed_packets(request('RX', 'RX bytes='))
                                         if x['token'] == fields['token'])
            if i < 6:
                value = 1 if i % 2 == 0 else 0
                request('LED ON' if value else 'LED OFF', f'ACK LED={value} ')
            time.sleep(.2)
        assert phase_report['speed'], 'No current-session CRC-valid SPEED telemetry'
        assert all(x['fault'] == expected_fault for x in phase_report['speed'])
        first, last = phase_report['samples'][0], phase_report['samples'][-1]
        assert len({x['token'] for x in phase_report['samples']}) == 1
        assert int(last['rx_bytes']) > int(first['rx_bytes'])
        assert all(first[k] == last[k] for k in ('parser_errors', 'uart_faults', 'rejected'))
        # The sampled pin/latch must both remain low; this is electrical
        # readback, not an assertion about mechanical standstill.
        idr, odr = read(0x40010c08, 8)
        assert not ((idr | odr) & (1 << 15)), 'STBY unexpectedly high'
        phase_report['stby_idr_odr'] = [idr, odr]
        phase_report['transport_errors'] = read(symbols['board_link_transport_errors'], 4)[0]
        phase_report['passed'] = True
        print('PASS '+name, flush=True)

    try:
        with s:
            s.reset_input_buffer()
            binary = args.elf.with_suffix('.bin').read_bytes()
            readback = args.report.with_suffix('.flash-readback.bin')
            swd('-u', '0x08000000', len(binary), readback)
            assert readback.read_bytes() == binary, 'ELF/BIN does not match board firmware'
            report['bin_sha256'] = hashlib.sha256(binary).hexdigest()
            report['readback_equal'] = True
            assert read(0x40003004, 12) == [3, 249, 0]
            phase('normal', 0)
            if args.inject_fault:
                injected = True
                write_byte('watchdog_latched', 1)
                time.sleep(1)
                phase('software-watchdog-latched', 6)
                request('STOP', 'QUEUED STOP')
                time.sleep(1)
                phase('STOP-does-not-clear-watchdog', 6)
                swd('-rst')
                injected = False
                time.sleep(1)
                # Mimic failed IWDG setup while its clock and SysTick run. An
                # existing watchdog must still be fed for diagnostic access.
                injected = True
                write_byte('iwdg_ready', 0)
                time.sleep(1)
                phase('iwdg-unavailable', 6)
                count = report['phases'][-1]['transport_errors']
                time.sleep(.5)
                phase('persistent-fault', 6)
                assert report['phases'][-1]['transport_errors'] == count
                swd('-rst')
                injected = False
                time.sleep(1)
                phase('recovered-after-reset', 0)
                assert read(0x40003004, 12) == [3, 249, 0]
            report['passed'] = True
    except Exception as exc:
        report['error'] = repr(exc)
        raise
    finally:
        if injected:
            try:
                swd('-rst')
                report['cleanup_reset'] = True
            except Exception as exc:
                report['cleanup_error'] = repr(exc)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
