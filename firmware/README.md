# firmware/

PlatformIO project for the ESP32, e-paper build only (see CLAUDE.md). Implements
CLAUDE.md's Next Steps **#1 and #2**: WiFi + NTP + NVS config + orbital-element
fetch + serial-printed lat/lon, plus the captive portal and config web page.
Next Steps **#3 (display renderer)** is not here yet - no panel hardware to
test the renderer against.

## Status: scaffolded, NOT YET COMPILED OR FLASHED

This was written against library source read on GitHub (function signatures
for Hopperpop/Sgp4-Library and ESPAsyncWebServer verified by fetching their
actual headers/source, not guessed), but nothing here has been through a real
`pio run` or run on real hardware yet. Known things to check on first build:

- `core/sgp4_track.cpp`'s direct `sgp4init()` call - verified against
  `Hopperpop/Sgp4-Library`'s `sgp4unit.h`/`sgp4pred.cpp` source, but not
  compiled. If it doesn't link, the free function may need an explicit
  `#include <sgp4unit.h>` (should already be pulled in transitively via
  `Sgp4.h` -> `sgp4pred.h`).
- `Sgp4::findsat(unsigned long)` is used for propagation after we populate
  `satrec` ourselves (bypassing the library's own TLE-text `init()`) - the
  interface exists per the library's header, but its internal behavior
  wasn't traced. Per CLAUDE.md Next Steps #1: verify computed lat/lon against
  `demo/sat_display_demo.py` to within a few km, for ISS *and* one GEO object,
  before trusting it further.
- `lib_deps` in `platformio.ini` uses the current names/orgs for
  ESPAsyncWebServer and AsyncTCP (`ESP32Async/...`, the actively maintained
  fork as of writing) - confirm these still resolve when you first
  `pio run`, library ownership in this ecosystem has moved before.
- `WiFiClientSecure::setInsecure()` is used for the CelesTrak/HTTPS fetch (no
  CA bundle embedded) - acceptable for a read-only hobby fetch, but a
  deliberate simplification worth knowing about.
- `core/elements.cpp` uses `String::toDouble()` - present on ESP32's Arduino
  core, but if the toolchain in use doesn't have it, swap for `.toFloat()`
  (fine precision-wise for these fields either way).

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
