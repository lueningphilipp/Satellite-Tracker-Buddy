#pragma once
#include <time.h>

// Sub-solar point / day-night terminator - ported as-is from subsolar()/
// is_day() in demo/sat_display_demo.py per CLAUDE.md's Architecture note.
// Returns sub-solar (declination, longitude) in degrees.
void subsolar(time_t t, double& decDeg, double& lonDeg);
bool isDay(double lat, double lon, double sunDecDeg, double sunLonDeg);
