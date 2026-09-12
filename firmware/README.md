# firmware/

PlatformIO project for the ESP32, e-paper build only (see CLAUDE.md). Implements
CLAUDE.md's Next Steps **#1 and #2**: WiFi + NTP + NVS config + orbital-element
fetch + serial-printed lat/lon, plus the captive portal and config web page.
**#1 and #2 are now verified working on real hardware** (see Status below).
Next Steps **#3 (display renderer)** is not here yet - no panel hardware to
test the renderer against.

## Status: bring-up done, core loop verified working on real hardware

Compiled clean on the first `pio run` (SGP4 library linkage, `lib_deps` names,
`String::toDouble()` - all fine, no changes needed). Flashed to a real ESP32
board and fully verified: captive portal, WiFi join, NTP sync, element fetch,
config-page satellite change, and SGP4 tracking for both ISS (LEO) and TDRS-3
(GEO), matching `demo/sat_display_demo.py` to ~1-18 km (see below).

One real bug was found and fixed along the way:

- **`satrec.jdsatepoch` was never set**, because `sgp4init()` (the low-level
  call we use instead of the library's TLE-text `init()`) doesn't touch it -
  only the TLE-parsing path does. `Sgp4::findsat()` computes
  `tsince = (jdNow - satrec.jdsatepoch) * 1440`, so with `jdsatepoch` stuck at
  0 every propagation used a wildly wrong `tsince` and failed
  (`satrec.error != 0` on every call, logged as "propagation error").
  Fix (in `core/sgp4_track.cpp`): set `sgp4lib.satrec.jdsatepoch = jd` (the
  full Julian date the `epoch` parameter was derived from) right after
  `sgp4init()` succeeds. Orbit init itself (apogee/perigee/period) was correct
  from the very first flash - only propagation was affected, which is why
  this didn't show up until watching the live lat/lon loop.

Verification results (see CLAUDE.md's TODO - this satisfies the "verify
against the demo to within a few km" requirement):

- **ISS**: ground distance ~18 km, altitude ~0.7 km off, longitude exact.
  Fully explained by geocentric vs. geodetic latitude - the demo computes
  latitude with a plain sphere (`atan2(z, hypot(x,y))`, radius - 6371 km
  flat); the C++ library's `ijk2ll()` does the proper WGS84-ellipsoid
  conversion. Checked the WGS84 flattening formula for -32.5 deg geocentric
  latitude: predicts ~19.3 km, matches the observed ~18 km. Not a bug - if
  anything the firmware's conversion is the more correct one.
- **GEO (TDRS-3, NORAD 19548)**: ground distance ~1.3 km, altitude ~5.8 km
  off (0.016% at GEO's ~35800 km scale) - same geocentric/geodetic effect
  plus WGS72 vs WGS84 gravitational-constant differences between the two
  SGP4 ports. Negligible for a ground-track display either way.

Remaining known things to double check as work continues:

- `lib_deps` in `platformio.ini` uses `ESP32Async/...` for ESPAsyncWebServer
  and AsyncTCP - resolved fine as of this build, but that ecosystem's library
  ownership has moved before, worth a glance if a future `pio run` fails to
  fetch them.
- `WiFiClientSecure::setInsecure()` is used for the CelesTrak/HTTPS fetch (no
  CA bundle embedded) - acceptable for a read-only hobby fetch, but a
  deliberate simplification worth knowing about.
- The config page's favourite buttons specifically (as opposed to typing a
  NORAD id manually, which is verified) haven't been clicked through yet.
  Captive portal + WiFi join + config-page manual NORAD change are all
  verified working.
- Site lat/lon and the n2yo key fields haven't been checked for actually
  surviving a reboot (NVS persistence) - only NORAD id has been.

## Build / flash

```
cd firmware
pio run                        # compile
pio run --target upload        # flash (board plugged in via USB)
pio device monitor -b 115200   # serial output
```

## First-boot flow

1. No WiFi creds in NVS -> device opens a `SatTracker-Setup` WiFi AP with a
   captive portal. Connect to it from a phone, it should prompt you to the
   setup page automatically (or browse to `192.168.4.1`); enter your WiFi
   SSID/password, save. Device restarts and joins that network.
2. Once online, the serial monitor prints its IP. Browse to `http://<that ip>/`
   for the full config page: NORAD id (plus favourite buttons - ISS, Tiangong,
   Hubble, NOAA-19), site lat/lon, and an optional n2yo API key field (stored,
   not used yet - name resolution isn't wired into firmware yet either).
3. Saving triggers an immediate elements refetch, logged to serial along with
   apogee/perigee/period, then lat/lon once a second.

## Deliberately not in this scaffold yet

- Display rendering (any mode) - `src/display/` doesn't exist yet.
- The trail ring buffer, and period-scaled sampling for high orbits - both
  need the display work to make sense; TODOs are left at the two spots in
  `main.cpp` where they'll plug in.
- n2yo name lookup, launch-date fetch, time-in-space, orbit classification -
  all exist in the demo but weren't asked for in this pass; the config page
  already collects and stores the n2yo key so wiring the lookup in later is a
  small addition, not a rework.
- The favourites-cycle button and deep sleep (need real hardware/frame first).
