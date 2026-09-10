#pragma once
#include <Arduino.h>

// Everything the device remembers across reboots, per CLAUDE.md's "Satellite
// selection" section: WiFi creds, NORAD id, site lat/lon, n2yo API key.
// Backed by ESP32 NVS via the Preferences library. Default satellite is the
// ISS (25544) on first boot, per spec.

struct DeviceConfig {
    String wifiSsid;
    String wifiPass;
    String noradId = "25544";      // default: ISS
    float siteLat = 0.0f;
    float siteLon = 0.0f;
    String n2yoApiKey;             // optional - empty means "skip name lookup"
};

class ConfigStore {
public:
    void begin();                  // load from NVS into `current`
    void save();                   // persist `current` back to NVS
    bool hasWifiCreds() const { return current.wifiSsid.length() > 0; }

    DeviceConfig current;
};
