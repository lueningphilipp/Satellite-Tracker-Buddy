#pragma once
#include <Arduino.h>

// Best-effort site location from ip-api.com's free IP-geolocation service -
// approximates lat/lon from the device's own internet-facing IP (city-level
// accuracy typically, not GPS-precise). Fallback for the config page's
// "Use my location" button: many browsers restrict the Geolocation JS API
// to secure (HTTPS) origins, and this device only serves plain HTTP, so
// browser geolocation often just fails there ("Only secure origins are
// allowed" - confirmed happening on real hardware/browser). A plain HTTP
// request *from* the ESP32 to an external API has no such restriction -
// "secure context" is a browser page rule, not a networking one - so this
// sidesteps the problem entirely rather than working around it.
//
// Free tier is HTTP-only (their HTTPS endpoint needs a paid plan), no API
// key, 45 requests/min - fine for a button a person clicks occasionally.
// Same privacy footprint as the CelesTrak/n2yo calls this firmware already
// makes: it reveals the device's public IP to a third party, which is
// inherent to how IP geolocation works and true of every internet request
// the device makes already, not something new this adds.
bool fetchGeoIpLocation(float& outLat, float& outLon, String* errOut = nullptr);
