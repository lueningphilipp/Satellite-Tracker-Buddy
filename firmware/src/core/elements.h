#pragma once
#include <Arduino.h>

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
bool fetchElements(const String& noradId, OrbitalElements& out);

// Stale hardcoded ISS elements, matching the demo's FALLBACK_OMM exactly -
// used when fetchElements() fails, so the device always has *something* to
// propagate rather than getting stuck with no satrec at all.
OrbitalElements fallbackElements();
