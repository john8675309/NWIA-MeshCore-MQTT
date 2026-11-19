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
