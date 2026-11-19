#!/usr/bin/env python3
"""Set an app-compatible admin password locally over USB; no other settings change."""
import argparse
import getpass
import os
import re
import select
import termios
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',default='/dev/ttyUSB0')
parser.add_argument('--expected-id',help='Optional expected 12-hex gateway MAC identity')
args=parser.parse_args()
fd=os.open(args.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
try:
    settings=termios.tcgetattr(fd);settings[2]&=~termios.HUPCL
    termios.tcsetattr(fd,termios.TCSANOW,settings)
    def command(value):
        termios.tcflush(fd,termios.TCIFLUSH)
        os.write(fd,value.encode()+b'\r')
        response=b'';end=time.monotonic()+5
        while time.monotonic()<end:
            ready,_,_=select.select([fd],[],[],.1)
            if ready:response+=os.read(fd,8192)
            if b'  -> ' in response and response.endswith(b'\n'):break
        return response.decode(errors='replace')
    match=re.search(r'id=([0-9a-f]{12})',command('get nwia.status'))
    if not match or (args.expected_id and match[1]!=args.expected_id.lower()):
        raise RuntimeError('Expected NWIA firmware and matching gateway identity; no password sent.')
    print('Set only the repeater admin password. Wi-Fi, MQTT, channel and radio settings stay in place.')
    print('Use 12–15 printable ASCII characters for compatibility with standard companion firmware. Symbols such as * are allowed.')
    while True:
        password=getpass.getpass('New admin password (hidden): ')
        if not 12<=len(password)<=15 or not all(32<=ord(c)<=126 for c in password):
            print('Use 12–15 printable ASCII characters. Try again.');continue
        confirm=getpass.getpass('Repeat admin password (hidden): ')
        if password!=confirm:
            print('The entries differ. Try again.');continue
        break
    reply=command('password '+password)
    if 'OK admin password saved' not in reply:
        raise RuntimeError('Password update not confirmed; raw reply hidden.')
    print('Admin password saved. No reboot needed. Try logging into your repeater from your app.')
finally:os.close(fd)
