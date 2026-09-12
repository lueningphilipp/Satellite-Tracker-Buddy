#include "web_server.h"
#include "../core/wifi_setup.h"
#include <ESPAsyncWebServer.h>

ConfigWebServer configWebServer;

static AsyncWebServer server(80);

// Favourites per CLAUDE.md. The "a Starlink of the day" idea needs a runtime
// picker (e.g. rotate through a small list, or hit CelesTrak's starlink
// group and grab one) - not implemented yet, left as a TODO rather than
// guessed at.
struct Favourite { const char* name; const char* norad; };
static const Favourite FAVOURITES[] = {
    {"ISS", "25544"},
    {"Tiangong", "48274"},
    {"Hubble", "20580"},
    {"NOAA-19", "33591"},
};

static const char PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Satellite Tracker</title>
<style>body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em}
input,select{width:100%;box-sizing:border-box;padding:.5em;margin:.3em 0 1em}
label{font-size:.85em;color:#555}
button{padding:.6em 1em;border:0;border-radius:4px;background:#222;color:#fff;margin:.2em}
.fav{background:#567}
.save{width:100%;padding:.8em;background:#284;font-size:1.05em;margin-top:1em}
hr{border:0;border-top:1px solid #ddd;margin:1.5em 0}
</style></head><body>
<h2>Satellite Tracker</h2>
<form method="POST" action="/config">
  <label>NORAD catalog id</label>
  <input name="norad" id="norad" value="%NORAD%" required>
  <div>%FAVBUTTONS%</div>

  <label>Site latitude</label>
  <input name="lat" value="%LAT%" type="number" step="any">
  <label>Site longitude</label>
  <input name="lon" value="%LON%" type="number" step="any">

  <label>n2yo API key (optional - used to resolve the real name sooner for
  freshly-launched objects CelesTrak still shows generically)</label>
  <input name="n2yo" value="%N2YO%">

  <label>Display full-refresh interval (minutes)</label>
  <input name="refresh" value="%REFRESH%" type="number" min="1" max="60" step="1">

  <label>Device hostname (shown to your router/DHCP; requires a reconnect
  to take effect)</label>
  <input name="hostname" value="%HOSTNAME%">

  <hr>
  <label>WiFi network - currently <b>%CURSSID%</b>. Pick a nearby network or
  type one below to switch (leave the name blank to keep the current
  network); saving a new network restarts the device to reconnect.</label>
  <select onchange="document.getElementById('ssid').value=this.value">
    <option value="">-- choose one, or type below --</option>
    %WIFIOPTIONS%
  </select>
  <label>Network name (SSID)</label>
  <input id="ssid" name="ssid" placeholder="leave blank to keep current network">
  <label>Password</label>
  <input name="pass" type="password">

  <button class="save" type="submit">Save &amp; refetch</button>
</form>
<script>
function pick(id){document.getElementById('norad').value=id;}
</script>
</body></html>
)HTML";

static String buildFavButtons() {
    String html;
    for (auto& f : FAVOURITES) {
        html += "<button type=\"button\" class=\"fav\" onclick=\"pick('";
        html += f.norad;
        html += "')\">";
        html += f.name;
        html += "</button>";
    }
    return html;
}

static String renderPage(const DeviceConfig& cfg) {
    String page = PAGE_TEMPLATE;
    page.replace("%NORAD%", cfg.noradId);
    page.replace("%FAVBUTTONS%", buildFavButtons());
    page.replace("%LAT%", String(cfg.siteLat, 4));
    page.replace("%LON%", String(cfg.siteLon, 4));
    page.replace("%N2YO%", cfg.n2yoApiKey);
    page.replace("%REFRESH%", String(cfg.displayRefreshMinutes));
    page.replace("%HOSTNAME%", cfg.hostname);
    // Current SSID only - never the password, so it can't leak into a page
    // source view.
    page.replace("%CURSSID%", cfg.wifiSsid.length() ? cfg.wifiSsid : "(not set)");
    // Cached, not a live scan - see wifi_setup.h's scanNetworksHtml() for
    // why a live scan can't safely happen inside this request handler.
    page.replace("%WIFIOPTIONS%", wifiSetup.cachedOptionsHtml());
    return page;
}

void ConfigWebServer::begin(ConfigStore& store, std::function<void()> onConfigSaved) {
    server.on("/", HTTP_GET, [&store](AsyncWebServerRequest* req) {
        req->send(200, "text/html", renderPage(store.current));
    });

    // REST GET, per CLAUDE.md's "REST: GET/POST /config" - handy for the
    // eventual button-cycle-favourites firmware to read state without
    // scraping HTML.
    server.on("/config", HTTP_GET, [&store](AsyncWebServerRequest* req) {
        String json = "{";
        json += "\"norad\":\"" + store.current.noradId + "\",";
        json += "\"lat\":" + String(store.current.siteLat, 4) + ",";
        json += "\"lon\":" + String(store.current.siteLon, 4) + ",";
        json += "\"hasN2yoKey\":" + String(store.current.n2yoApiKey.length() > 0 ? "true" : "false") + ",";
        json += "\"hostname\":\"" + store.current.hostname + "\",";
        json += "\"refreshMinutes\":" + String(store.current.displayRefreshMinutes) + ",";
        json += "\"wifiSsid\":\"" + store.current.wifiSsid + "\"";
        json += "}";
        req->send(200, "application/json", json);
    });

    server.on("/config", HTTP_POST, [&store, onConfigSaved](AsyncWebServerRequest* req) {
        if (req->hasParam("norad", true))
            store.current.noradId = req->getParam("norad", true)->value();
        if (req->hasParam("lat", true))
            store.current.siteLat = req->getParam("lat", true)->value().toFloat();
        if (req->hasParam("lon", true))
            store.current.siteLon = req->getParam("lon", true)->value().toFloat();
        if (req->hasParam("n2yo", true))
            store.current.n2yoApiKey = req->getParam("n2yo", true)->value();
        if (req->hasParam("refresh", true)) {
            int mins = req->getParam("refresh", true)->value().toInt();
            // Clamp rather than trust the form's min/max, which a manual
            // POST could bypass - 0/negative would wedge the render check
            // (nowMs - lastRenderMs >= 0 is always true, i.e. redraw every
            // loop) and this is a slow, visibly-flashing full refresh, not
            // something to ever run that often.
            if (mins < 1) mins = 1;
            if (mins > 60) mins = 60;
            store.current.displayRefreshMinutes = mins;
        }

        // Hostname and WiFi changes only take effect on a fresh WiFi.begin()
        // (see wifi_setup.cpp's connect()), so either one triggers a
        // restart after saving - same as the captive portal's own /save.
        bool needsRestart = false;
        if (req->hasParam("hostname", true)) {
            String newHostname = req->getParam("hostname", true)->value();
            if (newHostname != store.current.hostname) needsRestart = true;
            store.current.hostname = newHostname;
        }
        // Blank SSID means "leave WiFi alone" - the current network's name
        // is only ever shown, never blanked out via this form.
        if (req->hasParam("ssid", true) && req->getParam("ssid", true)->value().length() > 0) {
            store.current.wifiSsid = req->getParam("ssid", true)->value();
            store.current.wifiPass = req->hasParam("pass", true)
                                          ? req->getParam("pass", true)->value()
                                          : "";
            needsRestart = true;
        }

        store.save();

        if (needsRestart) {
            req->send(200, "text/html",
                       "<html><body><h3>Saved. Restarting to reconnect...</h3></body></html>");
            delay(500);
            ESP.restart();
            return;
        }

        req->send(200, "text/html",
                   "<html><body><p>Saved. Refetching elements for "
                   + store.current.noradId + "...</p><a href=\"/\">Back</a></body></html>");
        if (onConfigSaved) onConfigSaved();
    });

    server.begin();
}
