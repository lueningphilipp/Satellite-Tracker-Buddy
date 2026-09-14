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

    // Opens "SatelliteTrackerBuddy-Setup" AP + captive portal, serves a WiFi setup page,
    // saves whatever the user submits to NVS, then restarts the device.
    // Never returns (ESP.restart() at the end).
    void runCaptivePortal(ConfigStore& store);

    // Scans for nearby networks and returns them as <option> elements
    // (strongest signal first, deduped by SSID, open networks flagged) for
    // a <select> dropdown. Shared by the captive portal's own page and the
    // main config page's "change WiFi" section (core/web_server.h) so
    // scanning/formatting logic lives in exactly one place.
    //
    // Blocking (~2-6s) - ONLY safe to call from setup(), main.cpp's loop()
    // (delays the next 1s sample/render, same as the existing 24h element
    // refetch - acceptable), or the captive portal's own pre-server-start
    // code. Never call this from inside a live AsyncWebServer request
    // handler: those run on the "async_tcp" FreeRTOS task, which has its
    // own watchdog - blocking it for several seconds aborts/reboots the
    // whole device. Confirmed on real hardware: the main config page
    // originally called this straight from its GET "/" handler and crashed
    // with "Task watchdog got triggered... async_tcp" within seconds of
    // boot. web_server.cpp now reads cachedOptionsHtml() instead, kept
    // fresh by refreshCache() calls from safe contexts.
    String scanNetworksHtml();

    // Safe to call from anywhere, including live request handlers - just
    // returns whatever refreshCache() last computed (empty until the first
    // refresh).
    String cachedOptionsHtml() const { return cachedOptions; }
    void refreshCache() { cachedOptions = scanNetworksHtml(); }

private:
    String cachedOptions;
};

extern WiFiSetup wifiSetup;
