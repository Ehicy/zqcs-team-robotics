"""Read M4/first-fault/reset diagnostics through ST-Link without clearing faults.

Requires the exact ELF on the target. HOTPLUG reads only: no erase, flash,
reset, RAM writes, ARM or motor commands. The running snapshot is not atomic.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

FIELDS = {
    'board_link_reset_cause': 'I', 'board_link_transport_errors': 'I',
    'board_link_outputs_inhibited': 'B', 'milliseconds': 'I',
    'm4_raw_irqs': 'I', 'm4_overrun': 'B', 'm4_started': 'B',
    'm4_window_ms': 'I', 'm4_fault_ms': 'I', 'm4_fault_window_ms': 'I',
    'm4_fault_cycles': 'I', 'm4_fault_pins': 'I',
    'm4_filter.count': 'I', 'm4_filter.accepted': 'I',
    'm4_filter.short_pulses': 'I', 'm4_filter.duplicate_irqs': 'I',
    'm4_filter.pending': 'B', 'm4_filter.stable_a': 'B',
    'speeds.fault': 'B', 'speeds.fault_encoder_mask': 'B',
    'speeds.fault_sample_ms': 'I', 'speeds.fault_sample_dt': 'I',
    'speeds.fault_raw_delta': '4i', 'speeds.measured_mm_s': '4h',
    'speeds.last_counts': '4I',
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cli', type=Path, required=True)
    p.add_argument('--gdb', type=Path, required=True)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--stlink', required=True)
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    commands = [str(args.gdb), '-batch', '-ex', 'file '+args.elf.as_posix()]
    for field in FIELDS:
        commands += ['-ex', f'printf "FIELD {field} %u %u\\n", (unsigned)&{field}, (unsigned)sizeof({field})']
    output = subprocess.check_output(commands, text=True, errors='replace', timeout=20)
    layout = {}
    for field, address, size in re.findall(r'^FIELD (\S+) (\d+) (\d+)$', output, re.M):
        assert int(size) == struct.calcsize('<'+FIELDS[field]), 'Unexpected field size'
        layout[field] = (int(address), int(size))
    assert len(layout) == len(FIELDS), 'Incomplete ELF layout'
    report = {'elf': str(args.elf), 'stlink': args.stlink, 'operations': [],
              'atomic_snapshot': False, 'motor_commands_sent': False, 'writes': False}

    def call(*extra):
        cmd = [str(args.cli), '-c', 'port=SWD', 'sn='+args.stlink,
               'freq=100', 'mode=HOTPLUG', *map(str, extra), '-run']
        r = subprocess.run(cmd, text=True, errors='replace', stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=25)
        report['operations'].append({'args': cmd[1:], 'output': r.stdout, 'returncode': r.returncode})
        if r.returncode or 'Error:' in r.stdout:
            raise RuntimeError('SWD read failed; see report')
        return r.stdout

    def read(address, size):
        raw = bytearray()
        for line in call('-r32', hex(address), size).splitlines():
            match = re.fullmatch(r'0x[0-9a-fA-F]+\s*:\s*([0-9a-fA-F ]+)', line.strip())
            if match:
                for word in match[1].split():
                    raw.extend(int(word, 16).to_bytes(4, 'little'))
        assert len(raw) == size, 'Incomplete SWD read'
        return raw

    try:
        with tempfile.TemporaryDirectory(prefix='crtc-m4-read-') as folder:
            expected_path = Path(folder)/'expected.bin'
            objcopy = args.gdb.with_name(args.gdb.name.replace('gdb', 'objcopy'))
            assert objcopy != args.gdb, 'Use the Arm GNU gdb executable'
            subprocess.run([str(objcopy), '-O', 'binary', str(args.elf), str(expected_path)],
                           check=True, timeout=20)
            expected = expected_path.read_bytes()
            assert 0 < len(expected) <= 65536, 'Image outside STM32F103C8 flash size'
            path = Path(folder)/'flash.bin'
            call('-u', '0x08000000', len(expected), path)
            assert path.read_bytes() == expected, 'Flash differs: refuse ELF-specific RAM interpretation'
        report['flash_sha256'] = hashlib.sha256(expected).hexdigest()
        begin = min(a for a, _ in layout.values()) & ~3
        end = (max(a+s for a, s in layout.values()) + 3) & ~3
        assert 0x20000000 <= begin < end <= 0x20005000, 'Fields outside STM32F103C8 SRAM'
        raw = read(begin, end-begin)
        report['fields'] = {}
        for field, (address, size) in layout.items():
            values = struct.unpack('<'+FIELDS[field], raw[address-begin:address-begin+size])
            report['fields'][field] = values[0] if len(values) == 1 else values
        report['gpio_b_idr_odr'] = struct.unpack('<2I', read(0x40010C08, 8))
        report['rcc_csr_current'] = struct.unpack('<I', read(0x40021024, 4))[0]
        print(json.dumps(report['fields'], indent=2))
    except BaseException as exc:
        report['error'] = str(exc)
        raise
    finally:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
