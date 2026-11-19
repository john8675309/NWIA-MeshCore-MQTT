# Standalone Heltec V3 MeshCore / Wi-Fi MQTT repeater

This firmware keeps native MeshCore repeating active while bridging one
selected 128-bit channel-text stream over a shared TLS MQTT broker. No
companion computer is needed at the radio site. User phones continue using
their normal companion radios and apps.

## Build and flash

```bash
python3 -m venv .venv
.venv/bin/pip install platformio
.venv/bin/pio run -e Heltec_v3_nwia_mqtt
.venv/bin/pio run -e Heltec_v3_nwia_mqtt -t mergebin
```

The target is tracked in `platformio.nwia.ini`; an optional local override
file stays ignored. Default radio settings are 910.525 MHz / 62.5 kHz / SF7 /
CR5 and 22 dBm. Verify matching settings and an appropriate antenna before
transmitting. Existing stored radio preferences may override compiled defaults.
Back up an existing device's identity/settings before flashing.

```bash
.venv/bin/pio run -e Heltec_v3_nwia_mqtt -t upload --upload-port /dev/ttyUSB0
```

Use the actual USB port. `firmware-merged.bin` belongs at offset 0; an app-only
`firmware.bin` belongs at the correct app offset (0x10000 for this target).
Prefer PlatformIO for upload. The default public trust anchor is ISRG Root X1.
For a private CA, generate ignored `nwia/BrokerCA.h` before building:

```bash
python3 nwia/embed_ca.py /path/to/your-public-ca.crt
```

No private keys or passwords belong in that CA header. TLS hostname and
certificate verification stay enabled.

## Broker

Inspect your server, existing services, DNS and firewall before changes.
The supplied installer is for a clean dedicated Debian/Ubuntu broker and
refuses to overwrite its deployment files. Inspect it before execution:

```bash
sudo bash nwia/broker/install.sh mqtt.example.com operator@example.com gateway-a=aabbccddeeff gateway-b=112233445566
```

Use the real gateway identities from `get nwia.status`. Allow inbound TCP 8883
for MQTT and TCP 80 for standalone certificate issuance/renewal. The installer
uses Certbot interactively for certificate-account terms acceptance. Do not
replace an unrelated broker configuration blindly.

Each account may publish only to its own physical MAC topic and read the
selected group's packet topics. Passwords are unique per gateway and written
to a root-only credentials file on the broker. Do not reuse one account on
multiple gateways. Examples use broker group `pilot`; adjust the ACLs if you
choose a different group. No public plaintext listener is intended. Retained
messages and broker persistence must be disabled for this bridge.

## USB commissioning

After flashing, use a serial console at 115200 baud. CLI commands end with
carriage return. Forwarding starts disabled. Inspect `get nwia.status` to
obtain the MAC identity, then provision its broker account and ACL.

The interactive helper accepts your deployment details as non-secret arguments
and prompts locally without echo for passwords and the private channel key:

```bash
.venv/bin/python nwia/commission.py --port /dev/ttyUSB0 --broker mqtt.example.com --ssid YOUR_WIFI --mqtt-user gateway-a --name "NWIA Gateway" --group pilot
```

Optionally supply `--expected-id aabbccddeeff` to verify a specific device
before sending secrets. `--mqtt-port` defaults to 8883. Credentials are saved
to device NVS, not source code. Physical flash access is not protected by this
firmware; keep backups private. The helper configures the target radio defaults,
changes the admin password, enables the bridge and reboots.

A “pilot channel” simply means the MeshCore chat channel chosen for tests. It
is not a radio frequency or a special channel type. Use the same channel key
on test gateways and companions. The broker group label `pilot` is separate
from that RF chat channel. Never assume a channel name alone determines its key.
The key here is 16 bytes represented as 32 hexadecimal characters; companion
apps may display a different import/export representation.

Use 12–15 printable ASCII characters for normal app admin login. The gateway
supports storing 16 characters, but standard companion firmware sends only 15.
A 16-character login requires corresponding companion support. Symbols such
as `*` work. To change only the admin password, without a reboot:

```bash
.venv/bin/python nwia/set-admin-password.py --port /dev/ttyUSB0
```

Wi-Fi ownership belongs to this bridge: upstream Wi-Fi OTA is disabled. Use
USB updates. Sensor auto-discovery is disabled in this target.

## Diagnostics and controls

- `get nwia.status`: configuration readiness, enabled/dirty state, MQTT connection
  and aggregate publish/injection/drop/duplicate counters.
- `get nwia.network`: Wi-Fi status/RSSI, epoch clock, MQTT/TLS result, heap and
  network-task stack margin. Wi-Fi status 3 means connected.
- `get nwia.config`: non-secret boot-time broker/group/account summary.
- `neighbors`: directly heard repeaters, with ID prefix, seconds since last
  heard and SNR in quarter-dB units. Companion radios do not appear here.
- `discover.neighbors`: sends one zero-hop repeater discovery request.
- `set nwia.enabled off`: immediately pauses the bridge; ordinary RF repeating
  continues. Packets already in the native radio queue can still transmit.
- `set nwia.enabled on`, then `reboot`: enables bridging with a new quarantine.

New Wi-Fi/MQTT settings are USB-only. Most edits need a reboot. Normal repeater
management remains available over local RF through a companion/app.
`ready=1` means configuration is present, not connectivity. `mqtt=1` means a
connection was established, not subscriber or handheld delivery. `pub` counts
sends, and `in` counts native mesh submissions rather than proven reception.
Counters reset on reboot.

## Traffic, security and outage behavior

Only selected channel-text flood packets are bridged. DMs, ACKs, advertisements
and management traffic stay on RF. Preserve paths, ciphertext and original
sender/timestamp. The gateway possesses the selected channel key and can
decrypt; no end-to-end secrecy from the gateway is claimed.

- Bounded task queues; all native mesh allocation/processing stays on the
  radio task. TLS/MQTT runs on core 0 at idle priority; the radio loop runs on
  core 1. CPU is 240 MHz; watchdogs remain enabled.
- Validated TLS, per-gateway credentials and strict publish ACLs. Clean MQTT
  sessions, QoS 0, no retained data, no intentional outage backlog or retry.
- Envelopes expire after 60 seconds; a 65-second boot quarantine prevents
  replay from the previous RAM cache lifetime.
- Original message timestamps must be within five minutes in the past and
  30 seconds ahead. Handheld clocks must be synchronized.
- 256 fingerprints retained for five minutes. Cache saturation drops new
  bridge traffic instead of evicting live entries.
- Two-second RF-first ingress grace; four pending ingress frames. Export and
  injection are each limited to one packet per 15 seconds with no bursts.
- MQTT-ingressed packets are never exported by the same gateway. Native path,
  scope, hop and duplicate checks still apply. Already queued RF packets do
  not carry an MQTT expiry deadline.

Multiple overlapping gateways can still spend duplicate airtime. There is no
shared primary/backup election or split-brain fencing. Start with one gateway
per RF coverage area. Local RF repeating does not depend on Wi-Fi, NTP, TLS
or MQTT. Internet-side failures must not block the radio loop.

## Acceptance checklist

1. Provision two gateways with the same selected channel key and broker group,
   distinct accounts/MAC ACLs, and matching radio parameters.
2. Wait 65 seconds after boot. Arrange sites without a direct/intermediate RF
   path, with a companion near each gateway.
3. Send each direction; verify actual handheld reception, original message
   identity, RF counters and an independent MQTT subscriber.
4. Add RF overlap and inspect airtime/duplicate behavior.
5. Disconnect broker/internet, then Wi-Fi; verify local RF reception and
   retransmission. Restore networking and check for stale replay.
6. Test restarts, wrong credentials/CA/hostname/channel key, expired frames,
   queue saturation and sustained traffic.
7. Record observed results separately from compilation and host simulations.

See [VALIDATION.md](VALIDATION.md) for the actual completed bench checks and
remaining work. Native host tests can be run using `bash nwia/run-native-tests.sh`.
