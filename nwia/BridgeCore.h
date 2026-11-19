#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace nwia {
constexpr uint32_t kWireTtl = 60;
enum class Mode : uint8_t { Channel, All };
constexpr size_t kMaxRaw = 255;
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
inline bool validRaw(const uint8_t* p, size_t n, Mode mode = Mode::Channel) {
  if (n < 3 || n > kMaxRaw || (p[0] >> 6) != 0) return false;
  const uint8_t route = p[0] & 3, type = (p[0] >> 2) & 15;
  const size_t pos = (route == 0 || route == 3) ? 5 : 1;
  if (n <= pos) return false;
  const uint8_t path = p[pos];
  const size_t hashSize = (path >> 6) + 1;
  const size_t pathBytes = (path & 63) * hashSize;
  if (hashSize > 3 || pathBytes > 64) return false;
  const size_t payloadPos = pos + 1 + pathBytes;
  if (payloadPos >= n) return false;
  const size_t size = n - payloadPos;
  if (size > 184) return false;
  if (mode == Mode::Channel)
    return (route == 0 || route == 1) && type == 5 && size >= 19 && (size - 3) % 16 == 0;
  // Bounds protect upstream readers before any native parsing. All current
  // MeshCore payload families are supported; reserved types/versions fail closed.
  switch (type) {
    case 0: case 1: case 2: case 8: return size >= 20 && (size - 4) % 16 == 0;
    case 3: return size >= 4;
    case 4: return size >= 100 && size <= 132;
    case 5: case 6: return size >= 19 && (size - 3) % 16 == 0;
    case 7: return size >= 51 && (size - 35) % 16 == 0;
    case 9: {
      if (route < 2 || size < 9) return false;
      const size_t entrySize = size_t(1) << (p[payloadPos + 8] & 3);
      return (size - 9) % entrySize == 0;
    }
    case 10: return size >= 3 && ((p[payloadPos] & 15) != 3 || size >= 5);
    case 11: case 15: return size >= 1;
    default: return false;
  }
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
                     const uint8_t* raw, size_t n, Mode mode = Mode::Channel) {
  if (!validRaw(raw, n, mode)) return 0;
  memcpy(out, mode == Mode::All ? "NWI2" : "NWI1", 4); memcpy(out + 4, origin, 6);
  write32(out + 10, now); write32(out + 14, now + kWireTtl);
  out[18] = n >> 8; out[19] = n;
  memcpy(out + kHeader, raw, n);
  return kHeader + n;
}
inline bool decode(const uint8_t* p, size_t n, uint32_t now, Envelope& e, Mode mode = Mode::Channel) {
  if (now < 1700000000 || n < kHeader || n > kMaxWire || memcmp(p, mode == Mode::All ? "NWI2" : "NWI1", 4)) return false;
  const uint32_t created = read32(p + 10), expires = read32(p + 14);
  // NTP clocks may straddle a second boundary. Full transport tolerates at
  // most two seconds of future skew; legacy channel mode stays strict.
  const uint32_t skew = mode == Mode::All ? 2 : 0;
  if (uint64_t(created) > uint64_t(now) + skew || expires <= now || expires < created || expires - created != kWireTtl) return false;
  const size_t rawSize = (size_t(p[18]) << 8) | p[19];
  if (rawSize != n - kHeader || !validRaw(p + kHeader, rawSize, mode)) return false;
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
