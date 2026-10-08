"""Test slave commands through the existing master's USB-to-USART2 bridge."""
import argparse
import json
from pathlib import Path
import re
import sys
import time
import serial
from host_test import candidates, exchange, identify, open_board, receive, select_port


class SlaveLink:
    def __init__(self, master):
        self.master = master
        self.sequence = time.monotonic_ns() & 0xFFFFFFFF

    def request(self, command, expected=None):
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF or 1
        text = f'UART REQ {self.sequence:08X} {command}'
        if len(text.encode('ascii')) > 127:
            raise ValueError('Command exceeds the original master input limit')
        exchange(self.master, text, 'OK UART QUEUED')
        raw = receive(self.master)
        prefix = f'UART_RX RSP {self.sequence:08X} '
        if not raw.startswith(prefix):
            raise ValueError(f'Wrong slave reply or sequence: {raw!r}')
        reply = raw[len(prefix):]
        if expected is not None and reply != expected:
            raise ValueError(f'{command!r}: expected {expected!r}, got {reply!r}')
        return reply


def counter(info, name):
    match = re.search(r'(?:^| )' + re.escape(name) + r'=(\d+)(?: |$)', info)
    if not match:
        raise ValueError(f'Missing {name} counter: {info}')
    return int(match.group(1))


def identify_slave(link, master_uid, expected_uid):
    reply = link.request('INFO')
    if not reply.startswith('INFO firmware=usb-slave-test-v1 role=slave '):
        raise ValueError('Reply did not identify the slave firmware: ' + reply)
    match = re.search(r' uid=([0-9A-F]{24})(?: |$)', reply)
    if not match:
        raise ValueError('Slave reply has no UID: ' + reply)
    uid = match.group(1)
    if uid == master_uid:
        raise ValueError('Slave UID equals master UID; check that two different boards are connected')
    if expected_uid and uid != expected_uid.upper():
        raise ValueError('Wrong slave UID: ' + uid)
    return uid, reply


def check_slave(link):
    started = time.monotonic()
    for i in range(100):
        link.request('PING', 'PONG')
        payload = f'slave_roundtrip_{i:03d}'
        link.request('ECHO ' + payload, 'ECHO ' + payload)
    link.request('LED ON', 'OK LED 1')
    if not link.request('INFO').endswith(' led=1'):
        raise ValueError('Slave did not retain LED ON state')
    link.request('LED OFF', 'OK LED 0')
    if not link.request('INFO').endswith(' led=0'):
        raise ValueError('Slave did not retain LED OFF state')
    for expected in ['OK LED 1', 'OK LED 0', 'OK LED 1']:
        link.request('LED TOGGLE', expected)
    link.request('LED AUTO', 'OK LED AUTO')
    link.request('UNKNOWN', 'ERR COMMAND')
    # These must be ignored as malformed requests; no automatic response loops.
    exchange(link.master, 'UART RSP FFFFFFFF PONG', 'OK UART QUEUED')
    exchange(link.master, 'UART REQ BAD PING', 'OK UART QUEUED')
    exchange(link.master, 'UART REQ 00000000 PING', 'OK UART QUEUED')
    link.request('PING', 'PONG')
    return {'ping_roundtrips': 100, 'echo_roundtrips': 100, 'led_commands': 'PASS',
            'invalid_frame_recovery': 'PASS', 'seconds': round(time.monotonic() - started, 3)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='USB COM port of the master (currently COM3)')
    parser.add_argument('--master-uid', help='Optional expected master UID')
    parser.add_argument('--slave-uid', help='Optional expected slave UID')
    parser.add_argument('--command', help='One slave command, e.g. "LED TOGGLE"')
    parser.add_argument('--report', type=Path, help='Save results as JSON')
    args = parser.parse_args()
    selected = select_port(candidates(), args.port)
    result = {'port': selected.device, 'timestamp': time.strftime('%Y-%m-%dT%H:%M:%S%z')}
    with open_board(selected) as master:
        master_uid, result['master_info_before'] = identify(master)
        result['master_uid'] = master_uid
        if 'firmware=usb-board-test-v1 ' not in result['master_info_before']:
            raise ValueError('Select the bridge/master USB port, not the slave USB port')
        if args.master_uid and master_uid != args.master_uid.upper():
            raise ValueError('Wrong master UID: ' + master_uid)
        link = SlaveLink(master)
        result['slave_uid'], result['slave_info_before'] = identify_slave(
            link, master_uid, args.slave_uid)
        if args.command:
            result['reply'] = link.request(args.command)
            if result['reply'].startswith('ERR '):
                raise ValueError('Slave rejected command: ' + result['reply'])
        else:
            result['checks'] = check_slave(link)
        result['slave_info_after'] = link.request('INFO')
        result['master_info_after'] = exchange(master, 'INFO')
        # Counters accumulate since boot; hot-plugging can leave earlier errors.
        # Preserve the before/after readings and require no new errors in this run.
        result['new_errors'] = {
            'slave_uart': counter(result['slave_info_after'], 'uart_errors') -
                          counter(result['slave_info_before'], 'uart_errors'),
            'master_uart': counter(result['master_info_after'], 'uart_errors') -
                           counter(result['master_info_before'], 'uart_errors'),
            'master_usb': counter(result['master_info_after'], 'usb_drops') -
                          counter(result['master_info_before'], 'usb_drops')}
        if any(result['new_errors'].values()):
            raise ValueError('Error counters changed during test: ' + str(result['new_errors']))
        result['status'] = 'PASS'
    output = json.dumps(result, indent=2, ensure_ascii=False) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output, encoding='utf-8')
    print(output, end='')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, TimeoutError, serial.SerialException, OSError) as exc:
        print('FAIL: ' + str(exc), file=sys.stderr)
        raise SystemExit(1)
