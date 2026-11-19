#ifdef NWIA_MQTT
#include "NwiaMqtt.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SHA256.h>
#include <Utils.h>
#include <time.h>
#include <esp_mac.h>
#if __has_include("../../../nwia/BrokerCA.h")
#include "../../../nwia/BrokerCA.h"
#else
#include "../../../nwia/BrokerCA.example.h"
#endif

NwiaMqtt nwiaMqtt;

void NwiaMqtt::fingerprint(const mesh::Packet& p, uint8_t hash[16]) const {
  SHA256 sha;
  const uint8_t type = p.getPayloadType();
  sha.update(&type, 1); sha.update(p.payload, p.payload_len); sha.finalize(hash, 16);
}

bool NwiaMqtt::allowed(const mesh::Packet& p) const {
  if (!ready || p.getPayloadType() != PAYLOAD_TYPE_GRP_TXT || !p.isRouteFlood() ||
      p.getPayloadVer() != 0 || p.payload_len < 19 || p.payload[0] != channelHash) return false;
  uint8_t scratch[MAX_PACKET_PAYLOAD];
  // Verify the full configured channel key, not merely its collision-prone 1-byte hash.
  // Keep ciphertext unchanged and never log or publish the decrypted scratch data.
  const int n = mesh::Utils::MACThenDecrypt(channelKey, scratch, p.payload + 1, p.payload_len - 1);
  uint32_t timestamp = 0;
  if (n >= 5) memcpy(&timestamp, scratch, sizeof(timestamp)); // MeshCore little-endian
  memset(scratch, 0, sizeof(scratch));
  const uint32_t now = time(nullptr);
  // Reject replay under a fresh MQTT envelope. Phone/radio clocks must be synced.
  return n >= 5 && nwia::freshOriginal(timestamp, now);
}

void NwiaMqtt::begin(mesh::PacketManager* m) {
  manager = m; bootAt = millis();
  prefs.begin("nwia-mqtt", false);
  auto load = [this](const char* key, char* dest, size_t n) {
    const String value = prefs.getString(key, ""); strlcpy(dest, value.c_str(), n);
  };
  load("ssid", config.ssid, sizeof(config.ssid));
  load("wifiPassword", config.wifiPassword, sizeof(config.wifiPassword));
  load("host", config.host, sizeof(config.host));
  load("user", config.user, sizeof(config.user));
  load("password", config.password, sizeof(config.password));
  load("group", config.group, sizeof(config.group));
  load("channelkey", config.keyHex, sizeof(config.keyHex));
  config.port = prefs.getUShort("port", 8883);
  bool validHex = strlen(config.keyHex) == 32;
  for (const char* p = config.keyHex; *p; ++p) validHex &= isxdigit((unsigned char)*p) != 0;
  const bool hasKey = validHex && mesh::Utils::fromHex(channelKey, 16, config.keyHex);
  SHA256 sha; sha.update(channelKey, 16); sha.finalize(&channelHash, 1);
  esp_read_mac(origin, ESP_MAC_WIFI_STA);
  snprintf(nodeId, sizeof(nodeId), "%02x%02x%02x%02x%02x%02x", origin[0], origin[1], origin[2], origin[3], origin[4], origin[5]);
  snprintf(pubTopic, sizeof(pubTopic), "nwiamesh/v1/%s/packets/%s", config.group, nodeId);
  snprintf(subTopic, sizeof(subTopic), "nwiamesh/v1/%s/packets/+", config.group);
  ready = config.ssid[0] && config.host[0] && config.user[0] && config.password[0] &&
          config.group[0] && hasKey && strlen(NWIA_BROKER_CA) > 64;
  enabled = prefs.getBool("enabled", false);
  outQueue = xQueueCreate(8, sizeof(Wire)); inQueue = xQueueCreate(8, sizeof(Wire));
  if (!outQueue || !inQueue) { ready = false; Serial.println("NWIA: queue allocation failed"); return; }
  // Share core 0 with its idle task, even during expensive TLS crypto. Keep
  // core 1 available for MeshCore, and leave watchdog coverage enabled.
  if (xTaskCreatePinnedToCore(workerEntry, "nwia-mqtt", 12288, this,
                            tskIDLE_PRIORITY, &task, 0) != pdPASS) {
    ready = false; Serial.println("NWIA: worker allocation failed");
  }
  Serial.printf("NWIA gateway %s: %s, forwarding %s; reboot quarantine 65s\n", nodeId,
                ready ? "configured" : "missing configuration or compiled broker CA", enabled ? "on" : "off");
}

void NwiaMqtt::heard(mesh::Packet* p) {
  if (!allowed(*p)) return;
  uint8_t hash[16]; fingerprint(*p, hash);
  if (!cache.mark(hash, nwia::HeardRF, millis())) dropped++;
}

void NwiaMqtt::sent(mesh::Packet* p) {
  // Called only after native repeater forwarding: native scope and hop rules apply.
  if (!enabled || dirty || !connected || !allowed(*p) || uint32_t(millis() - bootAt) < 65000) return;
  const uint32_t now = millis();
  uint8_t hash[16]; fingerprint(*p, hash);
  if (cache.flags(hash, now) & (nwia::Uploaded | nwia::Injected)) { suppressed++; return; }
  if (int32_t(now - nextPublish) < 0) { dropped++; return; }
  uint8_t raw[250]; const int length = p->getRawLength();
  if (length > 250 || length < 0) { dropped++; return; }
  const size_t n = p->writeTo(raw);
  Wire w{}; w.queuedAt = now;
  w.size = nwia::encode(w.bytes, origin, time(nullptr), raw, n);
  if (!w.size || !cache.mark(hash, nwia::Uploaded, now)) { dropped++; return; }
  nextPublish = now + 15000;
  if (xQueueSend(outQueue, &w, 0) != pdTRUE) dropped++;
}

void NwiaMqtt::loop() {
  if (!ready || !enabled || dirty || uint32_t(millis() - bootAt) < 65000) return;
  const uint32_t now = millis();
  Wire w{};
  // Bound callback processing per iteration so a broker cannot monopolize the radio loop.
  for (int count = 0; count < 2 && xQueueReceive(inQueue, &w, 0) == pdTRUE; ++count) {
    nwia::Envelope e{};
    if (!nwia::decode(w.bytes, w.size, time(nullptr), e) || !memcmp(e.origin, origin, 6)) { dropped++; continue; }
    mesh::Packet p;
    if (!p.readFrom(e.raw, e.rawSize) || !allowed(p)) { dropped++; continue; }
    uint8_t hash[16]; fingerprint(p, hash);
    if (cache.flags(hash, now)) { suppressed++; continue; }
    Pending* freeSlot = nullptr;
    bool queued = false;
    for (auto& item : pending) {
      if (item.used && !memcmp(item.hash, hash, 16)) queued = true;
      if (!item.used) freeSlot = &item;
    }
    if (queued) { suppressed++; continue; }
    if (!freeSlot) { dropped++; continue; }
    freeSlot->used = true; freeSlot->due = now + 2000;
    freeSlot->expires = e.created + nwia::kWireTtl;
    freeSlot->size = e.rawSize; memcpy(freeSlot->raw, e.raw, e.rawSize); memcpy(freeSlot->hash, hash, 16);
  }
  for (auto& item : pending) {
    if (!item.used) continue;
    if (cache.flags(item.hash, now)) { item.used = false; suppressed++; continue; }
    if (time(nullptr) >= item.expires) { item.used = false; dropped++; continue; }
    if (int32_t(now - item.due) < 0 || int32_t(now - nextInject) < 0) continue;
    // Reserve identity BEFORE native processing/transmission. No live-cache eviction.
    if (!cache.mark(item.hash, nwia::Injected, now)) { item.used = false; dropped++; continue; }
    mesh::Packet* p = manager->allocNew();
    if (!p) { item.used = false; dropped++; continue; }
    if (!p->readFrom(item.raw, item.size)) { manager->free(p); item.used = false; dropped++; continue; }
    p->_snr = 0; // Synthetic ingress; no RF signal measurement is implied.
    // Preserve route, path and ciphertext. Native Mesh processing validates scope,
    // duplicate hashes, hop limits and path loops before it schedules RF output.
    manager->queueInbound(p, now);
    nextInject = now + 15000; item.used = false; accepted++;
  }
}

void NwiaMqtt::workerEntry(void* self) { static_cast<NwiaMqtt*>(self)->worker(); }
void NwiaMqtt::worker() {
  WiFiClientSecure tls;
  tls.setCACert(NWIA_BROKER_CA); // Arduino-ESP32 2.x uses seconds here, unlike Stream's millisecond API.
  // Handshake work stays on this worker, so the radio loop remains independent.
  tls.setTimeout(2); tls.setHandshakeTimeout(20);
  PubSubClient mqtt(tls);
  mqtt.setServer(config.host, config.port); mqtt.setBufferSize(512); mqtt.setSocketTimeout(2);
  mqtt.setKeepAlive(20);
  mqtt.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
    const String prefix = String("nwiamesh/v1/") + config.group + "/packets/";
    if (!String(topic).startsWith(prefix) || strlen(topic + prefix.length()) != 12 || length > nwia::kMaxWire) { dropped++; return; }
    nwia::Envelope e{};
    if (!nwia::decode(payload, length, time(nullptr), e)) { dropped++; return; }
    char id[13]; snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", e.origin[0], e.origin[1], e.origin[2], e.origin[3], e.origin[4], e.origin[5]);
    if (strcmp(topic + prefix.length(), id)) { dropped++; return; }
    Wire w{}; w.size = length; w.queuedAt = millis(); memcpy(w.bytes, payload, length);
    if (xQueueSend(inQueue, &w, 0) != pdTRUE) dropped++;
  });
  uint32_t lastWifi = 0, lastMqtt = 0;
  for (;;) {
    if (!ready || !enabled || dirty) {
      if (mqtt.connected()) mqtt.disconnect(); connected = false;
      Wire stale; while (xQueueReceive(outQueue, &stale, 0) == pdTRUE) {}
      while (xQueueReceive(inQueue, &stale, 0) == pdTRUE) {}
      vTaskDelay(pdMS_TO_TICKS(200)); continue;
    }
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      connected = false;
      if (!lastWifi || uint32_t(now - lastWifi) > 15000) {
        WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(config.ssid, config.wifiPassword);
        lastWifi = now; configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
      }
    } else if (time(nullptr) >= 1700000000) {
      if (!mqtt.connected() && (!lastMqtt || uint32_t(now - lastMqtt) > 10000)) {
        lastMqtt = now;
        mqtt.connect(nodeId, config.user, config.password, nullptr, 0, false, nullptr, true);
        lastMqtt = millis();
        mqttState = mqtt.state();
        char error[128]{};
        // This Arduino version returns a positive socket descriptor on TLS
        // success; only negative results are TLS failures.
        const int tlsResult = tls.lastError(error, sizeof(error));
        tlsError = tlsResult < 0 ? tlsResult : 0;
        if (tlsResult >= 0) error[0] = 0;
        if (mqtt.connected()) mqtt.subscribe(subTopic, 0);
        else Serial.printf("NWIA connect failed: mqtt=%d tls=%d %s\n",
                           mqttState.load(), tlsError.load(), error);
      }
      connected = mqtt.connected();
      if (connected) {
        mqtt.loop(); Wire w{};
        if (xQueueReceive(outQueue, &w, 0) == pdTRUE) {
          // No outage backlog: queued sends older than five seconds are dropped.
          if (uint32_t(now - w.queuedAt) <= 5000 && mqtt.publish(pubTopic, w.bytes, w.size, false)) published++;
          else dropped++;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool NwiaMqtt::command(char* line, char* reply) {
  if (!strcmp(line, "get nwia.status")) {
    snprintf(reply, 160, "id=%s ready=%d enabled=%d dirty=%d mqtt=%d pub=%lu in=%lu drop=%lu dup=%lu", nodeId,
      int(ready.load()), int(enabled.load()), int(dirty.load()), int(connected.load()),
      (unsigned long)published.load(), (unsigned long)accepted.load(), (unsigned long)dropped.load(), (unsigned long)suppressed.load());
    return true;
  }
  if (!strcmp(line, "get nwia.network")) {
    snprintf(reply, 160, "wifi=%d rssi=%d clock=%lu mqttState=%d tlsError=%d heap=%u stack=%u",
      int(WiFi.status()), int(WiFi.RSSI()), (unsigned long)time(nullptr),
      mqttState.load(), tlsError.load(), unsigned(ESP.getFreeHeap()),
      task ? unsigned(uxTaskGetStackHighWaterMark(task)) : 0);
    return true;
  }
  if (!strcmp(line, "get nwia.config")) {
    snprintf(reply, 160, "broker=%s:%u group=%s user=%s; secrets hidden; pending edits require reboot", config.host, config.port, config.group, config.user);
    return true;
  }
  if (!strncmp(line, "set nwia.", 9)) {
    char* name = line + 9; char* value = strchr(name, ' ');
    if (!value) { strcpy(reply, "ERR missing value"); return true; }
    *value++ = 0;
    if (!strcmp(name, "enabled")) {
      if (strcmp(value, "on") && strcmp(value, "off")) { strcpy(reply, "ERR use on/off"); return true; }
      const bool on = !strcmp(value, "on");
      // Enabling always requires a reboot, ensuring an empty pending queue and quarantine.
      if (!on) enabled = false;
      else dirty = true;
      prefs.putBool("enabled", on);
      strcpy(reply, on ? "OK saved; reboot required" : "OK bridge paused; RF repeater unchanged"); return true;
    }
    size_t max = 0;
    if (!strcmp(name, "ssid")) max = 32;
    else if (!strcmp(name, "wifiPassword")) max = 64;
    else if (!strcmp(name, "host")) max = 95;
    else if (!strcmp(name, "user")) max = 64;
    else if (!strcmp(name, "password")) max = 96;
    else if (!strcmp(name, "group")) {
      max = 32;
      for (const char* p = value; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') { strcpy(reply, "ERR group: letters/digits/-/_ only"); return true; }
    } else if (!strcmp(name, "channelkey")) {
      uint8_t test[16];
      for (const char* p = value; *p; ++p)
        if (!isxdigit((unsigned char)*p)) { strcpy(reply, "ERR invalid hex"); return true; }
      if (strlen(value) != 32 || !mesh::Utils::fromHex(test, 16, value)) { strcpy(reply, "ERR 32 hex characters required"); return true; }
      max = 32;
    } else if (!strcmp(name, "port")) {
      char* end; long n = strtol(value, &end, 10);
      if (*end || n < 1 || n > 65535) { strcpy(reply, "ERR invalid port"); return true; }
      dirty = true; prefs.putUShort("port", n); strcpy(reply, "OK saved; reboot required"); return true;
    } else { strcpy(reply, "ERR unknown NWIA setting"); return true; }
    if (!*value || strlen(value) > max) { strcpy(reply, "ERR empty/too long"); return true; }
    dirty = true; prefs.putString(name, value); strcpy(reply, "OK saved; reboot required"); return true;
  }
  return false;
}
#endif
