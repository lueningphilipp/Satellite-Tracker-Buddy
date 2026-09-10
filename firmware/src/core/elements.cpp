#include "elements.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>   // sscanf

// CelesTrak's CSV column order (verified against a live response - see the
// curl output in this project's chat history / CLAUDE.md's Architecture
// section):
//   OBJECT_NAME,OBJECT_ID,EPOCH,MEAN_MOTION,ECCENTRICITY,INCLINATION,
//   RA_OF_ASC_NODE,ARG_OF_PERICENTER,MEAN_ANOMALY,EPHEMERIS_TYPE,
//   CLASSIFICATION_TYPE,NORAD_CAT_ID,ELEMENT_SET_NO,REV_AT_EPOCH,BSTAR,
//   MEAN_MOTION_DOT,MEAN_MOTION_DDOT
// We only need a subset - MEAN_MOTION_DOT/DDOT are unused because
// Hopperpop/Sgp4-Library's sgp4init() doesn't take them (SGP4 proper ignores
// mean-motion derivatives; they're a leftover TLE-format field).
enum CsvCol {
    COL_NAME = 0, COL_OBJECT_ID, COL_EPOCH, COL_MEAN_MOTION, COL_ECC,
    COL_INCL, COL_RAAN, COL_ARGP, COL_MEAN_ANOM, COL_EPHEM_TYPE,
    COL_CLASS, COL_NORAD_ID, COL_ELEMENT_SET, COL_REV, COL_BSTAR,
};

static String csvField(const String& line, int index) {
    int start = 0, col = 0;
    while (col < index) {
        int comma = line.indexOf(',', start);
        if (comma < 0) return "";
        start = comma + 1;
        col++;
    }
    int end = line.indexOf(',', start);
    return end < 0 ? line.substring(start) : line.substring(start, end);
}

// Parses "YYYY-MM-DDTHH:MM:SS.ffffff" (CelesTrak's fixed EPOCH format).
static bool parseEpoch(const String& s, OrbitalElements& out) {
    int y, mo, d, h, mi;
    double sec;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6)
        return false;
    out.epochYear = y; out.epochMonth = mo; out.epochDay = d;
    out.epochHour = h; out.epochMin = mi; out.epochSec = sec;
    return true;
}

bool fetchElements(const String& noradId, OrbitalElements& out) {
    WiFiClientSecure client;
    client.setInsecure();   // no CA bundle on-device; acceptable risk for a
                             // read-only hobby fetch, see comment in main.cpp

    HTTPClient http;
    String url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=" + noradId + "&FORMAT=csv";
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("elements fetch: HTTP %d for CATNR=%s\n", code, noradId.c_str());
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    // First line is the CSV header; second is our one data row. A "No GP
    // data found" 404 body would already have been caught by the code check
    // above, so an empty/short body here means something else went wrong.
    int nl = body.indexOf('\n');
    if (nl < 0) return false;
    String row = body.substring(nl + 1);
    row.trim();
    if (row.length() == 0) return false;

    OrbitalElements parsed;
    parsed.name = csvField(row, COL_NAME);
    parsed.noradCatId = csvField(row, COL_NORAD_ID).toInt();
    if (!parseEpoch(csvField(row, COL_EPOCH), parsed)) return false;
    parsed.meanMotionRevPerDay = csvField(row, COL_MEAN_MOTION).toDouble();
    parsed.eccentricity = csvField(row, COL_ECC).toDouble();
    parsed.inclinationDeg = csvField(row, COL_INCL).toDouble();
    parsed.raanDeg = csvField(row, COL_RAAN).toDouble();
    parsed.argPerigeeDeg = csvField(row, COL_ARGP).toDouble();
    parsed.meanAnomalyDeg = csvField(row, COL_MEAN_ANOM).toDouble();
    parsed.bstar = csvField(row, COL_BSTAR).toDouble();

    if (parsed.name.length() == 0 || parsed.meanMotionRevPerDay <= 0) return false;

    out = parsed;
    return true;
}

OrbitalElements fallbackElements() {
    // Same snapshot as the demo's FALLBACK_OMM (sat_display_demo.py) - kept
    // in sync manually since it's a frozen historical value, not something
    // either side re-derives.
    OrbitalElements el;
    el.name = "ISS (fallback)";
    el.noradCatId = 25544;
    el.epochYear = 2026; el.epochMonth = 9; el.epochDay = 10;
    el.epochHour = 11; el.epochMin = 11; el.epochSec = 7.892448;
    el.meanMotionRevPerDay = 15.49068969;
    el.eccentricity = 0.00049899;
    el.inclinationDeg = 51.6301;
    el.raanDeg = 238.0248;
    el.argPerigeeDeg = 125.6916;
    el.meanAnomalyDeg = 234.4537;
    el.bstar = 0.98181333e-4;
    return el;
}
