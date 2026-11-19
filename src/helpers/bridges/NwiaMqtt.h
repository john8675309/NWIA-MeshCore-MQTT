#pragma once
#ifdef NWIA_MQTT
#include <Arduino.h>
#include <Mesh.h>
#include <Preferences.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "../../../nwia/BridgeCore.h"

class NwiaMqtt {
 public:
  void begin(mesh::PacketManager* manager);
  void loop();
  void heard(mesh::Packet* packet);
  void sent(mesh::Packet* packet);
  bool command(char* line, char* reply);
  bool allPackets() const { return config.allPackets; }
  void unforwarded(const mesh::Packet& original);
  bool active() const { return enabled.load(); }
 private:
  struct Config {
    char ssid[33]{}, wifiPassword[65]{}, host[96]{}, user[65]{}, password[97]{};
    char group[33]{}, keyHex[33]{};
    uint16_t port = 8883;
    bool allPackets = true;
  } config;
  struct Wire { uint16_t size; uint8_t bytes[nwia::kMaxWire]; uint32_t queuedAt; };
  struct Pending { bool used = false; uint32_t due, expires; uint8_t raw[nwia::kMaxRaw]; uint8_t size; uint8_t hash[16]; } pending[8];
  mesh::PacketManager* manager = nullptr;
  Preferences prefs;
  QueueHandle_t outQueue = nullptr, inQueue = nullptr;
  TaskHandle_t task = nullptr;
  uint8_t origin[6]{}, channelKey[32]{}, channelHash = 0;
  char nodeId[13]{}, pubTopic[120]{}, subTopic[120]{};
  nwia::Cache<1024> cache;
  uint32_t bootAt = 0, nextInject = 0, nextPublish = 0;
  std::atomic<bool> enabled{false}, dirty{false}, connected{false}, ready{false};
  std::atomic<int> mqttState{-1}, tlsError{0};
  std::atomic<uint32_t> published{0}, accepted{0}, dropped{0}, suppressed{0};
  nwia::Mode mode() const { return config.allPackets ? nwia::Mode::All : nwia::Mode::Channel; }
  void publish(const mesh::Packet& packet);
  bool allowed(const mesh::Packet& p) const;
  void fingerprint(const mesh::Packet& p, uint8_t hash[16]) const;
  static void workerEntry(void* self);
  void worker();
};
extern NwiaMqtt nwiaMqtt;
#endif
