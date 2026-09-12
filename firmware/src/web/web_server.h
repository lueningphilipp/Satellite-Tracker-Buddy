#pragma once
#include <Arduino.h>
#include <functional>
#include "../core/config.h"

// The full config page, served once the device is online (separate from
// wifi_setup.h's tiny first-boot captive-portal page). Per CLAUDE.md:
//   "Config UI: the device serves a small web page (ESPAsyncWebServer) at
//    http://<ip>/ with a field for the NORAD id plus a short list of
//    favourites... Saving triggers an immediate TLE refetch and clears the
//    trail buffer."
//
// Also covers changing WiFi networks after first setup (a scanned-network
// dropdown + SSID/password, reusing wifi_setup.h's scan/format helper) and
// setting the display refresh interval and device hostname. `onConfigSaved`
// is called after a POST /config save that *didn't* need a restart (WiFi/
// hostname changes always restart to take effect - see web_server.cpp), so
// main.cpp can refetch elements / reinit SGP4 / clear the trail buffer
// without web_server.cpp needing to know about any of that.

class ConfigWebServer {
public:
    void begin(ConfigStore& store, std::function<void()> onConfigSaved);
};

extern ConfigWebServer configWebServer;
