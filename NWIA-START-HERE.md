# Start here

Read [README.md](README.md), [nwia/README.md](nwia/README.md) and
[nwia/VALIDATION.md](nwia/VALIDATION.md).

This is a sanitized public source distribution of an experimental standalone
Heltec V3 MeshCore LoRa/Wi-Fi MQTT gateway. No operator broker address, account,
private channel key, password, device backup or deployment log is included.

Build `Heltec_v3_nwia_mqtt`, configure a TLS broker with per-gateway credentials
and v2 MAC-bound ACLs, then commission over USB. Full mode needs no channel key. Keep local radio operation
independent of MQTT. Run the acceptance checklist before adding gateways.
