#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "core/config.h"
#include "core/wifi_setup.h"
#include "core/elements.h"
#include "core/sgp4_track.h"
#include "web/web_server.h"

// Boot sequence per CLAUDE.md's Architecture section:
//   boot -> WiFi -> NTP -> fetch elements (CelesTrak) -> init SGP4 -> apogee/perigee
//   loop -> every 1s: SGP4(now) -> lat/lon -> (trail buffer / render - not yet built)
//         -> every 24h: refetch elements
//
// This is Next Steps #1 + #2 from CLAUDE.md: core (WiFi/NTP/NVS/elements
// fetch/serial lat-lon) plus the web config page + captive portal. Display
// rendering (#3) is intentionally not here yet - no panel hardware to test
// against. NOT YET COMPILED ON REAL HARDWARE - see platformio.ini.

ConfigStore config;

static void refetchAndInit() {
    OrbitalElements el;
    if (fetchElements(config.current.noradId, el)) {
        Serial.printf("Fetched elements for %s: %s\n",
                       config.current.noradId.c_str(), el.name.c_str());
    } else {
        Serial.println("Elements fetch failed - using stale fallback (ISS)");
        el = fallbackElements();
    }

    if (!satTrack.init(el)) {
        Serial.println("sgp4init() failed - bad elements?");
        return;
    }
    Serial.printf("%s: apogee %.0f km, perigee %.0f km, period %.1f min\n",
                  el.name.c_str(), satTrack.apogeeKm(), satTrack.perigeeKm(),
                  satTrack.periodMin());
    // TODO once the display renderer exists: clear the trail ring buffer here
    // too, per CLAUDE.md ("Saving triggers an immediate TLE refetch and
    // clears the trail buffer").
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nSatellite Tracker booting...");

    config.begin();

    if (!config.hasWifiCreds() || !wifiSetup.connect(config.current)) {
        Serial.println("No (or failed) WiFi creds - opening \"SatTracker-Setup\" AP...");
        wifiSetup.runCaptivePortal(config);   // never returns; restarts on save
    }
    Serial.printf("WiFi connected, IP %s\n", WiFi.localIP().toString().c_str());

    // All times UTC internally, per CLAUDE.md conventions - hence UTC offset 0.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Waiting for NTP sync");
    time_t now = time(nullptr);
    while (now < 100000) {   // still near the 1970 epoch => not synced yet
        delay(250);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println(" done, " + String((long)now));

    refetchAndInit();

    configWebServer.begin(config, refetchAndInit);
    Serial.printf("Config page: http://%s/\n", WiFi.localIP().toString().c_str());
}

static unsigned long lastSampleMs = 0;
static unsigned long lastRefetchMs = 0;
static const unsigned long REFETCH_INTERVAL_MS = 24UL * 3600UL * 1000UL;

void loop() {
    unsigned long nowMs = millis();

    if (nowMs - lastSampleMs >= 1000) {
        lastSampleMs = nowMs;
        SatPosition pos = satTrack.positionAt(time(nullptr));
        if (pos.valid) {
            Serial.printf("lat %7.2f  lon %7.2f  alt %7.0f km\n", pos.lat, pos.lon, pos.altKm);
        } else {
            Serial.println("propagation error");
        }
        // TODO once the display renderer exists: push (pos.lat, pos.lon) into
        // the trail ring buffer here - sampled every period/300 for high
        // orbits (GEO/Molniya), not every 1s unconditionally, per CLAUDE.md.
    }

    if (nowMs - lastRefetchMs >= REFETCH_INTERVAL_MS) {
        lastRefetchMs = nowMs;
        refetchAndInit();
    }
}
