#pragma once
#include <Arduino.h>
#include "elements.h"

struct SatPosition {
    double lat = 0, lon = 0, altKm = 0;
    bool valid = false;
};

// Wraps Hopperpop/Sgp4-Library (the "SparkFun SGP4 Arduino Library" clone
// CLAUDE.md specs). We bypass the library's own TLE-text init() and call its
// low-level sgp4init() directly with the numeric fields from elements.h -
// mirroring the demo's sgp4.omm.initialize(), and necessary because
// catalog numbers >=100000 have no valid TLE-text form to hand the library's
// normal init() path. Function signature verified against
// github.com/Hopperpop/Sgp4-Library/blob/master/src/sgp4unit.h - not yet
// compiled against real hardware, see platformio.ini's top comment.
class Sgp4Track {
public:
    bool init(const OrbitalElements& el);
    SatPosition positionAt(time_t unixTime);   // needs an NTP-synced clock

    double apogeeKm() const { return apogeeKm_; }
    double perigeeKm() const { return perigeeKm_; }
    double periodMin() const { return periodMin_; }
    const char* orbitClass() const { return orbitClass_; }

private:
    double apogeeKm_ = 0, perigeeKm_ = 0, periodMin_ = 0;
    const char* orbitClass_ = "?";
};

// Rough, human-friendly orbit label - ported as-is from classify_orbit() in
// demo/sat_display_demo.py per CLAUDE.md's Architecture note (a heuristic,
// not authoritative; don't re-derive the thresholds).
const char* classifyOrbit(double apogeeKm, double perigeeKm, double inclDeg, double periodMin);

extern Sgp4Track satTrack;
