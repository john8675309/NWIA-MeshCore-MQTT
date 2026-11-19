#include "../../nwia/BridgeCore.h"
#include <cassert>
#include <vector>
#include <random>
#include <iostream>

int main() {
  std::vector<uint8_t> raw(21, 0);
  raw[0] = (5 << 2) | 1; // flood, channel text, empty path + channel/hash/MAC + AES block
  assert(nwia::validRaw(raw.data(), raw.size()));
  const uint8_t origin[6] = {1,2,3,4,5,6};
  uint8_t wire[nwia::kMaxWire];
  const uint32_t epoch = 1800000000;
  const size_t n = nwia::encode(wire, origin, epoch, raw.data(), raw.size());
  assert(n == 41);
  assert(nwia::freshOriginal(epoch, epoch));
  assert(nwia::freshOriginal(epoch - 300, epoch));
  assert(!nwia::freshOriginal(epoch - 301, epoch));
  assert(nwia::freshOriginal(epoch + 30, epoch));
  assert(!nwia::freshOriginal(epoch + 31, epoch));
  assert(!nwia::freshOriginal(0, 0));
  nwia::Envelope e{};
  assert(nwia::decode(wire, n, epoch, e));
  assert(e.rawSize == raw.size() && !memcmp(e.raw, raw.data(), raw.size()));
  assert(!nwia::decode(wire, n, epoch - 1, e));
  assert(!nwia::decode(wire, n, epoch + 60, e));
  assert(!nwia::decode(wire, n, 0, e));
  for (size_t size = 0; size < n; ++size) assert(!nwia::decode(wire, size, epoch, e));
  wire[19]++; assert(!nwia::decode(wire, n, epoch, e)); wire[19]--;
  wire[0] = 'X'; assert(!nwia::decode(wire, n, epoch, e)); wire[0] = 'N';
  nwia::write32(wire + 14, epoch + 61); assert(!nwia::decode(wire, n, epoch, e));
  nwia::write32(wire + 14, epoch + 60);
  raw[0] = (5 << 2) | 2; assert(!nwia::validRaw(raw.data(), raw.size()));
  raw[0] = (4 << 2) | 1; assert(!nwia::validRaw(raw.data(), raw.size()));
  raw[0] = (5 << 2) | 1; raw[1] = 63; assert(!nwia::validRaw(raw.data(), raw.size()));
  raw[1] = 0xc0; assert(!nwia::validRaw(raw.data(), raw.size()));
  raw[1] = 0;
  std::vector<uint8_t> scoped(25, 0); scoped[0] = 5 << 2;
  assert(nwia::validRaw(scoped.data(), scoped.size()));
  std::vector<uint8_t> path(24, 0); path[0] = (5 << 2) | 1; path[1] = 0x81;
  assert(nwia::validRaw(path.data(), path.size()));
  // Persistent in RAM across MQTT reconnects; refuse eviction under cache pressure.
  nwia::Cache<2> cache;
  uint8_t a[16] = {1}, b[16] = {2}, c[16] = {3};
  assert(cache.mark(a, nwia::HeardRF, 100));
  assert(cache.mark(a, nwia::Uploaded, 200));
  assert(cache.flags(a, 200) == (nwia::HeardRF | nwia::Uploaded));
  assert(cache.mark(b, nwia::Injected, 200));
  assert(!cache.mark(c, nwia::Uploaded, 300));
  assert(cache.flags(b, 300) & nwia::Injected); // incoming echo must not be republished
  assert(cache.flags(a, 300100) == 0);
  assert(cache.mark(c, nwia::Uploaded, 300100));
  nwia::Cache<1> wrapped;
  assert(wrapped.mark(a, nwia::Injected, 0xfffffff0));
  assert(wrapped.flags(a, 20) & nwia::Injected);
  assert(!wrapped.flags(a, 300000));
  // Malformed network bytes, including truncation: ASan/UBSan verify all bounds.
  std::mt19937 rng(42);
  std::vector<uint8_t> fuzz(600);
  for (unsigned i = 0; i < 100000; ++i) {
    const size_t size = rng() % fuzz.size();
    for (size_t k = 0; k < size; ++k) fuzz[k] = rng();
    nwia::validRaw(fuzz.data(), size);
    nwia::decode(fuzz.data(), size, epoch, e);
  }
  std::cout << "PASS: envelope validation, expiry, path bounds, cache saturation, wraparound, 100000 malformed frames\n";
}
