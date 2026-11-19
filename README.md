# NWIA MeshCore MQTT gateway for Heltec V3

Experimental standalone firmware that keeps a Heltec V3 running as a native
MeshCore LoRa repeater while bridging one selected encrypted chat channel
bidirectionally through MQTT over Wi-Fi. No companion computer is required at
each gateway. A shared TLS MQTT broker runs elsewhere.

## Status

The ESP32-S3 target compiles and has been tested on a physical Heltec V3:

- RF-to-MQTT delivery observed by an independent subscriber.
- MQTT-to-RF test message received through a separate companion and phone.
- Duplicate ingress suppressed and MQTT-origin packets not re-exported.
- RF reception/retransmission continued during a broker outage.
- MQTT reconnected automatically without observed outage backlog replay.

These are single-gateway bench results. Two physically separated gateways,
Wi-Fi/router loss, RF overlap and long-duration stability still need testing.
Use one transmitting gateway per overlapping RF coverage area. This is not a
production-ready or exactly-once delivery claim.

## Download and flash

**[Open the browser flasher](https://john8675309.github.io/NWIA-MeshCore-MQTT/)**
to install directly over USB in desktop Chrome or Edge.


[Download prebuilt firmware from Releases](https://github.com/john8675309/NWIA-MeshCore-MQTT/releases).
Choose the **merged** image for a first installation and flash it at **0x0**.
[Flashing instructions](nwia/releases/FLASHING.md) cover browser flashing,
command-line flashing, backups and USB commissioning. No build is required.
These are experimental pilot images for Heltec V3 with US915 radio defaults.

## Build

```bash
python3 -m venv .venv
.venv/bin/pip install platformio
.venv/bin/pio run -e Heltec_v3_nwia_mqtt
.venv/bin/pio run -e Heltec_v3_nwia_mqtt -t mergebin
```

The tracked target is defined in `platformio.nwia.ini`. Network credentials and
private channel keys are configured over USB after flashing; they are not
compiled into the firmware. Deployment-specific files and hardware backups
are excluded from this repository.

Read [the setup guide](nwia/README.md), [validation record](nwia/VALIDATION.md),
and [broker example](nwia/broker/mosquitto.conf) before using the firmware.

## Important behavior

Only the selected 128-bit channel-text traffic is bridged. Direct messages,
ACKs, advertisements and remote management remain local RF traffic. Native
paths, hop rules and ciphertext are preserved. The gateway holds the selected
channel key and can decrypt it; MQTT carries the original encrypted packet.
Native channel MACs are short, so trusted operators and strict broker ACLs
remain necessary.

The bridge starts disabled and waits 65 seconds after boot. Local RF repeating
does not depend on MQTT. Wi-Fi/TLS/MQTT work runs separately from the radio
loop, with bounded queues, rate limits, freshness checks and duplicate caches.
No automatic primary/backup election is implemented.

## App login

Use a **12–15 character admin password** with standard companion firmware.
This gateway can store 16 characters, but the bundled standard companion's
login sender truncates at 15. A full 16-character login requires a compatible
companion build too. Symbols such as `*` are allowed.

## Upstream and license

Based on [MeshCore](https://github.com/meshcore-dev/MeshCore), upstream commit
`75b04ef06def54b348400cb1b138afb6749ffd4f`. This repository was prepared from
a source handoff archive, so it does not include upstream Git history.
The original [MIT license](license.txt) and third-party licenses are retained.
Dependency provenance is recorded in [vendor-lock.json](nwia/vendor-lock.json).
The original upstream overview is preserved in [README.upstream.md](README.upstream.md).
