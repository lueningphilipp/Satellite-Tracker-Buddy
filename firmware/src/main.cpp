#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "core/config.h"
#include "core/wifi_setup.h"
#include "core/elements.h"
#include "core/retry.h"
#include "core/status.h"
#include "core/request_tracker.h"
#include "core/sgp4_track.h"
#include "core/pass_predict.h"
#include "core/version.h"
#include "core/ota.h"
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
bool online = true;   // did the most recent elements fetch succeed (drives the ONLINE/OFFLINE dot)
time_t launchDate = 0;
bool haveLaunchDate = false;   // launchDate is known AND belongs to the object currently shown

// True while the displayed elements are the hardcoded ISS snapshot rather
// than something fetched live.
static bool shownIsFallback = false;

// Per-NORAD-id caches for the two "enrichment" lookups. A satellite's launch
// date never changes and its n2yo name only rarely does, so once known
// they're kept and not re-requested on every scheduled elements refetch
// (saves a CelesTrak request per cycle). Keyed by id so a cached value is
// never shown next to a different object's orbital numbers - see CLAUDE.md's
// fallback-mismatch gotcha.
static long launchDateNorad = 0;
static long n2yoNameNorad = 0;
static String n2yoNameCache;

// Loop timing state, hoisted up here (rather than declared just before
// loop(), where they used to live) so functions above loop() in the file can
// reference lastRenderMs/forceRenderNow too.
static unsigned long lastSampleMs = 0;
static unsigned long lastRenderMs = 0;

// One retry policy for all three API calls (see core/retry.h): first attempt
// plus up to FetchJob::MAX_RETRIES retries FetchJob::RETRY_DELAY_MS apart,
// transient failures only, then give up until the next scheduled trigger.
// Confirmed on real hardware why the elements job needs this at all: a
// transient failure (DNS resolver blip, router hiccup, CelesTrak briefly
// erroring) at exactly the scheduled fetch used to leave the display on
// stale fallback data for the rest of the interval - up to 24h at the max
// setting - although the network was fine again seconds later. The retries
// stay bounded on purpose so a genuinely down CelesTrak costs a handful of
// requests per outage, never a retry storm (see README's Rate limits).
//
//   elements     periodic (elementsFetchMinutes); armed once at boot and
//                re-armed on every config save.
//   launch date  one-shot, armed by an elements success when the shown
//   n2yo name    object's value isn't cached yet. Once they've given up they
//                wait for the next elements success to try again.
//
// All network I/O runs through serviceFetchJobs().
static FetchJob elementsJob;
static FetchJob launchDateJob;
static FetchJob n2yoJob;
static unsigned long lastWifiScanMs = 0;
static const unsigned long WIFI_SCAN_REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
// WiFi.begin() is only ever called from setup() and the captive-portal flow
// - nothing reconnects after that. Confirmed on real hardware: a WiFi drop
// (router reboot, signal loss, whatever) left the device silently offline
// forever, reachable neither for the config page nor element fetches, while
// SGP4 propagation kept running fine in the background (no network
// dependency there) - only a manual power-cycle brought it back. Checked
// periodically from loop() instead (see below), same "blocking loop() for a
// few seconds is fine, never do this from a request handler" class as the
// WiFi-scan refresh above.
static unsigned long lastWifiCheckMs = 0;
static const unsigned long WIFI_CHECK_INTERVAL_MS = 30UL * 1000UL;
// Set by onConfigSaved() (a config-page save) to force the next loop()
// iteration to redraw immediately, instead of waiting for the next
// scheduled renderIntervalMs tick (up to displayRefreshMinutes away,
// 2 min by default) - a save is something the user is actively watching
// for, unlike the automatic scheduled refetch, which doesn't force this
// (no reason to force an extra visible flash for a quiet background
// refresh). Same reasoning/pattern as passRecomputeNeeded below.
static bool forceRenderNow = false;

// Next-pass prediction (see core/pass_predict.h). haveNextPassInfo gates
// whether a site location is configured at all (siteLat/siteLon both 0.0
// is treated as "not set" - nobody's real site is exactly at 0N,0E, the
// same sentinel-by-convention approach already used elsewhere for this
// pair of fields).
bool haveNextPassInfo = false;
PassState nextPassState = PassState::kNone;
time_t nextPassTime = 0;
static unsigned long lastPassCheckMs = 0;
static const unsigned long PASS_CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL;

// **Real crash found on real hardware, twice, while building this**:
// findNextPass() is a tight CPU loop (thousands of SGP4 calls). It was
// first called directly from the old refetchAndInit() (since split up - see
// applyElements()), which the config page's POST handler also called
// synchronously via onConfigSaved - i.e. on the async_tcp task, exactly like
// the WiFi-scan watchdog crash found earlier this session (see CLAUDE.md's TODO). A single call didn't crash it, but
// a quick sequence of config-page saves did: "Task watchdog got
// triggered... async_tcp... Aborting()... Rebooting." An attempted fix
// with a plain non-atomic reentrancy flag, then a real atomic
// compare-and-swap, both still let two calls land close together (ESP32 is
// dual-core, so the async_tcp-task call and loop()'s own periodic check can
// genuinely run at once) - true concurrent execution isn't really the
// point to prevent here anyway; the *task* is. So: findNextPass() is now
// only ever invoked from loop() (this flag is how applyElements() asks for a
// recompute without doing it itself).
static bool passRecomputeNeeded = true;

// Only call this from loop() - never from applyElements() directly, see
// above.
static void recomputeNextPass() {
    lastPassCheckMs = millis();
    passRecomputeNeeded = false;

    haveNextPassInfo = (config.current.siteLat != 0.0f || config.current.siteLon != 0.0f);
    if (!haveNextPassInfo) return;

    nextPassState = findNextPass(satTrack, config.current.siteLat, config.current.siteLon,
                                  time(nullptr), nextPassTime);
    switch (nextPassState) {
        case PassState::kNow:
            Serial.println("Next pass: overhead now");
            break;
        case PassState::kFound:
            Serial.printf("Next pass: in %.0f min\n", difftime(nextPassTime, time(nullptr)) / 60.0);
            break;
        case PassState::kNone:
            Serial.println("Next pass: none found in search window (orbit may never reach this site)");
            break;
    }
}

// Hold the board's built-in BOOT/FLASH button (GPIO0, active-low, already
// wired on every ESP32-WROOM dev board - no extra button needed) for 3s
// *while the device is already running* to forget the stored WiFi network
// and open the "SatelliteTrackerBuddy-Setup" captive portal. NORAD id/lat-lon/n2yo key
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

// Swaps `el` in as the displayed object: SGP4 init, re-applying a cached n2yo
// name, resetting the trail (its sampling interval scales with the possibly
// new period - CLAUDE.md's "Trail sampling interval... must scale with
// period"), and flagging a next-pass recompute. Returns false, leaving the
// previous state in place, if SGP4 rejects the elements.
static bool applyElements(OrbitalElements el, bool isFallback) {
    if (!satTrack.init(el)) {
        Serial.println("sgp4init() failed - bad elements?");
        return false;
    }
    if (!isFallback && n2yoNameNorad == el.noradCatId && n2yoNameCache.length())
        el.name = n2yoNameCache;

    lastElements = el;
    shownIsFallback = isFallback;
    // Only ever show a launch date that belongs to the object on screen -
    // see CLAUDE.md's fallback-mismatch gotcha (this is what keys off
    // el.noradCatId rather than the requested id).
    haveLaunchDate = (launchDateNorad == el.noradCatId);
    if (isFallback) {
        connStatus.n2yoStatus = config.current.n2yoApiKey.length()
                                     ? "skipped (offline/fallback data)"
                                     : "no key configured";
        if (!haveLaunchDate) connStatus.launchDateStatus = "skipped (offline/fallback data)";
    }
    Serial.printf("%s (%s): apogee %.0f km, perigee %.0f km, period %.1f min\n",
                  el.name.c_str(), satTrack.orbitClass(), satTrack.apogeeKm(),
                  satTrack.perigeeKm(), satTrack.periodMin());

    // New satellite (or refetched elements) -> old trail no longer applies.
    trail.clear();
    trail.setPeriodMinutes(satTrack.periodMin());

    // A new satellite (or a site lat/lon change, which also routes through a
    // refetch via the config page's save handler) means the previous
    // next-pass prediction no longer applies either. Flag it for loop() to
    // actually recompute - see recomputeNextPass()'s comment for why this
    // must never call it directly.
    passRecomputeNeeded = true;
    return true;
}

// Appends the retry outlook to a status string, e.g. "network error - retry
// 1/2 in 2 min" - shown on the config page so an in-progress retry doesn't
// look like a dead end. Permanent errors (HTTP 404, ...) get no suffix.
static void noteRetry(String& status, FetchResult r, bool retryScheduled, const FetchJob& job) {
    if (r != FetchResult::Transient) return;
    int maxRetries = FetchJob::MAX_RETRIES;
    unsigned long delayMin = FetchJob::RETRY_DELAY_MS / 60000UL;
    if (retryScheduled)
        status += " - retry " + String(job.retriesUsed()) + "/" + String(maxRetries)
                  + " in " + String(delayMin) + " min";
    else
        status += " - gave up after " + String(maxRetries) + " retries";
}

static void runElementsJob() {
    OrbitalElements el;
    FetchResult r = fetchElements(config.current.noradId, el, &connStatus.elementsStatus);
    unsigned long intervalMs = (unsigned long)config.current.elementsFetchMinutes * 60UL * 1000UL;
    bool retrying = elementsJob.report(r, millis(), intervalMs);
    noteRetry(connStatus.elementsStatus, r, retrying, elementsJob);
    online = (r == FetchResult::Ok);

    if (!online) {
        Serial.printf("Elements fetch failed (%s)\n", connStatus.elementsStatus.c_str());
        applyElements(fallbackElements(), true);
        return;
    }

    Serial.printf("Fetched elements for %s: %s\n",
                  config.current.noradId.c_str(), el.name.c_str());
    if (!applyElements(el, false)) return;

    // Launch date and n2yo name follow a successful elements fetch, keyed off
    // the elements' own noradCatId (the object actually being displayed), and
    // only when not already cached for it.
    long id = el.noradCatId;
    if (launchDateNorad != id) launchDateJob.arm(millis());
    else launchDateJob.disarm();
    if (config.current.n2yoApiKey.length() == 0) {
        n2yoJob.disarm();
        connStatus.n2yoStatus = "no key configured";
    } else if (n2yoNameNorad != id) {
        n2yoJob.arm(millis());
    } else {
        n2yoJob.disarm();
    }
}

static void runLaunchDateJob() {
    if (shownIsFallback) { launchDateJob.disarm(); return; }
    long id = lastElements.noradCatId;
    time_t out;
    FetchResult r = fetchLaunchDate(String(id), out, &connStatus.launchDateStatus);
    bool retrying = launchDateJob.report(r, millis(), 0);
    noteRetry(connStatus.launchDateStatus, r, retrying, launchDateJob);
    if (r == FetchResult::Ok) {
        launchDate = out;
        launchDateNorad = id;
        haveLaunchDate = true;
    } else {
        Serial.printf("Launch date fetch failed (%s)\n", connStatus.launchDateStatus.c_str());
    }
}

// Best-effort name upgrade via n2yo - only when we're showing the requested
// object (not fallback data) and a key is configured. Skipped entirely
// rather than looked-up-for-the-wrong-object in the fallback case; also
// avoids quietly losing the "(fallback)" suffix that's otherwise the only
// other signal (besides the OFFLINE dot) that this is stale data.
static void runN2yoJob() {
    if (shownIsFallback || config.current.n2yoApiKey.length() == 0) { n2yoJob.disarm(); return; }
    long id = lastElements.noradCatId;
    String name;
    FetchResult r = fetchN2yoName(String(id), config.current.n2yoApiKey, name, &connStatus.n2yoStatus);
    bool retrying = n2yoJob.report(r, millis(), 0);
    noteRetry(connStatus.n2yoStatus, r, retrying, n2yoJob);
    if (r == FetchResult::Ok) {
        Serial.printf("n2yo resolved name: %s\n", name.c_str());
        n2yoNameNorad = id;
        n2yoNameCache = name;
        lastElements.name = name;
    } else {
        Serial.printf("n2yo name lookup failed (%s) - keeping CelesTrak name\n",
                      connStatus.n2yoStatus.c_str());
    }
}

// Runs at most one due fetch job (elements first, so its follow-ups come
// right after) and returns whether it did. Callers drain it with
// `while (serviceFetchJobs()) {}` - every job reschedules itself into the
// future (or disarms) after running, so that always terminates.
static bool serviceFetchJobs() {
    unsigned long now = millis();
    if (elementsJob.due(now))   { runElementsJob();   return true; }
    if (launchDateJob.due(now)) { runLaunchDateJob(); return true; }
    if (n2yoJob.due(now))       { runN2yoJob();       return true; }
    return false;
}

// The config page's onConfigSaved callback: a fresh retry budget, re-ask n2yo
// too (the name may have improved), and redraw as soon as the whole chain has
// finished. Launch date stays cached - it never changes - and is only re-armed
// by the elements job if the object is different.
static void onConfigSaved() {
    n2yoNameNorad = 0;
    n2yoNameCache = "";
    launchDateJob.disarm();
    n2yoJob.disarm();
    elementsJob.arm(millis());
    while (serviceFetchJobs()) {}
    forceRenderNow = true;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\nSatellite Tracker Buddy booting... (firmware %s)\n", FW_VERSION);

    epaperInit();

    config.begin();
    celestrakRequests.begin();
    // Reads whether this is the first boot after an OTA (PENDING_VERIFY)
    // and the "just updated" breadcrumb - no network needed. The health
    // mark / rollback timeout itself runs from loop(), see ota.h.
    ota.begin();

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    if (!config.hasWifiCreds() || !wifiSetup.connect(config.current)) {
        Serial.println("No (or failed) WiFi creds - opening \"SatelliteTrackerBuddy-Setup\" AP...");
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

    // First fetch runs here (not left to loop()) so satTrack is initialised
    // before the first sample/render. A failed one already fell back to the
    // ISS via runElementsJob(); its retries then continue from loop().
    elementsJob.arm(millis());
    while (serviceFetchJobs()) {}

    // One update check per boot, automatically - just sets a flag here
    // (setup() is otherwise a fine place to call ota.loop() directly, since
    // the web server/async_tcp task isn't running yet, but requestCheck()
    // keeps the actual network call on the exact same loop()-only path as
    // everything else, so there's only ever one place that runs it). This
    // only ever populates the status line the user sees on the config page
    // - install still only ever happens from an explicit button click, no
    // auto-install, per the "manual process" decision above.
    ota.requestCheck();

    // Populate the config page's WiFi dropdown before the web server (and
    // its async_tcp task) starts - see wifi_setup.h's scanNetworksHtml() for
    // why this can't be done live inside the page's request handler.
    wifiSetup.refreshCache();

    configWebServer.begin(config, onConfigSaved);
    Serial.printf("Config page: http://%s/\n", WiFi.localIP().toString().c_str());
}

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

    // Scheduled fetches. The elements interval is read live from config when
    // a fetch settles; the config page clamps it to >=10 minutes server-side
    // (see web_server.cpp) to keep even the most aggressive setting well clear
    // of CelesTrak's 50-errors/2h firewall threshold. Retry timing lives in
    // FetchJob - see the comment on the jobs above.
    while (serviceFetchJobs()) {}

    // Keeps the config page's WiFi dropdown from going stale forever. Runs
    // here (blocking loop() for a few seconds, same class of tradeoff as
    // the 24h element refetch above), never inside the page's own request
    // handler - see wifi_setup.h's scanNetworksHtml().
    if (nowMs - lastWifiScanMs >= WIFI_SCAN_REFRESH_INTERVAL_MS) {
        lastWifiScanMs = nowMs;
        wifiSetup.refreshCache();
    }

    // See lastWifiCheckMs's comment above for why this exists at all. A
    // short 5s timeout (vs. the 15s used at boot) keeps a failed attempt
    // from stalling the 1s sample loop for too long - it'll just retry
    // again next interval if the network is genuinely still down.
    if (nowMs - lastWifiCheckMs >= WIFI_CHECK_INTERVAL_MS) {
        lastWifiCheckMs = nowMs;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected - attempting to reconnect...");
            bool reconnected = wifiSetup.connect(config.current, 5000);
            Serial.println(reconnected ? "WiFi reconnected"
                                        : "WiFi reconnect attempt failed - will retry");
        }
    }

    // Keeps the next-pass prediction fresh - only ever computed here in
    // loop(), never from applyElements() directly, see recomputeNextPass()'s
    // comment. Three triggers: (1) passRecomputeNeeded, set by
    // applyElements() (new satellite or a site lat/lon change) - fires
    // regardless of the current haveNextPassInfo, since that flag itself is
    // what this recompute call re-evaluates; (2) a predicted rise time that
    // has actually elapsed, so a stale "pass" doesn't sit there showing a
    // time already in the past; (3) a 5-minute periodic cadence, which also
    // covers the kNow state (it has no natural expiry of its own - without
    // this, a brief pass could keep showing "overhead now" long after the
    // satellite actually set again).
    //
    // Placed BEFORE the render block below on purpose (moved here after a
    // real report: a render that happens on the same loop() iteration a
    // refetch just set passRecomputeNeeded on would otherwise use the
    // stale pre-recompute state - "Next pass" missing or wrong for one
    // whole render cycle right after the exact moment - satellite/config
    // change, or a scheduled refetch - a viewer is most likely to be
    // looking at the screen expecting it to be current).
    bool passTimeElapsed = haveNextPassInfo && nextPassState == PassState::kFound
                            && time(nullptr) >= nextPassTime;
    bool passStale = haveNextPassInfo && (nowMs - lastPassCheckMs >= PASS_CHECK_INTERVAL_MS);
    if (passRecomputeNeeded || passTimeElapsed || passStale) {
        recomputeNextPass();
    }

    // Manual firmware updates + post-update health check - see core/ota.h.
    // Both the manifest check and the download/flash are blocking network
    // work that only ever runs here in loop() (the config page's buttons
    // just set flags - the async_tcp rule again). A successful install
    // ends in ESP.restart(), so nothing below this runs on that iteration.
    //
    // "Healthy" for the purpose of cancelling a rollback: WiFi is up and
    // the TLS/HTTP stack has produced a real response from CelesTrak - an
    // "HTTP 404" for a bad NORAD id proves the stack works just as well as
    // "OK" does, so a mistyped satellite can't get a good build rolled
    // back. Only a connection/TLS-level failure ("network error") or no
    // fetch at all keeps it unhealthy.
    bool wifiUp = WiFi.status() == WL_CONNECTED;
    bool networkHealthy = wifiUp && (online || connStatus.elementsStatus.startsWith("HTTP "));
    ota.loop(config.current.otaManifestUrl, wifiUp, networkHealthy);

    // Read live from config each time (cheap) rather than caching, so a
    // refresh-rate change from the config page takes effect on the very
    // next check - no restart needed, unlike WiFi/hostname changes.
    // forceRenderNow (set by onConfigSaved() above) makes a config-page
    // save redraw immediately instead of waiting for this interval.
    unsigned long renderIntervalMs = (unsigned long)config.current.displayRefreshMinutes * 60UL * 1000UL;
    if (forceRenderNow || nowMs - lastRenderMs >= renderIntervalMs || lastRenderMs == 0) {
        lastRenderMs = nowMs;
        forceRenderNow = false;
        Serial.println("Rendering e-paper...");
        int wifiBars = (WiFi.status() == WL_CONNECTED) ? wifiRssiToBars(WiFi.RSSI()) : 0;
        epaperRender(satTrack, lastElements, trail, time(nullptr), online,
                     haveLaunchDate ? launchDate : (time_t)0, haveLaunchDate,
                     wifiBars,
                     "http://" + WiFi.localIP().toString() + "/",
                     haveNextPassInfo, nextPassState, nextPassTime);
        Serial.println("Render done");
    }
}
