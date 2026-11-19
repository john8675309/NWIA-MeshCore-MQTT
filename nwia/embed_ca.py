#!/usr/bin/env python3
"""Embed a public broker CA into the firmware. Never accepts a private key."""
import pathlib
import sys
pem = pathlib.Path(sys.argv[1]).read_text()
if "PRIVATE KEY" in pem or "-----BEGIN CERTIFICATE-----" not in pem:
    raise SystemExit("Supply a PEM CA certificate, never a private key")
if ')CA"' in pem:
    raise SystemExit("Invalid PEM")
out = pathlib.Path(__file__).with_name('BrokerCA.h')
out.write_text('#pragma once\nstatic const char NWIA_BROKER_CA[] = R"CA(' + pem + ')CA";\n')
print(out)
