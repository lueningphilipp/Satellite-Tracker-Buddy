#pragma once
#include <time.h>
#include "sgp4_track.h"

// Topocentric elevation angle (deg) of the satellite as seen from an
// observer at obsLat/obsLon (assumed sea level), spherical-Earth
// approximation - ported as-is from elevation_deg() in
// demo/sat_display_demo.py per CLAUDE.md's Architecture note. Consistent
// with Sgp4Track's own spherical lat/lon/alt (same RE, no WGS84
// correction), so the two stay geometrically consistent with each other
// even though a real geodetic observer position would be marginally
// different.
double elevationDeg(double obsLat, double obsLon, double satLat, double satLon, double satAltKm);

enum class PassState { kNow, kFound, kNone };

// Searches forward from `start` for the next time the satellite rises
// above minElevDeg as seen from obsLat/obsLon - ported as-is from
// find_next_pass() in the demo. kNow if already above the threshold at
// `start`; kFound + outPassTime for the next rise (accurate to about one
// search step, no bisection refinement - plenty for a "next pass in Xh Ym"
// display); kNone if no crossing turns up within maxDays, which is the
// "not possible for some orbits" case (a GEO object parked over a
// different longitude, or an inclination that never reaches this latitude
// at all - if a pass is geometrically possible it recurs within a day or
// two for anything except a GEO/near-GEO object, so a multi-day bounded
// search is enough to tell "rare" from "impossible").
//
// Blocking - can take a noticeable fraction of a second to a couple
// seconds depending on stepScale/maxDays (thousands of SGP4 calls). Only
// call this from setup()/loop()/refetchAndInit(), same rule as the other
// occasionally-blocking calls in this codebase (WiFi scan, element
// fetches) - never from inside a live AsyncWebServer request handler.
PassState findNextPass(Sgp4Track& track, double obsLat, double obsLon,
                        time_t start, time_t& outPassTime,
                        double minElevDeg = 10.0, int maxDays = 3, double stepScale = 100.0);
