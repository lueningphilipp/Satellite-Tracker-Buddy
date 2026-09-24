#pragma once
#include <Arduino.h>
#include <time.h>
#include "retry.h"

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

// Elements older than this are no longer trusted once a fetch has failed -
// SGP4 error grows with element age (km/day for LEO), so past a week the
// device drops back to the (also stale, but at least flagged) ISS fallback
// instead of showing ever-less-accurate data unnoticed. Only applied while
// the last fetch failed: a *successful* fetch of old elements (e.g. a dead
// satellite CelesTrak simply hasn't updated) is shown as-is.
static const long ELEMENTS_MAX_AGE_DAYS = 7;

// Epoch of `el` as unix time (UTC).
time_t elementsEpochUnix(const OrbitalElements& el);

// All three fetchers below make exactly ONE attempt and classify the outcome
// (see core/retry.h) - retrying/scheduling is the caller's job, identical for
// every fetcher. `errOut`, if given, is always set to a short human-readable
// status ("OK" on success, else e.g. "HTTP 403") that the config page shows,
// so connection problems are visible without the serial monitor.

// Fetches https://celestrak.org/NORAD/elements/gp.php?CATNR=<norad>&FORMAT=csv
// and parses the one data row into `out` (left untouched unless the result is
// Ok) - caller decides the fallback, same "never crash, just say what
// happened" spirit as the demo.
FetchResult fetchElements(const String& noradId, OrbitalElements& out, String* errOut = nullptr);

// Stale hardcoded ISS elements, matching the demo's FALLBACK_OMM exactly -
// used when fetchElements() fails, so the device always has *something* to
// propagate rather than getting stuck with no satrec at all.
OrbitalElements fallbackElements();

// Launch date (for time-in-space) isn't part of the OMM/CSV element set, so
// it's a separate CelesTrak call, same as the demo's fetch_launch_date():
// https://celestrak.org/satcat/records.php?CATNR=<norad>&FORMAT=json,
// field LAUNCH_DATE. Best-effort - `out` untouched unless Ok; caller just
// hides the time-in-space field rather than blocking.
//
// IMPORTANT: pass the NORAD id of the elements actually being displayed
// (e.g. `String(el.noradCatId)`), NOT necessarily the one the user
// requested - if fetchElements() above fell back to fallbackElements()
// (the ISS), the object actually shown is the ISS, and looking up some
// other satellite's launch date here would show a mismatched time-in-space
// next to the ISS's orbital numbers (confirmed as a real bug, fixed in the
// demo first - see sat_display_demo.py's main()).
FetchResult fetchLaunchDate(const String& noradId, time_t& out, String* errOut = nullptr);

// Best-effort real-name lookup via n2yo, mirroring the demo's fetch_name():
// CelesTrak's own OBJECT_NAME can lag for days/weeks on freshly-launched,
// multi-payload objects (generic "OBJECT A"/"OBJECT B"/...); n2yo usually
// has the real name sooner. `outName` is untouched unless Ok (no API key,
// network failure, or no name for this object) - caller just keeps
// CelesTrak's name in that case.
//
// Same "use the actually-displayed object's id" rule as fetchLaunchDate()
// above applies here too - callers should skip this entirely (not just
// pass a different id) when showing fallback data, since overwriting the
// clearly-labeled "ISS (fallback)" with a plain "ISS" would quietly lose
// the one signal (besides the ONLINE/OFFLINE dot) that the data is stale.
FetchResult fetchN2yoName(const String& noradId, const String& apiKey, String& outName, String* errOut = nullptr);
