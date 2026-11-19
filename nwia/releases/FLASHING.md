# Flash a prebuilt Heltec V3 release

Download firmware from [GitHub Releases](https://github.com/john8675309/NWIA-MeshCore-MQTT/releases). No PlatformIO or compilation is needed.

## Choose the file

These pilot images target **Heltec WiFi LoRa 32 V3**, ESP32-S3 / SX1262 / 8 MB
flash. Do not use them for V2, V4, or an unrelated ESP32 board. Radio defaults
are **910.525 MHz, 62.5 kHz, SF7, CR5, 22 dBm**; verify they suit your hardware
and network before powering/transmitting. Attach the appropriate antenna.

- **`*-merged.bin`**: first-time installation or migration, flash at **`0x0`**.
  Includes bootloader, partition table, OTA boot selector and application.
  Padding also overwrites the NVS area; existing Wi-Fi/MQTT preferences may be
  erased. Device identity/settings from another firmware are not guaranteed
  compatible. Back up the complete flash first if they matter.
- **`*-app.bin`**: update an existing NWIA installation with the same partition
  layout and app0 boot selection, flash at **`0x10000`**. Preserves the NVS and
  filesystem areas. Never flash this app-only file at offset zero.
- **`*-flash-kit.zip`**: both images, this guide, SHA-256 checksums, build
  manifest, and optional USB commissioning helpers.

Check the download against `SHA256SUMS.txt` (Linux: `sha256sum -c`; macOS:
`shasum -a 256`; Windows: `Get-FileHash -Algorithm SHA256`).

## Option A: browser flasher

The simplest option is the [NWIA Heltec V3 web flasher](https://john8675309.github.io/NWIA-MeshCore-MQTT/).
It selects this release's merged image, verifies its download checksum and
opens the USB installer. Follow the on-page board/backup and setup instructions.
The generic Espressif tool below remains an alternative.


1. Connect the board to your computer with a USB data cable. Close any serial
   monitors or apps using its USB port.
2. Open [Espressif ESP Tool](https://espressif.github.io/esptool-js/) in a
   Web Serial-compatible desktop browser such as Chrome or Edge. The tool
   states that Safari is unsupported.
3. Connect to the board's USB serial port. Select the **merged** image and
   enter flash address **`0x0`**. Leave **Erase all** unchecked unless you
   deliberately want to wipe the entire device after taking a backup.
4. Program the image. Reset the board when flashing finishes.

If it does not enter download mode, hold BOOT, tap RESET, release BOOT, then
connect/flash again. Some USB cables carry power only.

## Option B: Python / esptool

These commands use esptool 4.9.0, whose command names are shown below. Newer
major versions can use different spelling. Replace `PORT` with your USB port
(for example `/dev/ttyUSB0`, `/dev/cu.usbserial-...` or `COM4`).

```bash
python -m pip install esptool==4.9.0
```

Optional full backup (contains private keys/settings; keep it private):

```bash
python -m esptool --chip esp32s3 --port PORT --baud 460800 read_flash 0 ALL private-backup.bin
```

First installation / merged image:

```bash
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 nwia-heltec-v3-v0.2.0-pilot.1-merged.bin
```

Existing NWIA installation / app-only update:

```bash
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x10000 nwia-heltec-v3-v0.2.0-pilot.1-app.bin
```

Reduce baud to 115200 if transfer is unreliable. Refer to the official
[esptool basic commands](https://docs.espressif.com/projects/esptool/en/release-v4/esp32s3/esptool/basic-commands.html) for details.

## Configure after flashing

The download contains **no operator Wi-Fi/MQTT credentials or private channel
key**. A fresh installation defaults to full mode with MQTT forwarding disabled.
Existing installations retain their enabled setting: add v2 broker ACLs and
upgrade peer gateways before switching to full mode. Normal repeater
operation follows compiled defaults or surviving stored preferences; the
bootstrap admin password is `password`. **Change it during USB setup.**

Use a USB serial terminal at 115200 baud, carriage-return line endings, or
the included helper. The helper needs Python and pyserial, not PlatformIO:

```bash
python -m pip install pyserial==3.5
python commission.py --port PORT --broker mqtt.example.com --ssid YOUR_WIFI --mqtt-user gateway-a --name "NWIA Gateway" --group pilot
```

For a source checkout, the helper lives at `nwia/commission.py`. It prompts
without echo for passwords and, only in legacy channel mode, the MeshCore channel key.
Full mode needs no channel key at the gateway; companions keep their existing channels and keys.
Use a **12–15 character admin password** for compatibility with standard
companion firmware; symbols such as `*` work. Although this gateway stores
16 characters, standard companions truncate login passwords at 15.

Provision your own TLS broker account and MAC-bound ACL using the gateway ID
from `get nwia.status`. A publicly trusted broker must chain to the included
ISRG Root X1 trust anchor. A private CA requires a custom build.

After setup/reboot, expect `get nwia.status` to show ready=1, enabled=1,
dirty=0 and mqtt=1. Wait **65 seconds after boot** before testing bridge
traffic. Verify actual handheld reception in both directions.

This experimental full-packet pilot transports all current MeshCore packet families,
including DMs, ACKs, advertisements, discovery and management. Broker ACLs
and peer gateways must support v2; see FULL-PACKET-TRANSPORT.md. It uses QoS 0 and does not promise
exactly-once delivery. Start with one gateway per overlapping RF area.
See [the setup guide](https://github.com/john8675309/NWIA-MeshCore-MQTT/blob/main/nwia/README.md)
and [validation record](https://github.com/john8675309/NWIA-MeshCore-MQTT/blob/main/nwia/VALIDATION.md) for limits and outstanding two-site/stability tests.
