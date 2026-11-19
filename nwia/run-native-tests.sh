#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g tests/nwia/core_test.cpp -o "$work/core"
ASAN_OPTIONS=detect_leaks=0 "$work/core"
g++ -std=c++17 -fsanitize=address,undefined -g -Isrc -Ilib/NWIA_Crypto -Itests/nwia/native tests/nwia/packet_test.cpp src/Packet.cpp src/Utils.cpp lib/NWIA_Crypto/{AES128,AESCommon,SHA256,Hash,Cipher,BlockCipher,Crypto}.cpp -o "$work/packet"
ASAN_OPTIONS=detect_leaks=0 "$work/packet"
bash -n nwia/broker/install.sh

# Full-packet protocol/parser and native routing interoperability.
g++ -std=c++17 -fsanitize=address,undefined -g -Isrc -Ilib/NWIA_Crypto -Itests/nwia/native tests/nwia/all_packets_test.cpp src/Packet.cpp lib/NWIA_Crypto/{SHA256,Hash,Crypto}.cpp -o "$work/all"
ASAN_OPTIONS=detect_leaks=0 "$work/all"
native_ed_objects=()
for native_ed_source in lib/ed25519/{add_scalar,fe,ge,key_exchange,keypair,sc,sha512,sign,verify}.c; do
  native_ed_object="$work/$(basename "$native_ed_source" .c).o"
  # The unchanged ref10 C dependency intentionally shifts negative limbs.
  # Exclude only its shift-base instrumentation; project C++ stays strict.
  gcc -DED25519_NO_SEED=1 -fsanitize=address,undefined -fno-sanitize=shift-base -g -c "$native_ed_source" -o "$native_ed_object"
  native_ed_objects+=("$native_ed_object")
done
g++ -std=c++17 -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -g -Isrc -Ilib/NWIA_Crypto -Ilib/ed25519 -Itests/nwia/native tests/nwia/routing_test.cpp src/{Mesh,Dispatcher,Packet,Utils,Identity}.cpp src/helpers/StaticPoolPacketManager.cpp lib/NWIA_Crypto/{AES128,AESCommon,SHA256,SHA512,Hash,Cipher,BlockCipher,Crypto,Ed25519,Curve25519,BigNumberUtil}.cpp "${native_ed_objects[@]}" -o "$work/routing"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$work/routing"
