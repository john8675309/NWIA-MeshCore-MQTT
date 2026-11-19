# Full-packet MQTT transport

`mode=all` is the default. The gateway transports all current MeshCore payload
families without needing private channel keys or decrypting relayed DM/channel
payloads. Traffic addressed to the gateway itself is handled normally. Original
ciphertext, sender identity, signatures and routing data stay intact. Native
packet version 0 is supported; reserved future versions/types and structurally
invalid frames are rejected.

## Traffic families

| Native type | Traffic |
| --- | --- |
| 0 / 1 | Requests and responses, including management |
| 2 | Direct text messages |
| 3 | ACKs |
| 4 | Signed advertisements |
| 5 / 6 | All channel text and channel data |
| 7 | Anonymous requests, including initial login |
| 8 | Path returns |
| 9 | Traces |
| 10 | Multipart traffic / native multipart ACKs |
| 11 | Control and discovery |
| 15 | Custom data |

All four native route encodings and 1/2/3-byte normal path hashes are carried.
Each family still follows the native semantics of its route (for example,
trace and zero-hop control are direct forms). This does not implement reserved
future payloads, invent new route types, or override native radio capacity.

## Routing

Export successful native RF transmissions after their hop/path update. Also
export valid received packets that native processing does not forward, notably
direct packets addressed to a different next-hop repeater and zero-hop traffic.
The latter must cross the network without incorrectly consuming this gateway
as a hop.

At the receiving gateway, run native MeshCore processing first. When this
gateway is the next hop, normal route consumption, decryption for local traffic,
management replies, ACK forwarding and flood-hop appending apply. A direct
packet for another node is re-radiated unchanged as a virtual RF segment,
unless it was already seen, marked consumed, or forwarding is disallowed.
Flood traffic cannot use this fallback to bypass native scope, hop or loop
policy. Public advertisement signatures are checked before bridge export or
mirroring. Native authentication at the intended destination remains required.

Ingress metadata survives path mutation and native ACK cloning. Unchanged
mirrors and flood ingress are not re-exported. A direct packet may make another
MQTT leg only after this gateway consumes its own next-hop entry (or advances
a trace). That leg retains the original envelope creation/expiry, not a fresh
TTL. Direct-packet fingerprints include the route stage, so a gateway that
first mirrored a foreign-hop packet can later act as its legitimate next hop.
Foreign mirrors do not poison the native payload-only seen table. Traces also
carry their current SNR/path stage. Packet pool reuse clears all metadata.

This extends the logical RF network between sites. Remote adverts and discovery
may consequently appear reachable through a nearby bridge. Those virtual links
are not evidence of a physical long-distance RF reception. A gateway does not
add directly MQTT-received adverts to its physical neighbor list or invent RF
RSSI measurements. Do not use bridge traffic to claim physical map coverage.

## Timing, queues and outages

Full mode removes the old one-packet-per-15-seconds export/injection throttle.
Direct traffic has no artificial ingress grace delay; flood ingress waits
250 ms to allow a local RF copy to suppress it. Native scheduling still handles
priorities, CAD, duty/airtime budget and radio transmission. Queues are bounded
(16 frames per network direction, 8 pending ingress); keep four native packet
slots reserved for RF/local replies and avoid adding bridge ingress when eight
RF transmissions are already queued. Congestion can drop traffic.

The fingerprint cache holds 1024 entries for five minutes and refuses to evict
live entries. Saturation drops new bridge traffic while local RF continues.
Envelopes expire after 60 seconds, tolerate at most two seconds of NTP clock
skew, and are quarantined for 65 seconds after boot. Their remaining lifetime
is carried into native radio queueing; expired ingress is dropped before TX.
Outgoing network queue entries expire after five seconds. Clean MQTT sessions,
QoS 0, non-retained data, no deliberate outage backlog or send retries remain.

Opaque encrypted payload timestamps cannot be checked without destination
keys. Full mode therefore relies on short-lived envelopes, duplicate caches,
authenticated gateways and trusted operators; it does not claim independent
cryptographic freshness of a captured RF packet. Start with one transmitting
gateway per overlapping RF coverage area. There is no shared leader election.

## Configuration and broker migration

```text
set nwia.mode all
set nwia.enabled on
reboot
get nwia.status
```

Wi-Fi/broker/account/group/CA settings remain necessary. A channel key is not
required in full mode. Existing private channels keep their normal keys on
companions/endpoints; their encrypted packets are transported opaquely.

Full mode uses **`nwiamesh/v2/GROUP/packets/GATEWAY_MAC`** and **NWI2**
envelopes. A per-gateway ACL needs its own exact write topic and a read wildcard
limited to that group. The installer and ACL example include v2 plus legacy v1
permissions for migration. Existing brokers need equivalent v2 entries added
without widening writes to other gateway identities. Older channel-only peers
will not exchange v2 traffic; upgrade both gateways.

The old behavior remains available through `set nwia.mode channel` and a reboot.
It uses v1/NWI1, one configured channel key/MAC check, original message timestamp
filtering, and the older low-volume policy. Do not mix modes expecting exchange.

## Validation

`bash nwia/run-native-tests.sh` covers every family and route encoding, maximum
frames, v1/v2 isolation, clock skew, truncation and malformed input. The native
integration harness uses actual MeshCore routing, packet pools, real encrypted
endpoints and signature verification to exercise DMs, request/response and
anonymous-login transport, path returns, adverts, direct/foreign hops, ACKs,
traces, scope/duplicate guards, metadata reuse and expiry before radio TX.

ASan and UBSan remain enabled for project code. The unchanged ref10 C Ed25519
dependency's negative-limb shift-base instrumentation is excluded explicitly;
its other UBSan checks and ASan stay enabled. This is not a claim that the
legacy crypto dependency is free of undefined signed-shift behavior.

Host integration is not two-site RF acceptance. See VALIDATION.md for actual
hardware results and remaining tests before unattended deployment.
