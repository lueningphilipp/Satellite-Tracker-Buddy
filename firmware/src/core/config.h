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
    String hostname = "sattracker";      // DHCP/mDNS-visible device name
    int displayRefreshMinutes = 2;       // full e-paper refresh interval
};

class ConfigStore {
public:
    void begin();                  // load from NVS into `current`
    void save();                   // persist `current` back to NVS
    void clearWifiCreds();         // wipe just wifiSsid/wifiPass (NORAD id/lat-lon/n2yo key survive)
    bool hasWifiCreds() const { return current.wifiSsid.length() > 0; }

    DeviceConfig current;
};
