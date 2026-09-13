# Satellite Ground-Track Display

A desk gadget that tracks any satellite you choose (by NORAD catalog
number) and shows its live ground track, apogee/perigee, and a few other
stats on a small e-paper display, in a 3D-printed frame. Orbital elements
come from [CelesTrak](https://celestrak.org/); the position is computed
on-device with SGP4 - no cloud service, no app, just a WiFi-connected panel
on your desk.

**Keep this file up to date whenever a user-facing feature changes**
(config page fields, button behavior, setup steps, etc.) - it's the "what
is this and how do I use it" doc. For the detailed, chronological
development log (what's been tried, what broke, what's still open) see
[CLAUDE.md](CLAUDE.md).

## Features

- **Track anything, not just the ISS** - punch in any NORAD catalog number,
  or pick from a short list of favourites (ISS, Tiangong, Hubble, Spectrum).
- **Live ground track** - past track dashed, future track solid, both
  scaled to the satellite's actual orbital period so a slow GEO/Molniya
  orbit doesn't fill the trail with one lap's worth of samples in a few
  minutes the way a fast LEO pass would.
- **Day/night terminator** shown on the map (sub-solar point), so you can
  tell at a glance whether the satellite is in daylight or eclipse.
- **Apogee, perigee, inclination, period, orbit class** (LEO/MEO/GEO/SSO/
  Molniya/GTO/HEO) and **time in space**, computed from the orbital elements
  - no extra API needed for any of it except the launch date.
- **Handles high orbits gracefully** - GEO/Molniya/GPS objects with
  hours-to-a-day periods and 35,000+ km apogees display correctly, not just
  fast, close LEO passes.
- **WiFi status at a glance** - a signal-bars icon (bottom-right) shows
  whether the radio is actually connected, separate from the ONLINE/OFFLINE
  indicator near the title (that one reflects whether the *last CelesTrak
  fetch* succeeded - the two can disagree, e.g. WiFi's fine but CelesTrak is
  temporarily rate-limiting).
- **A little easter egg**: if the tracked object's name is exactly
  **SPECTRUM** (Isar Aerospace's rocket, whose second stage flies a single
  engine), the "you are here" marker becomes a tiny rocket silhouette
  instead of the usual reticle.
- **Better names sooner** - CelesTrak's own `OBJECT_NAME` can lag for
  days/weeks on freshly-launched, multi-payload objects (shows a generic
  "OBJECT A", "OBJECT B", ...); an optional n2yo API key (config page)
  resolves the real name sooner, in both the demo and firmware.
- **Configurable display refresh rate and hostname** (see the config page,
  below).
- **Change WiFi networks any time**, either from the config page or a
  physical button - no need to re-flash or factory-reset.
- **Connection status at a glance** - the config page shows whether WiFi,
  the elements fetch, the launch-date fetch, and the n2yo lookup are each
  working, with the specific error (e.g. an HTTP status code) when one
  isn't - no serial monitor needed to see what's wrong.

## Setting up

### Hardware

- An ESP32 dev board (plain ESP32-WROOM is fine; ESP32-S3 also works. Not
  ESP8266 - HTTPS + web config + SGP4 is too tight on it).
- A Waveshare 7.5" V2 800×480 e-paper panel + its ESP32 Driver Board/HAT.
- USB cable, a WiFi network, and (for setup) a phone or laptop.

### Build and flash

```
cd firmware
pio run                     # compile (PlatformIO, not the Arduino IDE)
pio run -t upload           # flash over USB
pio device monitor -b 115200   # serial output, for following boot/tracking logs
```

### First boot

1. With no WiFi network stored yet, the device opens its own WiFi network
   named **`SatTracker-Setup`**. Join it from a phone - a sign-in page
   should pop up automatically (if it doesn't, browse to `192.168.4.1`).
2. Pick your network from the scanned list (or type one in for hidden
   networks) and enter the password. Save - the device restarts and joins
   your network.
3. The serial monitor prints the device's new IP once it's online. Browse
   to `http://<that ip>/` for the full config page.

### The config page (`http://<device ip>/`)

A status panel at the top shows WiFi (connected network + signal strength),
the last elements fetch, launch-date fetch, and n2yo lookup outcomes - "OK"
or the specific problem (e.g. "HTTP 403"), highlighted so real problems
stand out from normal/unconfigured states (like "no key configured").

- **NORAD catalog id** - type any number, or click a favourite (ISS,
  Tiangong, Hubble, NOAA-19). Saving refetches elements immediately and
  clears the trail.
- **Site latitude/longitude** - not yet used by the renderer, reserved for a
  future "distance/pass from here" feature.
- **n2yo API key** - optional, free-tier; resolves the satellite's real name
  sooner than CelesTrak's own catalog does for freshly-launched objects (see
  Features above). Only looked up when elements were actually fetched live -
  skipped while showing fallback data, so it can't attach the wrong
  satellite's name to the ISS fallback elements.
- **Display full-refresh interval** - how often the e-paper redraws, in
  minutes (1-60, default 2). Takes effect immediately, no restart.
- **Device hostname** - shown to your router/DHCP. Takes effect on the next
  reconnect (WiFi hostnames are set at connection time, not live).
- **WiFi** - shows the currently-connected network, a dropdown of nearby
  scanned networks, and SSID/password fields to switch. Leave the SSID
  field blank to change other settings without touching WiFi. Saving a new
  network restarts the device to reconnect.

### Changing WiFi later without the config page

Hold the board's **BOOT** button (built into every ESP32 dev board, no
extra wiring) for **3 seconds while the device is already running** (not
while powering it on - see the warning below). It blinks the onboard LED
5 times to confirm, forgets the stored WiFi network, and reopens the
`SatTracker-Setup` portal - same flow as first boot.

> **Don't hold BOOT while powering the device on.** GPIO0 (the BOOT button)
> doubles as the ESP32's boot-mode strapping pin - holding it low at the
> exact moment the chip leaves reset makes the ROM bootloader drop into its
> UART flashing mode instead of running the app at all. There's no LED
> blink, no serial output, nothing - it's just sitting there waiting for a
> programmer. Power on normally, let it reach the tracking screen, *then*
> hold BOOT.

### Display behavior

- Full refresh every 2 minutes by default (configurable, see above) - each
  refresh takes ~2.6s and visibly flashes, which is normal e-paper
  behavior, not a fault.

## Known limitations / not built yet

- No favourites-cycle physical button (BOOT is currently spoken for by the
  WiFi-reset gesture above).
- No deep sleep / battery power path - the device expects to be USB-powered.
- No frame/CAD yet.
- No MQTT control.
- CelesTrak's `gp.php` endpoint enforces a firewall after 50 HTTP error
  responses in a 2-hour window (see celestrak.org/usage-policy.php) - if
  elements stop refreshing, the device falls back to stale cached data and
  shows OFFLINE rather than crashing; this generally clears on its own
  after a while.

---

## Developer notes

The rest of this file is for people working on the code, not using the
finished device.

### Repo layout

```
demo/          sat_display_demo.py - PC simulator, also the renderer spec
firmware/      PlatformIO project, env `epaper` (the only build target)
  src/main.cpp        boot sequence + main loop
  src/core/           config (NVS), wifi_setup (captive portal + scanning),
                      elements (OMM/CSV + launch-date fetch), sgp4_track
  src/web/            the config page (PROGMEM HTML), GET/POST /config
  src/display/        epaper_render (the renderer), astro (day/night),
                      trail_buffer, land_mask.h (generated, see tools/)
tools/         mask_to_progmem.py - regenerates firmware's land mask from
               the demo's own embedded one (don't hand-edit the header)
cad/           frame/bezel/diffuser - not started yet
```

`demo/` is a PC simulator (Python + pygame) of the exact display, and it's
the *spec* for the firmware's renderer, not just a prototype - if the two
ever disagree visually, the demo is right and the firmware has a bug. See
[firmware/README.md](firmware/README.md) for the firmware's own bring-up/
verification log.

### Running the demo

```
cd demo
pip install pygame sgp4
python sat_display_demo.py            # default satellite
python sat_display_demo.py 25544      # any NORAD id
python sat_display_demo.py 25544 --speed 60   # 60x time-lapse
```

Keys: `+`/`-` change time speed, `space` resets to real time, `q` quits.

An optional `demo/secrets.local.json` (gitignored) with `{"n2yo_api_key":
"..."}` enables the n2yo name lookup; without it, the demo just uses
CelesTrak's name.

### Architecture (shared by demo and firmware)

```
boot  → WiFi → NTP → fetch orbital elements (CelesTrak) → init SGP4
      → compute apogee/perigee → best-effort name lookup (n2yo, optional)
loop  → every 1s:  SGP4(now) → lat/lon → push to trail ring buffer → render()
      → every 2min (configurable): full e-paper redraw
      → every 24h:  refetch elements
```

- Orbital elements are fetched as OMM/CSV, not legacy TLE text - catalog
  numbers ≥100000 don't fit the classic TLE's fixed-width satellite-number
  field, so `FORMAT=tle`/`3le` 404s for those objects while CSV/JSON still
  work. Parsed with `sgp4.omm.parse_csv`/`initialize` (Python) rather than
  hand-slicing TLE columns.
- Apogee/perigee: `a = (μ/n²)^(1/3)` with `n` in rad/s from mean motion,
  `apo = a(1+e) - 6371`, `peri = a(1-e) - 6371`, μ = 398600.4418 km³/s².
- Projection is plain equirectangular: `x = (lon+180)/360*W`,
  `y = (90-lat)/180*H`. Land mask is a 360x180 1-bit bitmap, embedded as a
  base64 PNG in the demo and exported for firmware as a packed 1bpp PROGMEM
  array by `tools/mask_to_progmem.py`.
- Orbit class (LEO/MEO/GEO/SSO/Molniya/GTO/HEO) is a local heuristic from
  apogee/perigee/inclination/period (`classify_orbit()`/`classifyOrbit()`)
  - CelesTrak doesn't provide a real regime label, so treat this as a
    recognizable label, not an authoritative classification.

### Conventions

See [CLAUDE.md](CLAUDE.md) for the full list, notably: keep the demo and
firmware renderers visually identical (change a rendering rule in the demo
first, check it there, then port it); all times UTC internally; no blocking
network calls inside the render loop (fetch in setup or a scheduled task -
and never from inside an AsyncWebServer request handler, which runs on the
`async_tcp` task and will crash the device via watchdog if blocked for more
than a couple seconds); commit small; don't add dependencies without a
comment saying why.
