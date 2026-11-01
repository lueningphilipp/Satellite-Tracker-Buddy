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
//
// Slideshow pictures (see core/slide_store.h) are uploaded/deleted through
// their own endpoints, outside the main form; `onSlidesChanged(newId)` tells
// main.cpp the set changed (newId = a fresh upload to jump to, else 0);
// `onSlideStep(dir)` is the page's Previous (-1) / Next (+1) buttons.

class ConfigWebServer {
public:
    void begin(ConfigStore& store, std::function<void()> onConfigSaved,
               std::function<void(long)> onSlidesChanged,
               std::function<void(int)> onSlideStep);
};

extern ConfigWebServer configWebServer;
