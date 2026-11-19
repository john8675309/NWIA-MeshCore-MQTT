#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace nwia {
constexpr uint32_t kWireTtl = 60;
constexpr size_t kMaxRaw = 250;
constexpr size_t kHeader = 20;
constexpr size_t kMaxWire = kHeader + kMaxRaw;
inline uint32_t read32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void write32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
// Strictly validate before calling upstream Packet::readFrom, which assumes
// the frame already has enough bytes for its variable header and path.
inline bool validRaw(const uint8_t* p, size_t n) {
  if (n < 21 || n > kMaxRaw) return false;
  const uint8_t route = p[0] & 3;
  if (route != 0 && route != 1) return false;  // flood only
  if ((p[0] >> 6) != 0 || ((p[0] >> 2) & 15) != 5) return false; // v1 channel text
  const size_t pos = route == 0 ? 5 : 1;
  if (n <= pos) return false;
  const uint8_t path = p[pos];
  const size_t hashSize = (path >> 6) + 1;
  const size_t pathBytes = (path & 63) * hashSize;
  if (hashSize > 3 || pathBytes > 64) return false;
  const size_t payloadPos = pos + 1 + pathBytes;
  if (payloadPos >= n) return false;
  const size_t payloadSize = n - payloadPos;
  return payloadSize >= 19 && payloadSize <= 184 && (payloadSize - 3) % 16 == 0;
}
inline bool freshOriginal(uint32_t timestamp, uint32_t now) {
  return now >= 1700000000 && uint64_t(timestamp) <= uint64_t(now) + 30 &&
         (timestamp >= now || now - timestamp <= 300);
}
struct Envelope {
  uint8_t origin[6];
  uint32_t created;
  const uint8_t* raw;
  size_t rawSize;
};
inline size_t encode(uint8_t* out, const uint8_t origin[6], uint32_t now,
                     const uint8_t* raw, size_t n) {
  if (!validRaw(raw, n)) return 0;
  memcpy(out, "NWI1", 4); memcpy(out + 4, origin, 6);
  write32(out + 10, now); write32(out + 14, now + kWireTtl);
  out[18] = n >> 8; out[19] = n;
  memcpy(out + kHeader, raw, n);
  return kHeader + n;
}
inline bool decode(const uint8_t* p, size_t n, uint32_t now, Envelope& e) {
  if (now < 1700000000 || n < kHeader || n > kMaxWire || memcmp(p, "NWI1", 4)) return false;
  const uint32_t created = read32(p + 10), expires = read32(p + 14);
  // Future packets fail closed. All gateways use NTP. No retained/backlog replay.
  if (created > now || expires <= now || expires < created || expires - created != kWireTtl) return false;
  const size_t rawSize = (size_t(p[18]) << 8) | p[19];
  if (rawSize != n - kHeader || !validRaw(p + kHeader, rawSize)) return false;
  memcpy(e.origin, p + 4, 6); e.created = created; e.raw = p + kHeader; e.rawSize = rawSize;
  return true;
}
enum Flags : uint8_t { HeardRF = 1, Uploaded = 2, Injected = 4 };
template<size_t Capacity> class Cache {
  struct Entry { uint8_t hash[16]; uint32_t at; uint8_t flags; } entries[Capacity]{};
 public:
  uint8_t flags(const uint8_t hash[16], uint32_t now) const {
    for (const auto& e : entries)
      if (e.flags && uint32_t(now - e.at) < 300000 && !memcmp(e.hash, hash, 16)) return e.flags;
    return 0;
  }
  bool mark(const uint8_t hash[16], uint8_t bits, uint32_t now) {
    Entry* freeEntry = nullptr;
    for (auto& e : entries) {
      if (e.flags && uint32_t(now - e.at) < 300000) {
        if (!memcmp(e.hash, hash, 16)) { e.flags |= bits; return true; }
      } else if (!freeEntry) freeEntry = &e;
    }
    // Never evict a live identity to make room for traffic. Saturation drops bridge traffic.
    if (!freeEntry) return false;
    memcpy(freeEntry->hash, hash, 16); freeEntry->at = now; freeEntry->flags = bits;
    return true;
  }
};
} // namespace nwia
