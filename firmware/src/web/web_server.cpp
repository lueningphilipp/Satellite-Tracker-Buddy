#include "web_server.h"
#include "../core/wifi_setup.h"
#include "../core/status.h"
#include "../core/geoip.h"
#include "../core/request_tracker.h"
#include "../core/version.h"
#include "../core/ota.h"
#include "../core/slide_store.h"
#include <WiFi.h>
#include <LittleFS.h>
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
input[type=radio],input[type=checkbox]{width:auto;margin:0 .4em 0 0;vertical-align:middle}
.modes label{display:block;font-size:1em;color:#222;margin:.4em 0}
fieldset{border:1px solid #ddd;border-radius:6px;margin:1em 0;padding:.5em 1em}
legend{font-weight:bold;padding:0 .3em}
.slide{display:flex;align-items:center;gap:.6em;margin:.4em 0;font-size:.85em}
.slide canvas{width:120px;height:72px;border:1px solid #ccc;flex:none}
.slide span{flex:1;word-break:break-all}
.del{background:#a22}
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
  <label>Display mode</label>
  <div class="modes">
    <label><input type="radio" name="mode" value="0" %MODESAT% onchange="showMode()">Satellite tracker</label>
    <label><input type="radio" name="mode" value="1" %MODESLIDE% onchange="showMode()">Slideshow</label>
  </div>

  <fieldset id="satSettings"><legend>Satellite tracker</legend>
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
  </fieldset>

  <fieldset id="slideSettings"><legend>Slideshow</legend>
  <label>Minutes each picture stays on screen (with a single picture it
  just stays up)</label>
  <input name="slidemin" value="%SLIDEMIN%" type="number" min="1" max="1440" step="1">

  <label id="slideCount">Pictures on the device</label>
  <div id="slideList"></div>

  <label>Add pictures - converted to black and white 800x480 in this browser,
  then uploaded right away (no need to Save)</label>
  <input type="file" id="pics" accept="image/*" multiple onchange="previewPic()">
  <label>Framing</label>
  <select id="fit" onchange="previewPic()">
    <option value="cover">Fill the screen (crops the edges)</option>
    <option value="contain">Show the whole picture (white borders)</option>
  </select>
  <label style="display:block;margin:.3em 0 1em">
    <input type="checkbox" id="dither" checked onchange="previewPic()">
    Dither (best for photos - untick for logos, text and line art)
  </label>
  <canvas id="preview" width="800" height="480" style="width:100%;border:1px solid #ccc"></canvas>
  <button type="button" onclick="uploadPics()">Upload</button>
  <div id="upStatus" style="font-size:.85em;color:#555;margin:.3em 0"></div>
  </fieldset>

  <fieldset><legend>Device</legend>
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
  </fieldset>

  <button class="save" type="submit">Save</button>
</form>
<script>
function pick(id){document.getElementById('norad').value=id;}

// --- Slideshow ---------------------------------------------------------
// Pictures are converted here, not on the device: scaled/cropped to the
// panel's 800x480, turned to grey, then thresholded or Floyd-Steinberg
// dithered to 1 bit, and packed 8 pixels/byte, MSB first, 1 = black - the
// exact bytes the firmware draws (see core/slide_store.h).
var W = 800, H = 480, listLoaded = false, listGen = 0;
function $(id){ return document.getElementById(id); }
function showMode(){
  var slide = document.querySelector('input[name=mode]:checked').value == '1';
  $('satSettings').style.display = slide ? 'none' : '';
  $('slideSettings').style.display = slide ? '' : 'none';
  if (slide && !listLoaded) { listLoaded = true; loadList(); }
}
function loadImg(file){
  return new Promise(function(ok, bad){
    var img = new Image();
    img.onload = function(){ ok(img); };
    img.onerror = bad;
    img.src = URL.createObjectURL(file);
  });
}
function convert(img){
  var c = document.createElement('canvas'); c.width = W; c.height = H;
  var x = c.getContext('2d');
  x.fillStyle = '#fff'; x.fillRect(0, 0, W, H);
  x.imageSmoothingQuality = 'high';
  var cover = $('fit').value == 'cover';
  var s = cover ? Math.max(W / img.width, H / img.height) : Math.min(W / img.width, H / img.height);
  var w = img.width * s, h = img.height * s;
  x.drawImage(img, (W - w) / 2, (H - h) / 2, w, h);
  var p = x.getImageData(0, 0, W, H).data, g = new Float32Array(W * H);
  for (var i = 0; i < W * H; i++) g[i] = 0.299 * p[4*i] + 0.587 * p[4*i+1] + 0.114 * p[4*i+2];
  var dith = $('dither').checked, bits = new Uint8Array(W * H / 8);
  for (var y = 0; y < H; y++) for (var px = 0; px < W; px++) {
    var k = y * W + px, old = g[k], black = old < 128, err = old - (black ? 0 : 255);
    if (black) bits[k >> 3] |= 0x80 >> (px & 7);
    if (!dith) continue;
    if (px + 1 < W) g[k + 1] += err * 7 / 16;
    if (y + 1 < H) {
      if (px > 0) g[k + W - 1] += err * 3 / 16;
      g[k + W] += err * 5 / 16;
      if (px + 1 < W) g[k + W + 1] += err / 16;
    }
  }
  return bits;
}
function drawBits(canvas, bits){
  var c = document.createElement('canvas'); c.width = W; c.height = H;
  var x = c.getContext('2d'), d = x.createImageData(W, H);
  for (var i = 0; i < W * H; i++) {
    var v = (bits[i >> 3] & (0x80 >> (i & 7))) ? 0 : 255;
    d.data[4*i] = d.data[4*i+1] = d.data[4*i+2] = v; d.data[4*i+3] = 255;
  }
  x.putImageData(d, 0, 0);
  canvas.getContext('2d').drawImage(c, 0, 0, canvas.width, canvas.height);
}
function previewPic(){
  var f = $('pics').files;
  if (!f.length) return;
  loadImg(f[0]).then(function(img){ drawBits($('preview'), convert(img)); })
    .catch(function(){ $('upStatus').textContent = 'Could not read ' + f[0].name + ' as an image.'; });
}
async function uploadPics(){
  var files = $('pics').files, st = $('upStatus');
  if (!files.length) { st.textContent = 'Choose one or more pictures first.'; return; }
  for (var n = 0; n < files.length; n++) {
    var f = files[n];
    st.textContent = 'Uploading ' + (n + 1) + ' of ' + files.length + ' (' + f.name + ')...';
    var bits;
    try { bits = convert(await loadImg(f)); }
    catch (e) { st.textContent = 'Could not read ' + f.name + ' as an image.'; loadList(); return; }
    drawBits($('preview'), bits);
    try {
      var r = await fetch('/slides/upload?name=' + encodeURIComponent(f.name),
                          {method: 'POST', headers: {'Content-Type': 'application/octet-stream'}, body: bits});
      if (!r.ok) { st.textContent = f.name + ': ' + await r.text(); loadList(); return; }
    } catch (e) { st.textContent = f.name + ': upload failed (device unreachable?)'; loadList(); return; }
  }
  st.textContent = 'Uploaded ' + files.length + ' picture(s) - the newest one is on the display now.';
  $('pics').value = '';
  loadList();
}
async function delPic(id){
  if (!confirm('Delete this picture from the device?')) return;
  await fetch('/slides/delete?id=' + id, {method: 'POST'});
  loadList();
}
async function loadList(){
  var gen = ++listGen, el = $('slideList');
  var d;
  try { d = await (await fetch('/slides/list')).json(); }
  catch (e) { el.textContent = 'Could not load the picture list.'; return; }
  if (gen != listGen) return;
  $('slideCount').textContent = 'Pictures on the device: ' + d.slides.length + ' / ' + d.max;
  el.innerHTML = '';
  var thumbs = [];
  d.slides.forEach(function(s){
    var row = document.createElement('div'); row.className = 'slide';
    var cv = document.createElement('canvas'); cv.width = 160; cv.height = 96;
    var name = document.createElement('span'); name.textContent = s.name || ('Picture ' + s.id);
    var del = document.createElement('button'); del.type = 'button'; del.className = 'del';
    del.textContent = 'Delete'; del.onclick = function(){ delPic(s.id); };
    row.appendChild(cv); row.appendChild(name); row.appendChild(del); el.appendChild(row);
    thumbs.push([s.id, cv]);
  });
  // One at a time - 48KB each, no need to make the device serve them all at once.
  for (var t = 0; t < thumbs.length; t++) {
    if (gen != listGen) return;
    try {
      var buf = await (await fetch('/slides/raw?id=' + thumbs[t][0])).arrayBuffer();
      if (buf.byteLength == W * H / 8) drawBits(thumbs[t][1], new Uint8Array(buf));
    } catch (e) {}
  }
}
showMode();
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
    bool slideshow = cfg.displayMode == DisplayMode::Slideshow;
    page.replace("%MODESAT%", slideshow ? "" : "checked");
    page.replace("%MODESLIDE%", slideshow ? "checked" : "");
    page.replace("%SLIDEMIN%", String(cfg.slideMinutes));

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

void ConfigWebServer::begin(ConfigStore& store, std::function<void()> onConfigSaved,
                            std::function<void(long)> onSlidesChanged) {
    server.on("/", HTTP_GET, [&store](AsyncWebServerRequest* req) {
        req->send(200, "text/html", renderPage(store.current));
    });

    // Server-side fallback for the "Use my location" button - see
    // core/geoip.h for why (many browsers block the Geolocation JS API on
    // this device's plain-HTTP origin). A single blocking HTTPClient GET
    // (bounded by HTTPClient's own timeout) - unlike WiFi.scanNetworks() or a
    // tight CPU loop, that hasn't been a problem on this task. The elements/
    // launch-date/n2yo fetches, which are several such calls back to back
    // plus retries, do NOT run from handlers - see main.cpp's onConfigSaved().
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
        json += "\"mode\":\"" + String(store.current.displayMode == DisplayMode::Slideshow ? "slideshow" : "satellite") + "\",";
        json += "\"slideMinutes\":" + String(store.current.slideMinutes) + ",";
        json += "\"slideCount\":" + String(slides.count()) + ",";
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
        if (req->hasParam("mode", true)) {
            store.current.displayMode = req->getParam("mode", true)->value() == "1"
                                            ? DisplayMode::Slideshow : DisplayMode::Satellite;
        }
        if (req->hasParam("slidemin", true)) {
            int mins = req->getParam("slidemin", true)->value().toInt();
            // Same reasoning as the refresh clamp above: 0 would flash
            // through the pictures as fast as the panel can refresh.
            if (mins < 1) mins = 1;
            if (mins > 1440) mins = 1440;
            store.current.slideMinutes = mins;
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
                   store.current.displayMode == DisplayMode::Slideshow
                       ? String("<html><body><p>Saved. Slideshow mode.</p><a href=\"/\">Back</a></body></html>")
                       : "<html><body><p>Saved. Refetching elements for "
                         + store.current.noradId + "...</p><a href=\"/\">Back</a></body></html>");
        if (onConfigSaved) onConfigSaved();
    });

    // --- Slideshow pictures (see core/slide_store.h) ---------------------
    // The page converts each picture to the panel's raw 1bpp format and
    // POSTs exactly SLIDE_BYTES as application/octet-stream, which
    // ESPAsyncWebServer hands to the body callback chunk by chunk (~1.4KB
    // each). Each chunk is one small LittleFS write - milliseconds, not the
    // multi-second blocking the async_tcp gotcha is about - so writing here
    // is fine; the e-paper redraw itself is left to loop() via
    // onSlidesChanged. Size and the SLIDE_MAX limit are checked up front
    // from Content-Length, before anything touches flash.
    // Not plain "/slides": ESPAsyncWebServer treats a path as a prefix
    // match, so that would also swallow GET /slides/raw.
    server.on("/slides/list", HTTP_GET, [](AsyncWebServerRequest* req) {
        String json = "{\"max\":" + String(SLIDE_MAX) + ",\"slides\":[";
        bool first = true;
        for (const SlideInfo& s : slides.list()) {
            if (!first) json += ",";
            first = false;
            json += "{\"id\":" + String(s.id) + ",\"name\":\"" + s.name + "\"}";
        }
        json += "]}";
        req->send(200, "application/json", json);
    });

    server.on("/slides/raw", HTTP_GET, [](AsyncWebServerRequest* req) {
        long id = req->hasParam("id") ? req->getParam("id")->value().toInt() : 0;
        String p = slides.path(id);
        if (id <= 0 || !LittleFS.exists(p)) { req->send(404, "text/plain", "no such picture"); return; }
        req->send(LittleFS, p, "application/octet-stream");
    });

    server.on("/slides/upload", HTTP_POST,
        [onSlidesChanged](AsyncWebServerRequest* req) {
            if (req->contentLength() != SLIDE_BYTES) {
                req->send(400, "text/plain", "wrong size - expected an 800x480 1-bit picture");
                return;
            }
            if (!req->_tempFile) {
                req->send(507, "text/plain",
                          slides.hasRoom() ? String("could not write to flash")
                                           : "storage full (" + String(SLIDE_MAX) + " pictures max) - delete one first");
                return;
            }
            req->_tempFile.close();
            // Kept JSON- and HTML-safe at the source, so the list endpoint
            // above can embed it without escaping.
            String name = req->hasParam("name") ? req->getParam("name")->value() : "";
            String clean;
            for (size_t i = 0; i < name.length() && clean.length() < 60; i++) {
                char c = name[i];
                clean += ((uint8_t)c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>') ? '_' : c;
            }
            long id = slides.commitUpload(clean);
            if (id < 0) { req->send(500, "text/plain", "could not save the picture"); return; }
            Serial.printf("Slide %ld uploaded (%s)\n", id, clean.c_str());
            req->send(200, "text/plain", String(id));
            if (onSlidesChanged) onSlidesChanged(id);
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0 && total == SLIDE_BYTES && slides.hasRoom())
                req->_tempFile = slides.openUpload();
            if (req->_tempFile && req->_tempFile.write(data, len) != len) {
                req->_tempFile.close();
                slides.discardUpload();
            }
        });

    server.on("/slides/delete", HTTP_POST, [onSlidesChanged](AsyncWebServerRequest* req) {
        long id = req->hasParam("id") ? req->getParam("id")->value().toInt() : 0;
        if (id <= 0 || !slides.remove(id)) { req->send(404, "text/plain", "no such picture"); return; }
        Serial.printf("Slide %ld deleted\n", id);
        req->send(200, "text/plain", "deleted");
        if (onSlidesChanged) onSlidesChanged(0);
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
