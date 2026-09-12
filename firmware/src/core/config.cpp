#include "config.h"
#include <Preferences.h>

// Single NVS namespace for everything. Keys kept short (Preferences caps
// them at 15 chars).
static const char* NVS_NS = "sattrack";

void ConfigStore::begin() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/true);
    current.wifiSsid   = prefs.getString("wifiSsid", "");
    current.wifiPass   = prefs.getString("wifiPass", "");
    current.noradId    = prefs.getString("noradId", "25544");
    current.siteLat    = prefs.getFloat("siteLat", 0.0f);
    current.siteLon    = prefs.getFloat("siteLon", 0.0f);
    current.n2yoApiKey = prefs.getString("n2yoKey", "");
    prefs.end();
}

void ConfigStore::clearWifiCreds() {
    current.wifiSsid = "";
    current.wifiPass = "";
    save();
}

void ConfigStore::save() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putString("wifiSsid", current.wifiSsid);
    prefs.putString("wifiPass", current.wifiPass);
    prefs.putString("noradId", current.noradId);
    prefs.putFloat("siteLat", current.siteLat);
    prefs.putFloat("siteLon", current.siteLon);
    prefs.putString("n2yoKey", current.n2yoApiKey);
    prefs.end();
}
