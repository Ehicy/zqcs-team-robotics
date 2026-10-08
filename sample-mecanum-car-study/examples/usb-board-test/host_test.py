"""USB CDC checks, with optional two-board USART2 checks. No flashing."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    raise SystemExit('Install pyserial first: python -m pip install pyserial==3.5')


def candidates():
    return [p for p in list_ports.comports() if (p.vid, p.pid) == (0xCAFE, 0x4001)]


def select_port(ports, name):
    matches = [p for p in ports if name is None or p.device.casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError('Select exactly one F103 test port with --port; use --list to inspect.')
    return matches[0]


def open_board(port):
    conn = serial.Serial(port.device, 115200, timeout=2, write_timeout=2)
    conn.dtr = True
    time.sleep(0.2)
    conn.reset_input_buffer()
    return conn


def receive(conn):
    raw = conn.readline()
    if not raw.endswith(b'\n'):
        raise TimeoutError(f'{conn.port}: incomplete reply: {raw!r}')
    return raw.decode('ascii').rstrip('\r\n')


def exchange(conn, command, expected=None):
    conn.write(command.encode('ascii') + b'\n')
    reply = receive(conn)
    if expected is not None and reply != expected:
        raise ValueError(f'{conn.port}: {command!r}: expected {expected!r}, got {reply!r}')
    return reply


def identify(conn):
    reply = exchange(conn, 'INFO')
    if not reply.startswith(('INFO firmware=usb-board-test-v1 ',
                             'INFO firmware=usb-slave-test-v1 ')):
        raise ValueError(f'{conn.port}: unexpected firmware: {reply!r}')
    match = re.search(r' uid=([0-9A-F]{24})(?: |$)', reply)
    if not match:
        raise ValueError(f'{conn.port}: missing chip UID: {reply!r}')
    return match.group(1), reply


def usb_check(conn):
    started = time.monotonic()
    for i in range(100):
        exchange(conn, 'PING', 'PONG')
        payload = f'usb_roundtrip_{i:03d}'
        exchange(conn, 'ECHO ' + payload, 'ECHO ' + payload)
    exchange(conn, 'LED ON', 'OK LED 1')
    exchange(conn, 'LED OFF', 'OK LED 0')
    exchange(conn, 'LED TOGGLE', 'OK LED 1')
    exchange(conn, 'LED AUTO', 'OK LED AUTO')
    exchange(conn, 'UNKNOWN', 'ERR COMMAND')
    exchange(conn, 'X' * 200, 'ERR LINE TOO LONG')
    exchange(conn, 'PING', 'PONG')
    conn.write(b'ECHO bad\x00line\n')
    if receive(conn) != 'ERR LINE TOO LONG':
        raise ValueError('Invalid line was not rejected')
    exchange(conn, 'PING', 'PONG')
    return {'ping_roundtrips': 100, 'echo_roundtrips': 100,
            'led_commands': 'PASS', 'invalid_line_recovery': 'PASS',
            'seconds': round(time.monotonic() - started, 3)}


def uart_check(first, second):
    for sender, receiver, label in [(first, second, 'A_to_B'), (second, first, 'B_to_A')]:
        for i in range(10):
            payload = f'{label}_{i:03d}'
            exchange(sender, 'UART ' + payload, 'OK UART QUEUED')
            reply = receive(receiver)
            if reply != 'UART_RX ' + payload:
                raise ValueError(f'{receiver.port}: unexpected UART message: {reply!r}')
    return {'messages_A_to_B': 10, 'messages_B_to_A': 10, 'status': 'PASS'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true', help='List only the matching test boards')
    parser.add_argument('--port', help='COM port of the first board')
    parser.add_argument('--uid', help='Optional expected 24-character chip UID')
    parser.add_argument('--peer-port', help='Second board; needs PA2/PA3 crossed and common GND')
    parser.add_argument('--command', help='Send one command instead of running checks')
    parser.add_argument('--report', type=Path, help='Save test results as JSON')
    args = parser.parse_args()
    ports = candidates()
    if args.list:
        print(json.dumps([{'port': p.device, 'description': p.description,
                           'serial': p.serial_number} for p in ports], indent=2))
        return
    selected = select_port(ports, args.port)
    result = {'port': selected.device, 'timestamp': time.strftime('%Y-%m-%dT%H:%M:%S%z')}
    with open_board(selected) as first:
        uid, result['info_before'] = identify(first)
        result['uid'] = uid
        if args.uid and uid != args.uid.upper():
            raise ValueError(f'Wrong board UID: {uid}')
        if args.command:
            result['reply'] = exchange(first, args.command)
        else:
            result['usb'] = usb_check(first)
            if args.peer_port:
                if 'firmware=usb-board-test-v1 ' not in result['info_before']:
                    raise ValueError('Raw transfer test needs two bridge boards; use slave_test.py for master/slave')
                peer = select_port(ports, args.peer_port)
                if peer.device.casefold() == selected.device.casefold():
                    raise ValueError('The two ports must be different boards')
                with open_board(peer) as second:
                    peer_uid, peer_info = identify(second)
                    if 'firmware=usb-board-test-v1 ' not in peer_info:
                        raise ValueError('Peer is a slave; use slave_test.py for master/slave')
                    if uid == peer_uid:
                        raise ValueError('The two boards returned the same UID')
                    result['peer'] = {'port': peer.device, 'uid': peer_uid, 'info': peer_info}
                    result['peer_usb'] = usb_check(second)
                    result['uart'] = uart_check(first, second)
                    result['peer']['info_after'] = exchange(second, 'INFO')
            result['info_after'] = exchange(first, 'INFO')
            if 'usb_drops=0 ' not in result['info_after'] or 'uart_errors=0 ' not in result['info_after']:
                raise ValueError('Nonzero device error counters: ' + result['info_after'])
            if 'peer' in result:
                peer_after = result['peer']['info_after']
                if 'usb_drops=0 ' not in peer_after or 'uart_errors=0 ' not in peer_after:
                    raise ValueError('Nonzero peer error counters: ' + peer_after)
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
