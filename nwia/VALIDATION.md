# Validation record — 2026-11-19

Status: experimental bench-test firmware. Deployment-specific identities,
network settings, credentials and logs are omitted from this public record.

## Passed host checks

- BridgeCore native tests under AddressSanitizer and UndefinedBehaviorSanitizer:
  structure/truncation, freshness, future/expired frames, packet/path bounds,
  cache saturation, wraparound and 100,000 malformed frames. LeakSanitizer is
  disabled because the test environment runs under ptrace.
- Actual upstream MeshCore Packet/Utils and Crypto integration: encrypted
  round trip, preserved path/ciphertext, stable fingerprint and wrong-key rejection.
- Broker installer shell syntax and helper Python syntax checks.
- ESP32-S3 build/link/size check and merged image generation.

## Passed on a physical Heltec V3

- Full device backup before flashing; USB upload/write checksum verification.
- Native RF repeating and confirmed radio parameters.
- Wi-Fi association, NTP clock, validated TLS and authenticated MQTT connection.
- RF-to-MQTT: a separate companion sent a test; native RF receive/retransmit
  counters increased and an independent subscriber received its encrypted frame.
- MQTT-to-RF: a labelled native test message arrived through MQTT, was submitted
  to MeshCore and transmitted over LoRa; the user confirmed it on a phone.
- Duplicate MQTT ingress suppressed; MQTT-origin packet not exported again.
- Broker deliberately stopped: new local RF receive/retransmit counters
  increased while MQTT was disconnected. Uptime continued and core errors were
  zero. Reception of this offline transmission by another local handheld was
  not separately measured.
- Broker restarted: gateway reconnected automatically in about eight seconds;
  no outage backlog replay was observed.
- Final-image reboot retained settings/identity and connected successfully.

## Broker checks

A dedicated Debian 13 / Mosquitto 2.0.21 broker was inspected before changes,
then configured with a public TLS certificate, unique account/MAC ACL, no
public plaintext listener, anonymous access disabled, retained messages and
persistence disabled. External tests passed for compiled-CA/hostname
verification, MQTT 3.1.1 clean-session QoS0 delivery, wrong-password rejection,
wrong-origin/group write rejection, and retained-message rejection. Certificate
renewal simulation, deploy hook, service restart and reload passed. Temporary
test accounts were removed after use. No operator configuration is included.

## Issues found and corrected

- Malformed PlatformIO undefine flags caused initial compiler failures. Exact
  inherited definitions are now removed through build_unflags.
- Initial TLS attempts timed out and triggered the core-0 idle watchdog.
  Corrected seconds-based timeout calls, raised handshake allowance to 20
  seconds, raised CPU from 80 to 240 MHz and pinned the network worker to core
  0 at idle priority. Watchdogs and certificate checks remain enabled.
- Positive Arduino TLS socket descriptors are normalized to a successful
  diagnostic result.
- USB admin-password replies no longer echo passwords.
- Gateway storage was extended to 16 password characters while preserving
  legacy preference offsets. Standard companion sendLogin still truncates
  at 15: use 12–15 characters for standard app login. App-login acceptance is
  not claimed by these bench results.

## Not completed

- End-to-end tests with two physical gateways at RF-isolated sites.
- Wi-Fi/router-loss and second-local-handheld outage reception tests.
- Overlapping-gateway airtime measurements, extensive restart/replay and
  saturation tests, full wrong-channel/clock failure hardware coverage.
- Long-duration stability and automatic primary/backup election.

These results are not a production-readiness or exactly-once delivery claim.

## Provenance

Upstream MeshCore commit: 75b04ef06def54b348400cb1b138afb6749ffd4f.
Existing MIT and third-party licenses are retained. Dependency versions and
checksums are in vendor-lock.json. ISRG Root X1 is a public trust anchor.
No passwords, private channel keys, private CA keys or flash dumps are included.
