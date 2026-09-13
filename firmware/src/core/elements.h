#pragma once
#include <Arduino.h>
#include <time.h>

// Orbital elements as fetched from CelesTrak's OMM/CSV endpoint - the fields
// sgp4_track.cpp needs to call the SGP4 library's low-level init directly,
// mirroring what the demo's sgp4.omm.initialize() does. See CLAUDE.md's
// Architecture section for why this is CSV, not legacy TLE text.
struct OrbitalElements {
    String name;
    long   noradCatId = 0;
    int    epochYear = 0, epochMonth = 0, epochDay = 0, epochHour = 0, epochMin = 0;
    double epochSec = 0;             // includes fractional seconds
    double meanMotionRevPerDay = 0;
    double eccentricity = 0;
    double inclinationDeg = 0;
    double raanDeg = 0;
    double argPerigeeDeg = 0;
    double meanAnomalyDeg = 0;
    double bstar = 0;
};

// Fetches https://celestrak.org/NORAD/elements/gp.php?CATNR=<norad>&FORMAT=csv
// and parses the one data row into `out`. Returns false (leaving `out`
// untouched) on any network/parse failure - caller decides the fallback,
// same "never crash, just say what happened" spirit as the demo.
// `errOut`, if given, is always set to a short human-readable status ("OK"
// on success, else e.g. "HTTP 403") - the config page shows this so
// connection problems (like CelesTrak's known gp.php 403s, see CLAUDE.md's
// TODO) are visible without the serial monitor.
bool fetchElements(const String& noradId, OrbitalElements& out, String* errOut = nullptr);

// Stale hardcoded ISS elements, matching the demo's FALLBACK_OMM exactly -
// used when fetchElements() fails, so the device always has *something* to
// propagate rather than getting stuck with no satrec at all.
OrbitalElements fallbackElements();

// Launch date (for time-in-space) isn't part of the OMM/CSV element set, so
// it's a separate CelesTrak call, same as the demo's fetch_launch_date():
// https://celestrak.org/satcat/records.php?CATNR=<norad>&FORMAT=json,
// field LAUNCH_DATE. Best-effort - returns false (out untouched) on any
// failure, caller just hides the time-in-space field rather than blocking.
//
// IMPORTANT: pass the NORAD id of the elements actually being displayed
// (e.g. `String(el.noradCatId)`), NOT necessarily the one the user
// requested - if fetchElements() above fell back to fallbackElements()
// (the ISS), the object actually shown is the ISS, and looking up some
// other satellite's launch date here would show a mismatched time-in-space
// next to the ISS's orbital numbers (confirmed as a real bug, fixed in the
// demo first - see sat_display_demo.py's main()).
bool fetchLaunchDate(const String& noradId, time_t& out, String* errOut = nullptr);

// Best-effort real-name lookup via n2yo, mirroring the demo's fetch_name():
// CelesTrak's own OBJECT_NAME can lag for days/weeks on freshly-launched,
// multi-payload objects (generic "OBJECT A"/"OBJECT B"/...); n2yo usually
// has the real name sooner. Returns false (outName untouched) if no API key
// is given, the network call fails, or n2yo has no name for this object -
// caller just keeps CelesTrak's name in that case.
//
// Same "use the actually-displayed object's id" rule as fetchLaunchDate()
// above applies here too - callers should skip this entirely (not just
// pass a different id) when showing fallback data, since overwriting the
// clearly-labeled "ISS (fallback)" with a plain "ISS" would quietly lose
// the one signal (besides the ONLINE/OFFLINE dot) that the data is stale.
bool fetchN2yoName(const String& noradId, const String& apiKey, String& outName, String* errOut = nullptr);
