#include "wifi_setup.h"
#include "../display/epaper_render.h"   // epaperRenderSetup(): on-panel setup instructions
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

WiFiSetup wifiSetup;

int wifiRssiToBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    return 1;
}

static const char* AP_SSID = "SatelliteTrackerBuddy-Setup";
static const byte DNS_PORT = 53;
static const int MAX_SCAN_RESULTS = 64;   // plenty for any real-world scan; just a stack-array cap

static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

String WiFiSetup::scanNetworksHtml() {
    int n = WiFi.scanNetworks();
    int order[MAX_SCAN_RESULTS];
    int count = 0;
    for (int i = 0; i < n && count < MAX_SCAN_RESULTS; i++) {
        if (WiFi.SSID(i).length() == 0) continue;
        order[count++] = i;
    }
    // Selection sort by RSSI descending - count is at most MAX_SCAN_RESULTS,
    // so O(n^2) is negligible and avoids pulling in <algorithm>.
    for (int i = 0; i < count - 1; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++) {
            if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[best])) best = j;
        }
        int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }

    String options;
    String seen[MAX_SCAN_RESULTS];
    int seenCount = 0;
    for (int k = 0; k < count; k++) {
        int i = order[k];
        String ssid = WiFi.SSID(i);
        bool dup = false;
        for (int s = 0; s < seenCount; s++) {
            if (seen[s] == ssid) { dup = true; break; }
        }
        if (dup) continue;
        if (seenCount < MAX_SCAN_RESULTS) seen[seenCount++] = ssid;

        bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        String esc = htmlEscape(ssid);
        options += "<option value=\"" + esc + "\">" + esc + " (" + String(WiFi.RSSI(i)) +
                   " dBm)" + (open ? " - open" : "") + "</option>";
    }
    WiFi.scanDelete();
    return options;
}

// Builds the setup page with the scanned network list baked in as a
// <select> that fills the SSID text field on change - the field stays the
// thing that's actually submitted, so hidden/out-of-range networks can
// still be entered by hand exactly as before.
static String buildSetupPage() {
    String page = F(
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Satellite Tracker Buddy Setup</title>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
        "input,select{width:100%;box-sizing:border-box;padding:.5em;margin:.4em 0}"
        "button{width:100%;padding:.7em;background:#222;color:#fff;border:0;border-radius:4px}</style>"
        "</head><body>"
        "<h2>Connect to WiFi</h2>"
        "<form method=\"POST\" action=\"/save\">"
        "<label>Nearby networks</label>"
        "<select onchange=\"document.getElementById('ssid').value=this.value\">"
        "<option value=\"\">-- choose one, or type below --</option>");
    page += wifiSetup.scanNetworksHtml();
    page += F(
        "</select>"
        "<label>Network name (SSID)</label>"
        "<input id=\"ssid\" name=\"ssid\" required>"
        "<label>Password</label>"
        "<input name=\"pass\" type=\"password\">"
        "<button type=\"submit\">Save &amp; connect</button>"
        "</form></body></html>");
    return page;
}

bool WiFiSetup::connect(DeviceConfig& config, uint32_t timeoutMs) {
    if (config.wifiSsid.length() == 0) return false;

    WiFi.mode(WIFI_STA);
    // Must be set after mode(WIFI_STA) but before begin() - the ESP32
    // Arduino core silently ignores setHostname() calls made outside that
    // window (e.g. it has no effect on an already-connected session, so a
    // hostname change from the config page needs a reconnect to take hold).
    if (config.hostname.length()) WiFi.setHostname(config.hostname.c_str());
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

void WiFiSetup::runCaptivePortal(ConfigStore& store) {
    // AP_STA (not plain AP) so the radio can still scan for nearby networks
    // while the softAP is up - the scan needs STA mode active, and dropping
    // AP mode to get it would kick anyone already connected to the portal.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    // Tell whoever is looking at the device what to do - without this the
    // panel just stays blank until WiFi is configured. Full e-paper refresh
    // (~3s) happens here, before the server starts, so it can't hold up or
    // race a request handler (see the async_tcp gotcha in CLAUDE.md).
    epaperRenderSetup(AP_SSID, apIP.toString());

    // One scan up front, baked into the page once - good enough for a
    // one-time setup screen; a network that appears after the portal's
    // already loaded just isn't in the list (re-loading "/" rescans, so a
    // manual refresh picks it up).
    String setupPage = buildSetupPage();

    DNSServer dns;
    dns.start(DNS_PORT, "*", apIP);   // redirect every hostname to us

    AsyncWebServer server(80);
    server.on("/", HTTP_GET, [&setupPage](AsyncWebServerRequest* req) {
        req->send(200, "text/html", setupPage);
    });
    server.on("/save", HTTP_POST, [&store](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid", true)) {
            req->send(400, "text/plain", "missing ssid");
            return;
        }
        store.current.wifiSsid = req->getParam("ssid", true)->value();
        store.current.wifiPass = req->hasParam("pass", true)
                                      ? req->getParam("pass", true)->value()
                                      : "";
        store.save();
        req->send(200, "text/html",
                   "<html><body><h3>Saved. Restarting...</h3></body></html>");
        delay(500);
        ESP.restart();
    });
    // Captive-portal probes (Android's /generate_204, iOS/macOS's
    // hotspot-detect.html, Windows' ncsi.txt, ...) all land here via the
    // wildcard DNS above, since none of them match a route we registered.
    // A flat 200+HTML response (what this used to send) is NOT reliably
    // recognized as "needs sign-in" by modern Android/iOS - they expect
    // something other than their exact "connected" response (204 / the
    // literal string "Success" / etc), and a redirect is the form most
    // consistently understood to mean "there's a portal, go here" - without
    // it the phone just marks the network "no internet" and never auto-pops
    // the sign-in sheet, even though the AP and page both work fine if you
    // open a browser manually. Confirmed needed: real device connected to
    // the AP but got no sign-in prompt with the plain-200 version.
    server.onNotFound([apIP](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(302, "text/plain", "");
        res->addHeader("Location", "http://" + apIP.toString() + "/");
        req->send(res);
    });
    server.begin();

    // Blocks here until /save triggers ESP.restart() above.
    for (;;) {
        dns.processNextRequest();
        delay(10);
    }
}
