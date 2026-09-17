#include "web_server.h"
#include "../core/wifi_setup.h"
#include "../core/status.h"
#include "../core/geoip.h"
#include "../core/request_tracker.h"
#include "../core/version.h"
#include "../core/ota.h"
#include <WiFi.h>
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
    {"Spectrum", "100614"},   // Isar Aerospace's rocket - see epaper_render's easter egg
};

static const char PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Satellite Tracker Buddy</title>
<style>body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em}
input,select{width:100%;box-sizing:border-box;padding:.5em;margin:.3em 0 1em}
label{font-size:.85em;color:#555}
button{padding:.6em 1em;border:0;border-radius:4px;background:#222;color:#fff;margin:.2em}
.fav{background:#567}
.save{width:100%;padding:.8em;background:#284;font-size:1.05em;margin-top:1em}
hr{border:0;border-top:1px solid #ddd;margin:1.5em 0}
.status{background:#f4f4f4;border-radius:6px;padding:.7em 1em;margin-bottom:1.2em;font-size:.85em;line-height:1.6}
.status b{color:#555}
.bad{color:#a22}
.inline{display:inline}
.inline button{padding:.35em .7em;font-size:.9em}
.install{background:#a52}
</style></head><body>
<h2>Satellite Tracker Buddy</h2>
<div class="status">
  <b>Firmware version:</b> %FWVERSION%<br>
  <b>Firmware update:</b> %OTASTATUS%
  <form class="inline" method="POST" action="/ota/check"><button class="fav">Check for updates</button></form><br>
  %OTAINSTALL%<br>
  <b>WiFi:</b> %WIFISTATUS%<br>
  <b>Elements fetch:</b> %ELEMENTSSTATUS%<br>
  <b>Launch date fetch:</b> %LAUNCHSTATUS%<br>
  <b>n2yo name lookup:</b> %N2YOSTATUS%<br>
  <b>CelesTrak requests (last 2h):</b> %REQCOUNT% / 50 (their firewall
  threshold - see the README's "Rate limits" section; this device's fetch
  interval floor already keeps normal use well under it)
</div>
<form method="POST" action="/config">
  <label>NORAD catalog id</label>
  <input name="norad" id="norad" value="%NORAD%" required>
  <div>%FAVBUTTONS%</div>

  <label>Site latitude (optional - enables the "Next pass" prediction on
  the display: when the satellite will next be at least 10&deg; above your
  horizon. Leave both at 0 to disable it - not every orbit passes over
  every location, so it may also show "None")</label>
  <input id="lat" name="lat" value="%LAT%" type="number" step="any">
  <label>Site longitude</label>
  <input id="lon" name="lon" value="%LON%" type="number" step="any">
  <button type="button" class="fav" onclick="useMyLocation()">Use my location</button>
  <div id="geoStatus" style="font-size:.8em;color:#555;margin:.3em 0"></div>

  <label>n2yo API key (optional - used to resolve the real name sooner for
  freshly-launched objects CelesTrak still shows generically). Leaving this
  blank on save keeps the current key - check the box below to actually
  remove it. Masked here the same way the WiFi password is, since anyone
  looking at (or screenshotting) this page would otherwise see it in
  plain text.</label>
  <input name="n2yo" value="%N2YO%" type="password">
  <label style="display:block;margin:.3em 0 1em">
    <input type="checkbox" name="n2yo_clear" value="1" style="width:auto;margin:0 .4em 0 0;vertical-align:middle">
    Remove n2yo key
  </label>

  <label>Display full-refresh interval (minutes)</label>
  <input name="refresh" value="%REFRESH%" type="number" min="1" max="60" step="1">

  <label>Elements/launch-date fetch interval (minutes) - how often CelesTrak
  is polled for new orbital data. Kept at 10 minutes minimum: CelesTrak
  firewalls an IP after 50 HTTP error responses in a 2-hour window, and two
  requests happen per fetch, so even at the minimum this stays well under
  that (see the README's "Rate limits" section).</label>
  <input name="fetch" value="%FETCH%" type="number" min="10" max="1440" step="1">

  <label>Device hostname (shown to your router/DHCP; requires a reconnect
  to take effect)</label>
  <input name="hostname" value="%HOSTNAME%">

  <label>Update manifest URL (advanced - only change this for testing
  against a local server, or to point at a fork's own releases)</label>
  <input name="otaurl" value="%OTAURL%">

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
function useMyLocation(){
  var status = document.getElementById('geoStatus');
  if (!navigator.geolocation) {
    geoIpFallback('Geolocation not supported by this browser');
    return;
  }
  status.textContent = 'Locating...';
  navigator.geolocation.getCurrentPosition(function(pos){
    document.getElementById('lat').value = pos.coords.latitude.toFixed(4);
    document.getElementById('lon').value = pos.coords.longitude.toFixed(4);
    status.textContent = 'Filled in from your browser (precise) - remember to Save below.';
  }, function(err){
    // Most browsers only allow this API over HTTPS (or localhost) - this
    // device only serves plain HTTP, so it's often blocked outright rather
    // than prompting. Fall back to a server-side, IP-based approximation
    // instead of just giving up - see core/geoip.h for why that sidesteps
    // the restriction entirely (it's a browser JS API rule, not a
    // networking one, so a plain device-to-service HTTP call is unaffected).
    geoIpFallback('Could not get your precise location (' + err.message + ')');
  });
}
function geoIpFallback(reason){
  var status = document.getElementById('geoStatus');
  status.textContent = reason + ' - trying an approximate location from your network instead...';
  fetch('/geoip').then(function(r){ return r.json(); }).then(function(data){
    if (data.ok) {
      document.getElementById('lat').value = data.lat.toFixed(4);
      document.getElementById('lon').value = data.lon.toFixed(4);
      status.textContent = 'Approximate location filled in (city-level accuracy, not exact) - remember to Save below.';
    } else {
      status.textContent = reason + ' - and the approximate fallback failed too (' + data.error + '). Type the coordinates in manually.';
    }
  }).catch(function(){
    status.textContent = reason + ' - and the approximate fallback failed too. Type the coordinates in manually.';
  });
}
</script>
<p style="text-align:center;font-size:.8em;color:#888;margin-top:2em">
  <a href="https://github.com/lueningphilipp/Satellite-Tracker-Buddy" style="color:#888">Satellite Tracker Buddy on GitHub</a>
</p>
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

// Wraps anything that isn't "OK" or a normal/neutral state (no key
// configured yet, not fetched yet) in a highlight span, so actual
// connection problems (e.g. CelesTrak's known gp.php 403s, see CLAUDE.md's
// TODO) stand out instead of blending into normal text - without also
// flagging "you haven't set an optional API key" as if it were an error.
static String statusSpan(const String& status) {
    if (status == "OK" || status == "no key configured" || status == "not fetched yet")
        return status;
    return "<span class=\"bad\">" + status + "</span>";
}

static String renderPage(const DeviceConfig& cfg) {
    String page = PAGE_TEMPLATE;
    // FW_VERSION comes from the git tag at build time, not a stored/config
    // value - see core/version.h and CLAUDE.md's "OTA + versioning plan".
    page.replace("%FWVERSION%", FW_VERSION);
    page.replace("%NORAD%", cfg.noradId);
    page.replace("%FAVBUTTONS%", buildFavButtons());
    page.replace("%LAT%", String(cfg.siteLat, 4));
    page.replace("%LON%", String(cfg.siteLon, 4));
    page.replace("%N2YO%", cfg.n2yoApiKey);
    page.replace("%REFRESH%", String(cfg.displayRefreshMinutes));
    page.replace("%FETCH%", String(cfg.elementsFetchMinutes));
    page.replace("%HOSTNAME%", cfg.hostname);
    page.replace("%OTAURL%", cfg.otaManifestUrl);

    // Firmware update status/button - see core/ota.h. The install button
    // only appears once a check has actually found a newer release, so a
    // fresh page load (before any check) or an up-to-date device shows just
    // the status line and the "Check for updates" button above it.
    String otaStatus = ota.status();
    page.replace("%OTASTATUS%", ota.statusIsError()
                     ? ("<span class=\"bad\">" + otaStatus + "</span>")
                     : otaStatus);
    page.replace("%OTAINSTALL%", ota.updateAvailable()
        ? ("<form class=\"inline\" method=\"POST\" action=\"/ota/install\">"
           "<button class=\"install\">Install " + ota.availableVersion() + "</button></form>")
        : "");

    // Current SSID only - never the password, so it can't leak into a page
    // source view.
    page.replace("%CURSSID%", cfg.wifiSsid.length() ? cfg.wifiSsid : "(not set)");
    // Cached, not a live scan - see wifi_setup.h's scanNetworksHtml() for
    // why a live scan can't safely happen inside this request handler.
    page.replace("%WIFIOPTIONS%", wifiSetup.cachedOptionsHtml());

    // Live WiFi.status() (cheap, non-blocking) alongside the cached fetch
    // statuses from the last refetchAndInit() - together these are meant to
    // answer "why isn't this showing what I expect" without the serial
    // monitor.
    String wifiStatus = WiFi.status() == WL_CONNECTED
        ? ("Connected to " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)")
        : "<span class=\"bad\">Not connected</span>";
    page.replace("%WIFISTATUS%", wifiStatus);
    page.replace("%ELEMENTSSTATUS%", statusSpan(connStatus.elementsStatus));
    page.replace("%LAUNCHSTATUS%", statusSpan(connStatus.launchDateStatus));
    page.replace("%N2YOSTATUS%", statusSpan(connStatus.n2yoStatus));

    // Highlighted only once it's getting close to CelesTrak's actual
    // threshold (50) - not on every nonzero count, which would make a
    // perfectly normal handful of requests look like a problem.
    int reqCount = celestrakRequests.countInLast2h();
    String reqCountStr = String(reqCount);
    page.replace("%REQCOUNT%", reqCount >= 40 ? ("<span class=\"bad\">" + reqCountStr + "</span>") : reqCountStr);
    return page;
}

void ConfigWebServer::begin(ConfigStore& store, std::function<void()> onConfigSaved) {
    server.on("/", HTTP_GET, [&store](AsyncWebServerRequest* req) {
        req->send(200, "text/html", renderPage(store.current));
    });

    // Server-side fallback for the "Use my location" button - see
    // core/geoip.h for why (many browsers block the Geolocation JS API on
    // this device's plain-HTTP origin). A blocking HTTPClient GET, same as
    // the elements/launch-date/n2yo fetches already called safely from
    // request handlers elsewhere on this page - a plain HTTP GET isn't the
    // class of call (WiFi.scanNetworks(), a tight CPU loop) that's actually
    // unsafe there, see CLAUDE.md's TODO.
    server.on("/geoip", HTTP_GET, [](AsyncWebServerRequest* req) {
        float lat, lon;
        String err;
        if (fetchGeoIpLocation(lat, lon, &err)) {
            String json = "{\"ok\":true,\"lat\":" + String(lat, 4) + ",\"lon\":" + String(lon, 4) + "}";
            req->send(200, "application/json", json);
        } else {
            String json = "{\"ok\":false,\"error\":\"" + err + "\"}";
            req->send(200, "application/json", json);
        }
    });

    // REST GET, per CLAUDE.md's "REST: GET/POST /config" - handy for the
    // eventual button-cycle-favourites firmware to read state without
    // scraping HTML.
    server.on("/config", HTTP_GET, [&store](AsyncWebServerRequest* req) {
        String json = "{";
        json += "\"fwVersion\":\"" + String(FW_VERSION) + "\",";
        json += "\"norad\":\"" + store.current.noradId + "\",";
        json += "\"lat\":" + String(store.current.siteLat, 4) + ",";
        json += "\"lon\":" + String(store.current.siteLon, 4) + ",";
        json += "\"hasN2yoKey\":" + String(store.current.n2yoApiKey.length() > 0 ? "true" : "false") + ",";
        json += "\"hostname\":\"" + store.current.hostname + "\",";
        json += "\"refreshMinutes\":" + String(store.current.displayRefreshMinutes) + ",";
        json += "\"fetchMinutes\":" + String(store.current.elementsFetchMinutes) + ",";
        json += "\"wifiSsid\":\"" + store.current.wifiSsid + "\",";
        json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        json += "\"elementsStatus\":\"" + connStatus.elementsStatus + "\",";
        json += "\"launchDateStatus\":\"" + connStatus.launchDateStatus + "\",";
        json += "\"n2yoStatus\":\"" + connStatus.n2yoStatus + "\",";
        json += "\"celestrakRequests2h\":" + String(celestrakRequests.countInLast2h()) + ",";
        json += "\"otaEnabled\":" + String(ota.enabled() ? "true" : "false") + ",";
        json += "\"otaStatus\":\"" + ota.status() + "\",";
        json += "\"otaUpdateAvailable\":" + String(ota.updateAvailable() ? "true" : "false");
        if (ota.updateAvailable())
            json += ",\"otaAvailableVersion\":\"" + ota.availableVersion() + "\"";
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
        // Blank alone does NOT clear the key - it means "unchanged". The
        // page always pre-fills this field with the current key, so a
        // normal browser save carries it forward automatically; blank only
        // shows up from a raw/scripted POST that didn't set it (hit this
        // project during development - a batch of curl test calls wiped a
        // real n2yo key this way) or a user who genuinely cleared the
        // field, which the checkbox now disambiguates explicitly.
        if (req->hasParam("n2yo_clear", true)) {
            store.current.n2yoApiKey = "";
        } else if (req->hasParam("n2yo", true) && req->getParam("n2yo", true)->value().length() > 0) {
            store.current.n2yoApiKey = req->getParam("n2yo", true)->value();
        }
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
        if (req->hasParam("fetch", true)) {
            int mins = req->getParam("fetch", true)->value().toInt();
            // Hard floor of 10 minutes regardless of what's POSTed - see the
            // field's label above for the "50 errors/2h" reasoning. This is
            // the actual enforcement point; the form's min= is just a UI hint
            // a manual POST could bypass.
            if (mins < 10) mins = 10;
            if (mins > 1440) mins = 1440;
            store.current.elementsFetchMinutes = mins;
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
        // Blank means "keep the default/current URL" - a manual POST
        // clearing this field shouldn't leave the device unable to check
        // for updates at all.
        if (req->hasParam("otaurl", true) && req->getParam("otaurl", true)->value().length() > 0) {
            store.current.otaManifestUrl = req->getParam("otaurl", true)->value();
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

    // Manual firmware-update flow (see core/ota.h) - both handlers only set
    // a flag for loop() to act on. Neither the manifest fetch nor the
    // multi-second flash write may run on this async_tcp task (same rule as
    // everything else in this file - see CLAUDE.md's "Known gotchas").
    // Was a static "Checking for updates..." page with no way to tell when
    // the check (which runs from loop(), not this handler - see above)
    // actually finished, short of the user manually clicking Back and
    // reloading "/" themselves, possibly more than once if it wasn't done
    // yet - reported as "it goes to this page and never returns" on real
    // hardware. Now polls /config's otaStatus (already exposed there) every
    // second and redirects to "/" itself once it stops starting with
    // "checking" - requestCheck() writes that prefix synchronously so the
    // very first poll already sees it, not a stale result from before this
    // click. Capped at 30 tries so a truly stuck check (e.g. a hung TLS
    // handshake short of its own internal timeout) still leaves a way back
    // instead of polling forever.
    server.on("/ota/check", HTTP_POST, [](AsyncWebServerRequest* req) {
        ota.requestCheck();
        req->send(200, "text/html", R"HTML(
<html><body style="font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em">
<p id="msg">Checking for updates...</p>
<a href="/">Back</a>
<script>
var tries = 0;
function poll() {
  tries++;
  fetch('/config').then(function(r){ return r.json(); }).then(function(data){
    if (data.otaStatus && data.otaStatus.indexOf('checking') !== 0) {
      window.location.href = '/';
    } else if (tries < 30) {
      setTimeout(poll, 1000);
    } else {
      document.getElementById('msg').textContent =
        'Still checking - this is taking longer than usual. Click Back and reload "/" in a bit.';
    }
  }).catch(function(){
    if (tries < 30) setTimeout(poll, 1000);
  });
}
setTimeout(poll, 1000);
</script>
</body></html>
)HTML");
    });
    // No confirmation step by design: this button only ever renders after a
    // check has found a genuinely newer release, so a click here is already
    // a deliberate, on-page decision - see CLAUDE.md's OTA plan.
    server.on("/ota/install", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool started = ota.requestInstall();
        req->send(200, "text/html",
                   started
                       ? "<html><body><p>Installing update - the device will restart on its own "
                         "in a minute or two. Reload this page after a short wait to see the new "
                         "version.</p><a href=\"/\">Back</a></body></html>"
                       : "<html><body><p>No update is currently queued (run a check first).</p>"
                         "<a href=\"/\">Back</a></body></html>");
    });

    server.begin();
}
