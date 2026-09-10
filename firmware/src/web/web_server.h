#pragma once
#include <Arduino.h>
#include <functional>
#include "../core/config.h"

// The full config page, served once the device is online (separate from
// wifi_setup.h's tiny captive-portal page). Per CLAUDE.md:
//   "Config UI: the device serves a small web page (ESPAsyncWebServer) at
//    http://<ip>/ with a field for the NORAD id plus a short list of
//    favourites... Saving triggers an immediate TLE refetch and clears the
//    trail buffer."
//
// `onConfigSaved` is called after a successful POST /config save, so
// main.cpp can refetch elements / reinit SGP4 / (later) clear the trail
// buffer without web_server.cpp needing to know about any of that.

class ConfigWebServer {
public:
    void begin(ConfigStore& store, std::function<void()> onConfigSaved);
};

extern ConfigWebServer configWebServer;
