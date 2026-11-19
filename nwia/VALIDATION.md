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

## Full-packet v2 development — 2026-11-19

The earlier hardware results above describe the channel-only v0.1 pilot.
The new default `mode=all` uses v2 topics / NWI2 envelopes and covers all 13
current payload families without a channel key.

Passed on the updated source:
- ASan/UBSan native tests for all payload families, four route encodings,
  1/2/3-byte paths, full-size frames, version isolation, skew and fuzz/truncation.
- Actual MeshCore two-gateway routing with real encrypted endpoints: DM,
  requests/responses, anonymous-login transport, path returns and signed adverts.
- Foreign-hop and zero-hop delivery, scope/duplicate guards, trace forwarding,
  ACK-clone ingress/deadline metadata, pool reuse and expiry before radio TX.
- The ref10 C dependency retains known signed-shift idioms; only its shift-base
  instrumentation is excluded, explicitly. ASan and other UBSan checks stay on;
  project C++ uses strict UBSan halt-on-error. A zero-length/null app-data copy
  in native advert creation exposed by those tests was corrected.
- ESP32-S3 compile/link/merged image: static RAM 90,036 / 327,680; flash
  1,162,461 / 3,342,336 bytes.

Live gateway candidate checks (operator identifiers omitted):
- Fresh full-device backup taken privately; correct hardware identity verified.
- Upload checksum passed; existing device identity/settings survived.
- `mode=all ready=1 enabled=1 dirty=0 mqtt=1`; TLS result 0, heap around
  146 KB, network-task stack margin 6920 bytes, core errors 0.
- Broker ACL extended with exact per-gateway v2 write/read permissions while
  retaining v1 entries; no password change.
- An authenticated temporary test origin sent a real ECDH/AES/MAC anonymous
  read-only clock request over MQTT. The actual gateway processed it and
  returned its native encrypted response via RF TX and MQTT. The ephemeral
  client verified/decrypted the response and checked request tag and clock.
  `in=1`, `pub=1`; temporary account/ACL removed.

Live full-mode checks for RF chat, DM, advertisements and app management passed. Two
physical gateways at RF-isolated sites and long-duration acceptance remain
required; the native two-gateway harness is not represented as radio proof.

Additional live full-mode results:
- User sent a DM to an offline recipient. Its type-2 encrypted flood packet
  was repeated over RF and observed on v2 MQTT. No recipient-delivery claim.
- User sent a Public-channel message. Type-5 group traffic appeared on v2 MQTT.
- User confirmed successful iPhone app login. The independent MQTT observer
  captured type-7 login, type-1 encrypted replies and type-0 follow-up management
  traffic while local app operation continued normally.
- Uptime exceeded five minutes, TLS remained connected, core errors stayed 0,
  and free heap remained about 146 KB during these live checks.

- A native signed flood advert was requested over USB and observed as type 4
  on v2 MQTT after RF transmission.
- A later routing test covered an origin at a third site and multiple MQTT
  legs: foreign mirrors do not mark native payload-only seen state; a consumed
  direct hop may re-export the shortened route with the original TTL. Per-stage
  transport fingerprints suppress unchanged echoes without blocking progress.
  That corrective candidate was rebuilt and reflashed for final checks.

Final live routing correction checks:
- Final candidate booted in full mode and reconnected with core errors 0.
- The encrypted read-only anonymous clock exchange passed again.
- A valid anonymous request addressed to an ephemeral test endpoint was sent
  through MQTT with this gateway as the next hop and a deliberately older
  envelope timestamp. The real gateway consumed its own path entry, transmitted
  the shortened direct packet, and re-exported it through MQTT. The independent
  client verified identical ciphertext and unchanged original creation/expiry.
- Temporary test account/ACL removed; normal gateway account retained.
- These are single-gateway physical tests plus multi-gateway native simulations,
  not a two-site RF-isolated acceptance test or a production-readiness claim.
