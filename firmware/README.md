# firmware/

PlatformIO project for the ESP32, e-paper build only. WiFi captive portal
(+ auto-reconnect), NVS config, OMM/CSV element fetch, SGP4, the config web
page, and the e-paper renderer (map, land, night shading, track, reticle,
name/orbit-class, apogee/perigee/incl/period, time-in-space, next-pass
prediction, firmware version) are all wired up and running on real
hardware.

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

Verification results (satisfies the "verify against the demo to within a
few km" requirement):

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

Remaining known things to double check (kept from the original bring-up -
not necessarily still exhaustive):

- `lib_deps` in `platformio.ini` uses `ESP32Async/...` for ESPAsyncWebServer
  and AsyncTCP - resolved fine as of this build, but that ecosystem's library
  ownership has moved before, worth a glance if a future `pio run` fails to
  fetch them.
- `WiFiClientSecure::setInsecure()` is used for the CelesTrak/HTTPS fetch (no
  CA bundle embedded) - acceptable for a read-only hobby fetch, but a
  deliberate simplification worth knowing about.
- The config page's favourite buttons specifically (as opposed to typing a
  NORAD id manually, which is verified) still haven't been clicked through.
- The n2yo key field specifically hasn't been checked for surviving a reboot
  (NVS persistence) - site lat/lon and NORAD id have been, repeatedly, in
  normal use since this was originally written.

## Build / flash

```
cd firmware
pio run                        # compile
pio run --target upload        # flash (board plugged in via USB)
pio device monitor -b 115200   # serial output
```

## First-boot flow

1. No WiFi creds in NVS -> device opens a `SatelliteTrackerBuddy-Setup` WiFi AP with a
   captive portal. Connect to it from a phone, it should prompt you to the
   setup page automatically (or browse to `192.168.4.1`); enter your WiFi
   SSID/password, save. Device restarts and joins that network.
2. Once online, the serial monitor prints its IP. Browse to `http://<that ip>/`
   for the full config page: NORAD id (plus favourite buttons - ISS, Tiangong,
   Hubble, Spectrum), site lat/lon, and an optional n2yo API key field (used
   for best-effort real-name resolution on freshly-launched objects).
3. Saving triggers an immediate elements refetch, logged to serial along with
   apogee/perigee/period, then lat/lon once a second.

## Current status and what's not built yet

This file's history above is from the original bring-up scaffold and is
kept as-is for the record (bug fixes, verification numbers). It's since
grown well past that point - display rendering, the trail buffer, WiFi
change/reset, config page, etc. are all built and running. For what the
firmware actually does today and how to use it, see the repo root's
[README.md](../README.md).
