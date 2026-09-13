#include "geoip.h"
#include <HTTPClient.h>
#include <string.h>   // strlen

bool fetchGeoIpLocation(float& outLat, float& outLon, String* errOut) {
    HTTPClient http;
    // Plain http:// - HTTPClient's single-arg begin() uses a plain
    // WiFiClient for this (no setInsecure() dance needed, unlike the
    // HTTPS calls elsewhere in this codebase).
    if (!http.begin("http://ip-api.com/json/")) {
        if (errOut) *errOut = "couldn't start request";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("geoip fetch: HTTP %d\n", code);
        http.end();
        if (errOut) *errOut = code > 0 ? ("HTTP " + String(code)) : "network error";
        return false;
    }
    String body = http.getString();
    http.end();

    // Plain string search, same style as fetchLaunchDate()/fetchN2yoName()
    // in elements.cpp - two fixed-format numeric fields don't need a full
    // JSON parse (ArduinoJson is a listed dependency but deliberately
    // unused elsewhere in this codebase for exactly this reason).
    const char* latKey = "\"lat\":";
    int latIdx = body.indexOf(latKey);
    const char* lonKey = "\"lon\":";
    int lonIdx = body.indexOf(lonKey);
    if (latIdx < 0 || lonIdx < 0) {
        if (errOut) *errOut = "no lat/lon in response";
        return false;
    }
    outLat = body.substring(latIdx + strlen(latKey)).toFloat();
    outLon = body.substring(lonIdx + strlen(lonKey)).toFloat();
    if (errOut) *errOut = "OK";
    return true;
}
