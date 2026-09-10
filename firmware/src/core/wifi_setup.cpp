#include "wifi_setup.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

WiFiSetup wifiSetup;

static const char* AP_SSID = "SatTracker-Setup";
static const byte DNS_PORT = 53;

// Minimal WiFi setup page. Deliberately not the full config page (NORAD id /
// lat-lon / n2yo key) - this only needs to get the device onto a network so
// it can reach core/web_server.h's fuller page afterwards.
static const char SETUP_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Satellite Tracker Setup</title>
<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}
input{width:100%;box-sizing:border-box;padding:.5em;margin:.4em 0}
button{width:100%;padding:.7em;background:#222;color:#fff;border:0;border-radius:4px}</style>
</head><body>
<h2>Connect to WiFi</h2>
<form method="POST" action="/save">
  <label>Network name (SSID)</label>
  <input name="ssid" required>
  <label>Password</label>
  <input name="pass" type="password">
  <button type="submit">Save &amp; connect</button>
</form>
</body></html>
)HTML";

bool WiFiSetup::connect(DeviceConfig& config, uint32_t timeoutMs) {
    if (config.wifiSsid.length() == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

void WiFiSetup::runCaptivePortal(ConfigStore& store) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    DNSServer dns;
    dns.start(DNS_PORT, "*", apIP);   // redirect every hostname to us

    AsyncWebServer server(80);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", SETUP_PAGE);
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
    // Captive-portal probes (Android/iOS/Windows all poll a well-known path
    // expecting either a redirect or specific content to detect the portal).
    // Answering every unknown path with our page is the simplest catch-all.
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", SETUP_PAGE);
    });
    server.begin();

    // Blocks here until /save triggers ESP.restart() above.
    for (;;) {
        dns.processNextRequest();
        delay(10);
    }
}
