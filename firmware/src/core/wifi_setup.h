#pragma once
#include <Arduino.h>
#include "config.h"

// WiFiManager-style first-boot flow, per CLAUDE.md:
//   "First boot with no WiFi creds: open an AP (captive portal, WiFiManager-
//    style) that shows the same page."
//
// Usage from main.cpp:
//   if (!wifiSetup.connect(config)) {
//       wifiSetup.runCaptivePortal(config);   // blocks until creds saved + device restarts
//   }
//
// The captive portal's own tiny page only asks for WiFi SSID/password - the
// full NORAD id / site lat-lon / n2yo key config page (core/web_server.h)
// only makes sense once the device is actually online, so it's a separate,
// later step, not part of this first-boot flow.

class WiFiSetup {
public:
    // Tries to join the stored network. Returns true once connected (or
    // immediately false if no creds are stored at all).
    bool connect(DeviceConfig& config, uint32_t timeoutMs = 15000);

    // Opens "SatTracker-Setup" AP + captive portal, serves a WiFi setup page,
    // saves whatever the user submits to NVS, then restarts the device.
    // Never returns (ESP.restart() at the end).
    void runCaptivePortal(ConfigStore& store);
};

extern WiFiSetup wifiSetup;
