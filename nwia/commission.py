#!/usr/bin/env python3
"""Interactive USB commissioning. Secrets stay in local prompts/device NVS."""
import argparse
import getpass
import re
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyUSB0')
    parser.add_argument('--broker', required=True, help='TLS broker DNS hostname')
    parser.add_argument('--mqtt-port', type=int, default=8883)
    parser.add_argument('--mqtt-user', required=True)
    parser.add_argument('--ssid', required=True)
    parser.add_argument('--name', default='NWIA Gateway')
    parser.add_argument('--group', default='pilot')
    parser.add_argument('--mode', choices=['all', 'channel'], default='all')
    parser.add_argument('--expected-id', help='Optional expected 12-hex gateway MAC identity')
    args = parser.parse_args()
    link = serial.Serial(args.port, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
    link.dtr = False
    link.rts = False
    time.sleep(2)

    def command(line, secret=False):
        if '\r' in line or '\n' in line or len(line.encode()) > 158:
            raise ValueError('Invalid or oversized console input')
        link.reset_input_buffer()
        link.write(line.encode() + b'\r')
        deadline = time.monotonic() + 5
        response = b''
        while time.monotonic() < deadline:
            response += link.read(4096)
            if b'  -> ' in response and response.endswith(b'\n'):
                break
        text = response.decode(errors='replace')
        if '  -> ' not in text or re.search(r'\b(err|error)\b', text, re.I):
            raise RuntimeError('USB command failed' if secret else 'USB command failed: ' + line)
        return text

    try:
        status = command('get nwia.status')
        match = re.search(r'id=([0-9a-f]{12})', status)
        if not match or (args.expected_id and match[1] != args.expected_id.lower()):
            raise RuntimeError('Expected NWIA firmware and matching gateway identity; no secrets sent')
        print('Confirmed NWIA firmware; gateway identity: ' + match[1])
        while True:
            wifi = getpass.getpass('Wi-Fi password (hidden): ')
            if 1 <= len(wifi.encode()) <= 64 and not any(c in wifi for c in '\r\n'):
                break
            print('Wi-Fi password must be 1–64 bytes with no line breaks. Try again.')
        channel = None
        if args.mode == 'channel':
            while True:
                channel = getpass.getpass('MeshCore chat-channel key, 32 hex characters (hidden): ')
                if re.fullmatch('[0-9a-fA-F]{32}', channel):
                    break
                print('The channel key must contain exactly 32 hexadecimal characters. Try again.')
        print('This NWIA build supports 12–16 admin-password characters. Standard companion firmware sends at most 15.')
        print('For normal app login, use 12–15 printable ASCII characters. 16 requires compatible companion firmware.')
        while True:
            admin = getpass.getpass('New repeater admin password (hidden): ')
            if not 12 <= len(admin) <= 16:
                print('Admin password must be 12–16 characters. Try again.')
                continue
            if not admin.isascii() or any(ord(c) < 32 or ord(c) == 127 for c in admin):
                print('Use printable ASCII characters, with no line breaks or control characters. Try again.')
                continue
            confirm = getpass.getpass('Repeat admin password (hidden): ')
            if admin != confirm:
                print('The two admin-password entries differ. Enter both again.')
                continue
            break
        while True:
            password = getpass.getpass('Unique MQTT gateway password (hidden): ')
            if 1 <= len(password.encode()) <= 96 and not any(c in password for c in '\r\n'):
                break
            print('MQTT password must be 1–96 bytes with no line breaks. Try again.')
        command('set nwia.enabled off')
        for line in ['set radio 910.525,62.5,7,5', 'set tx 22', 'set name ' + args.name,
                     'set repeat on', 'set nwia.ssid ' + args.ssid,
                     'set nwia.host ' + args.broker, 'set nwia.port ' + str(args.mqtt_port),
                     'set nwia.user ' + args.mqtt_user, 'set nwia.group ' + args.group,
                     'set nwia.mode ' + args.mode]:
            command(line)
        for line in ['password ' + admin, 'set nwia.wifiPassword ' + wifi,
                     'set nwia.password ' + password]:
            command(line, secret=True)
        if channel is not None:
            command('set nwia.channelkey ' + channel, secret=True)
        command('set nwia.enabled on')
        link.write(b'reboot\r')
        print('Configuration saved. Waiting for reboot, Wi-Fi, TLS and the 65-second quarantine…')
        deadline = time.monotonic() + 120
        time.sleep(8)
        while time.monotonic() < deadline:
            try:
                status = command('get nwia.status')
            except RuntimeError:
                time.sleep(3)
                continue
            if 'ready=1' in status and 'mqtt=1' in status:
                print(status.strip())
                print('MQTT connected. Wait 65 seconds after boot before RF bridge tests.')
                print('Connectivity is not proof of over-air delivery; follow nwia/VALIDATION.md.')
                return
            time.sleep(3)
        raise RuntimeError('MQTT did not connect within 120 seconds; check USB status and network settings')
    finally:
        link.close()


if __name__ == '__main__':
    main()
