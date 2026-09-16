#include "ota.h"
#include "version.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <stdarg.h>
#include <string.h>
#include <vector>

OtaUpdater ota;

// The Arduino core's initArduino() normally marks a freshly-OTA'd image
// valid *immediately* on its first boot (esp32-hal-misc.c), before a single
// line of our code runs - which would make the bootloader's rollback
// support useless for catching a build that boots but can't do its job.
// The core exposes this weak hook to opt out: returning true means "the app
// will call esp_ota_mark_app_valid_cancel_rollback() itself once it knows
// it's healthy" - see OtaUpdater::handleRollback(). Must be extern "C" -
// the core's weak default is a plain C symbol.
extern "C" bool verifyRollbackLater() { return true; }

// How long a freshly-installed build gets to reach networkHealthy before
// it's declared bad and rolled back. Generous: WiFi connect (15s) + NTP +
// the first elements fetch normally take well under a minute, so 10 min
// only fires for a build whose network stack is actually broken (or a
// genuinely offline network, in which case reverting to the previous
// known-good build is harmless - it'll just offer the update again).
static const unsigned long HEALTH_TIMEOUT_MS = 10UL * 60UL * 1000UL;

// Abort a stalled download if no bytes arrive for this long.
static const unsigned long STREAM_STALL_TIMEOUT_MS = 20UL * 1000UL;

// Separate NVS namespace from the main config (config.cpp's "sattrack") so
// this can be written without going through ConfigStore::save().
static const char* NVS_NS_OTA = "sattrack_ota";

// ---------------------------------------------------------------------------
// Version handling
// ---------------------------------------------------------------------------

// Strict "x.y.z" - three non-negative integers, dots, nothing else. This is
// what a clean tagged build's FW_VERSION looks like (extra_script.py strips
// the "fw-v" prefix). Anything else (a dev build's "-N-gHASH" suffix,
// "-dirty", a bare commit hash, "unknown") fails here on purpose.
static bool parseVersion(const char* s, int out[3]) {
    if (!s || !*s) return false;
    int parts = 0;
    const char* p = s;
    while (parts < 3) {
        if (*p < '0' || *p > '9') return false;
        long v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            if (v > 100000) return false;
            p++;
        }
        out[parts++] = (int)v;
        if (parts < 3) {
            if (*p != '.') return false;
            p++;
        }
    }
    return *p == '\0';
}

static int compareVersion(const int a[3], const int b[3]) {
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Status / breadcrumb helpers
// ---------------------------------------------------------------------------

void OtaUpdater::setStatus(bool isError, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(statusBuf, sizeof(statusBuf), fmt, args);
    va_end(args);
    statusError = isError;
    Serial.printf("OTA: %s\n", statusBuf);
}

// Written just before the restart into a new image; read back on the next
// boot(s) to tell the user what happened - including the case where the
// bootloader rolled back, which is otherwise completely silent (the old
// build just... boots).
void OtaUpdater::writeBreadcrumb(const char* from, const char* to) {
    Preferences prefs;
    prefs.begin(NVS_NS_OTA, /*readOnly=*/false);
    prefs.putString("from", from);
    prefs.putString("to", to);
    prefs.end();
}

void OtaUpdater::clearBreadcrumb() {
    Preferences prefs;
    prefs.begin(NVS_NS_OTA, /*readOnly=*/false);
    prefs.clear();
    prefs.end();
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

void OtaUpdater::begin() {
    int v[3];
    releaseBuild = parseVersion(FW_VERSION, v);

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    pendingVerify = (esp_ota_get_state_partition(running, &state) == ESP_OK
                     && state == ESP_OTA_IMG_PENDING_VERIFY);
    Serial.printf("OTA: running from %s, image state %s\n",
                  running ? running->label : "?",
                  pendingVerify ? "PENDING_VERIFY (first boot after update)" : "valid/undefined");

    Preferences prefs;
    prefs.begin(NVS_NS_OTA, /*readOnly=*/true);
    String from = prefs.getString("from", "");
    String to = prefs.getString("to", "");
    prefs.end();

    if (to.length()) {
        if (to == FW_VERSION) {
            // The update took - this is the new build booting. Breadcrumb
            // stays until handleRollback() marks the image valid, so that
            // if this boot never gets healthy and the bootloader reverts,
            // the previous build can still report what happened.
            setStatus(false, "updated %s -> %s%s", from.c_str(), to.c_str(),
                      pendingVerify ? " (verifying this boot...)" : "");
        } else if (from == FW_VERSION) {
            // We're the *old* build running again with a breadcrumb saying
            // we should have been replaced: the new one was rolled back.
            setStatus(true, "update to %s failed and was rolled back to %s - it never got healthy (WiFi + a CelesTrak response) within 10 min",
                      to.c_str(), from.c_str());
            clearBreadcrumb();
        } else {
            clearBreadcrumb();   // stale - e.g. a USB reflash in between
        }
    } else if (!releaseBuild) {
        setStatus(false, "dev build %s - update check disabled (only tagged x.y.z releases compare versions)", FW_VERSION);
    }
}

// ---------------------------------------------------------------------------
// loop()-side driver
// ---------------------------------------------------------------------------

bool OtaUpdater::requestInstall() {
    if (!haveUpdate) return false;
    installRequested = true;
    return true;
}

void OtaUpdater::loop(const String& manifestUrl, bool wifiConnected, bool networkHealthy) {
    handleRollback(networkHealthy);

    if (checkRequested) {
        checkRequested = false;
        check(manifestUrl, wifiConnected);
    }
    if (installRequested) {
        installRequested = false;
        install(wifiConnected);
    }
}

void OtaUpdater::handleRollback(bool networkHealthy) {
    if (!pendingVerify) return;

    if (networkHealthy) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        pendingVerify = false;
        clearBreadcrumb();
        Serial.printf("OTA: build %s marked valid (%s) - rollback cancelled\n",
                      FW_VERSION, err == ESP_OK ? "ok" : esp_err_to_name(err));
        // Drop the "(verifying...)" suffix without losing the update note.
        char* paren = strstr(statusBuf, " (verifying");
        if (paren) *paren = '\0';
        return;
    }

    if (millis() >= HEALTH_TIMEOUT_MS) {
        Serial.printf("OTA: build %s never became healthy within %lu s - rolling back to the previous image\n",
                      FW_VERSION, HEALTH_TIMEOUT_MS / 1000UL);
        Serial.flush();
        delay(200);
        esp_ota_mark_app_invalid_rollback_and_reboot();   // does not return
    }
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

// Picks a TLS or plain client by URL scheme - https for GitHub, plain http
// so a manifest served from `python -m http.server` on the LAN works for
// testing. setInsecure() for the same reason as every other fetch in this
// firmware (no CA bundle on-device; see elements.cpp). Consequence worth
// being explicit about: the manifest's sha256 protects against a corrupt
// or truncated download, NOT against a hostile network that serves both a
// fake manifest and a fake binary - signing the release would be the fix
// for that, deliberately deferred because the update is manual and
// user-initiated (see CLAUDE.md).
static bool startRequest(HTTPClient& http, WiFiClientSecure& secure, WiFiClient& plain,
                         const String& url, String* err) {
    bool isHttps = url.startsWith("https://");
    if (!isHttps && !url.startsWith("http://")) {
        if (err) *err = "URL must start with http:// or https://";
        return false;
    }
    if (isHttps) secure.setInsecure();
    // GitHub's release download URLs 302 to objects.githubusercontent.com -
    // a different host - so redirects must be followed.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(5);
    http.setTimeout(15000);
    http.setReuse(false);
    if (!http.begin(isHttps ? (WiFiClient&)secure : plain, url)) {
        if (err) *err = "couldn't start request";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Check
// ---------------------------------------------------------------------------

void OtaUpdater::check(const String& manifestUrl, bool wifiConnected) {
    haveUpdate = false;
    if (!releaseBuild) {
        setStatus(false, "dev build %s - update check disabled (only tagged x.y.z releases compare versions)", FW_VERSION);
        return;
    }
    if (!wifiConnected) {
        setStatus(true, "check failed: WiFi not connected");
        return;
    }
    setStatus(false, "checking %s ...", manifestUrl.c_str());

    WiFiClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    String err;
    if (!startRequest(http, secure, plain, manifestUrl, &err)) {
        setStatus(true, "check failed: %s", err.c_str());
        return;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        if (code > 0)
            setStatus(true, "check failed: HTTP %d fetching the manifest%s", code,
                      code == 404 ? " (no release published yet, or the repo isn't public)" : "");
        else
            setStatus(true, "check failed: network error (%s)", HTTPClient::errorToString(code).c_str());
        return;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, body);
    if (jsonErr) {
        setStatus(true, "check failed: manifest isn't valid JSON (%s)", jsonErr.c_str());
        return;
    }
    const char* version = doc["version"] | "";
    const char* url = doc["url"] | "";
    const char* sha = doc["sha256"] | "";
    size_t size = doc["size"] | (size_t)0;

    int theirs[3], ours[3];
    if (!parseVersion(version, theirs)) {
        setStatus(true, "check failed: manifest version \"%s\" isn't x.y.z", version);
        return;
    }
    if (strlen(url) < 8 || strlen(url) >= sizeof(availUrl)
        || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
        setStatus(true, "check failed: manifest has a missing/bad binary URL");
        return;
    }
    if (strlen(sha) != 64) {
        setStatus(true, "check failed: manifest sha256 must be 64 hex chars");
        return;
    }
    for (const char* p = sha; *p; p++) {
        if (!isxdigit((unsigned char)*p)) {
            setStatus(true, "check failed: manifest sha256 isn't hex");
            return;
        }
    }
    if (size == 0) {
        setStatus(true, "check failed: manifest has no size");
        return;
    }

    parseVersion(FW_VERSION, ours);   // already validated by releaseBuild
    int cmp = compareVersion(theirs, ours);
    if (cmp > 0) {
        strlcpy(availVersion, version, sizeof(availVersion));
        strlcpy(availUrl, url, sizeof(availUrl));
        strlcpy(availSha256, sha, sizeof(availSha256));
        availSize = size;
        haveUpdate = true;
        setStatus(false, "update available: %s (running %s, %u KB download)", version, FW_VERSION,
                  (unsigned)(size / 1024));
    } else if (cmp == 0) {
        setStatus(false, "up to date (%s is the latest release)", FW_VERSION);
    } else {
        setStatus(false, "running %s, which is newer than the latest published release %s", FW_VERSION, version);
    }
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

static void toHex(const uint8_t* in, size_t n, char* out) {
    static const char* digits = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 0x0f];
    }
    out[2 * n] = '\0';
}

void OtaUpdater::install(bool wifiConnected) {
    if (!haveUpdate) {
        setStatus(true, "install failed: no update known - run a check first");
        return;
    }
    if (!wifiConnected) {
        setStatus(true, "install failed: WiFi not connected");
        return;
    }
    // Snapshot - the check() results could be overwritten by another check
    // while this runs (they can't, both are loop()-only, but this keeps the
    // rest of this function independent of that).
    char version[sizeof(availVersion)];
    strlcpy(version, availVersion, sizeof(version));
    size_t expectedSize = availSize;

    if (expectedSize > ESP.getFreeSketchSpace()) {
        setStatus(true, "install failed: %u-byte image doesn't fit the %u-byte OTA slot",
                  (unsigned)expectedSize, (unsigned)ESP.getFreeSketchSpace());
        return;
    }

    setStatus(false, "downloading %s: 0%%", version);

    WiFiClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    String err;
    if (!startRequest(http, secure, plain, String(availUrl), &err)) {
        setStatus(true, "install failed: %s", err.c_str());
        return;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        if (code > 0) setStatus(true, "install failed: HTTP %d fetching the binary", code);
        else setStatus(true, "install failed: network error (%s)", HTTPClient::errorToString(code).c_str());
        return;
    }
    int contentLength = http.getSize();
    if (contentLength > 0 && (size_t)contentLength != expectedSize) {
        http.end();
        setStatus(true, "install failed: server says %d bytes, manifest says %u - not installing",
                  contentLength, (unsigned)expectedSize);
        return;
    }

    if (!Update.begin(expectedSize, U_FLASH)) {
        http.end();
        setStatus(true, "install failed: %s", Update.errorString());
        return;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, /*is224=*/0);

    // Heap, not stack: the Arduino loop task's stack is only 8KB and TLS
    // reads already use a fair chunk of it.
    std::vector<uint8_t> buf(4096);
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    int lastPercent = 0;
    unsigned long lastDataMs = millis();
    bool ok = true;

    while (written < expectedSize) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (!http.connected() && stream->available() == 0) {
                setStatus(true, "install failed: connection closed after %u of %u bytes",
                          (unsigned)written, (unsigned)expectedSize);
                ok = false;
                break;
            }
            if (millis() - lastDataMs > STREAM_STALL_TIMEOUT_MS) {
                setStatus(true, "install failed: download stalled at %u of %u bytes",
                          (unsigned)written, (unsigned)expectedSize);
                ok = false;
                break;
            }
            delay(1);
            continue;
        }
        size_t want = min(min(avail, buf.size()), expectedSize - written);
        size_t n = stream->readBytes(buf.data(), want);
        if (n == 0) continue;
        lastDataMs = millis();

        if (Update.write(buf.data(), n) != n) {
            setStatus(true, "install failed: flash write error (%s)", Update.errorString());
            ok = false;
            break;
        }
        mbedtls_sha256_update_ret(&sha, buf.data(), n);
        written += n;

        int percent = (int)((uint64_t)written * 100 / expectedSize);
        if (percent >= lastPercent + 5 || percent == 100) {
            lastPercent = percent;
            // Direct snprintf rather than setStatus() - no need to log 20
            // progress lines to serial.
            snprintf(statusBuf, sizeof(statusBuf), "downloading %s: %d%%", version, percent);
        }
        yield();
    }
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (!ok) {
        Update.abort();
        return;
    }

    char hex[65];
    toHex(digest, sizeof(digest), hex);
    if (strcasecmp(hex, availSha256) != 0) {
        // Nothing has been committed yet - abort() discards the inactive
        // slot's contents without touching the boot pointer.
        Update.abort();
        setStatus(true, "install failed: sha256 mismatch (got %.12s..., manifest %.12s...) - not installed",
                  hex, availSha256);
        return;
    }

    // end() validates the image header/structure and sets the inactive
    // slot as the next boot partition (state NEW -> the bootloader marks
    // it PENDING_VERIFY on the way in; see handleRollback()).
    if (!Update.end(/*evenIfRemaining=*/false)) {
        setStatus(true, "install failed: %s", Update.errorString());
        return;
    }

    writeBreadcrumb(FW_VERSION, version);
    haveUpdate = false;
    setStatus(false, "installed %s (sha256 verified) - restarting into it now", version);
    Serial.flush();
    delay(1000);
    ESP.restart();
}
