#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "core/config.h"
#include "core/wifi_setup.h"
#include "core/elements.h"
#include "core/sgp4_track.h"
#include "web/web_server.h"
#include "display/epaper_render.h"
#include "display/trail_buffer.h"

// Boot sequence per CLAUDE.md's Architecture section:
//   boot -> WiFi -> NTP -> fetch elements (CelesTrak) -> init SGP4 -> apogee/perigee
//   loop -> every 1s: SGP4(now) -> lat/lon -> push to trail ring buffer -> render()
//         -> every 24h: refetch elements
//
// All of Next Steps #1-#3 are now wired together. Core (WiFi/NTP/elements/
// SGP4) and the config page are verified on real hardware; the e-paper
// renderer is new and only hello-world-level verified (panel/wiring/GxEPD2
// class confirmed correct) - the actual map/track/text output hasn't been
// seen on the physical panel yet.

ConfigStore config;
TrailBuffer trail;
OrbitalElements lastElements;   // kept around so loop() can pass it to epaperRender()
bool online = true;
time_t launchDate = 0;
bool haveLaunchDate = false;

// Hold the board's built-in BOOT/FLASH button (GPIO0, active-low, already
// wired on every ESP32-WROOM dev board - no extra button needed) for 3s
// *while the device is already running* to forget the stored WiFi network
// and open the "SatTracker-Setup" captive portal. NORAD id/lat-lon/n2yo key
// are untouched.
//
// This must be sampled in loop(), NOT at/before setup() - GPIO0 doubles as
// the chip's boot-mode strapping pin: if it's held LOW at the instant the
// chip comes out of reset, the ROM bootloader drops straight into the UART
// download/flashing mode instead of ever running our app, so a check at the
// top of setup() can never fire for the "hold through power-on" gesture that
// seems like the obvious way to use it (confirmed on real hardware: held
// through power-up, zero app output, matches exactly what "stuck in the
// flashing bootloader" looks like). Holding it only after boot has already
// reached loop() avoids that strapping window entirely - and clearing creds
// here calls wifiSetup.runCaptivePortal() directly in-process (no
// ESP.restart()) so we never trigger a fresh hardware reset while a finger
// might still be on the button.
static const int8_t BOOT_BUTTON_PIN = 0;
// Onboard LED most ESP32-WROOM dev boards carry on GPIO2 (Arduino core
// doesn't define LED_BUILTIN for the generic "esp32dev" board, but this is
// the near-universal convention). Only used for the confirmation blink - if
// this particular board has no LED there, it's just an unused GPIO toggling.
static const int8_t STATUS_LED_PIN = 2;
static unsigned long buttonHeldSinceMs = 0;   // 0 = not currently held

static void refetchAndInit() {
    OrbitalElements el;
    online = fetchElements(config.current.noradId, el);
    if (online) {
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
    lastElements = el;
    Serial.printf("%s (%s): apogee %.0f km, perigee %.0f km, period %.1f min\n",
                  el.name.c_str(), satTrack.orbitClass(), satTrack.apogeeKm(),
                  satTrack.perigeeKm(), satTrack.periodMin());

    haveLaunchDate = fetchLaunchDate(config.current.noradId, launchDate);
    if (!haveLaunchDate) Serial.println("Launch date fetch failed - time-in-space will be hidden");

    // New satellite selected (or refetched) -> old trail no longer applies,
    // and its sampling interval scales with the (possibly new) period, per
    // CLAUDE.md's "Saving triggers an immediate TLE refetch and clears the
    // trail buffer" + "Trail sampling interval... must scale with period".
    trail.clear();
    trail.setPeriodMinutes(satTrack.periodMin());
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nSatellite Tracker booting...");

    epaperInit();

    config.begin();

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

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
static unsigned long lastRenderMs = 0;
static unsigned long lastRefetchMs = 0;
static const unsigned long REFETCH_INTERVAL_MS = 24UL * 3600UL * 1000UL;
static const unsigned long RENDER_INTERVAL_MS = 2UL * 60UL * 1000UL;   // full refresh every 2 min

void loop() {
    unsigned long nowMs = millis();

    // See the big comment by BOOT_BUTTON_PIN's declaration for why this is
    // sampled here (mid-run) and not in setup().
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (buttonHeldSinceMs == 0) buttonHeldSinceMs = nowMs;
        if (nowMs - buttonHeldSinceMs >= 3000) {
            Serial.println("BOOT held 3s - clearing stored WiFi credentials");
            // Fast 5-blink sequence confirms the wipe without needing a
            // serial monitor, and doubles as "you can let go of BOOT now" -
            // release it before the portal's eventual ESP.restart() on save,
            // or that reset will re-strap GPIO0 into the flashing bootloader
            // the same way holding it through power-on does.
            for (int i = 0; i < 5; i++) {
                digitalWrite(STATUS_LED_PIN, HIGH);
                delay(100);
                digitalWrite(STATUS_LED_PIN, LOW);
                delay(100);
            }
            config.clearWifiCreds();
            wifiSetup.runCaptivePortal(config);   // never returns; restarts on save
        }
    } else {
        buttonHeldSinceMs = 0;
    }

    if (nowMs - lastSampleMs >= 1000) {
        lastSampleMs = nowMs;
        time_t t = time(nullptr);
        SatPosition pos = satTrack.positionAt(t);
        if (pos.valid) {
            Serial.printf("t=%ld  lat %7.2f  lon %7.2f  alt %7.0f km\n",
                           (long)t, pos.lat, pos.lon, pos.altKm);
            trail.maybeSample(t, pos.lat, pos.lon);
        } else {
            Serial.println("propagation error");
        }
    }

    if (nowMs - lastRenderMs >= RENDER_INTERVAL_MS || lastRenderMs == 0) {
        lastRenderMs = nowMs;
        Serial.println("Rendering e-paper...");
        epaperRender(satTrack, lastElements, trail, time(nullptr), online,
                     haveLaunchDate ? launchDate : (time_t)0, haveLaunchDate);
        Serial.println("Render done");
    }

    if (nowMs - lastRefetchMs >= REFETCH_INTERVAL_MS) {
        lastRefetchMs = nowMs;
        refetchAndInit();
    }
}
