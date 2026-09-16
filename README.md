# Satellite Tracker Buddy

A desk gadget that tracks any satellite you choose (by NORAD catalog
number) and shows its live ground track, apogee/perigee, and a few other
stats on a small e-paper display, in a 3D-printed frame. Orbital elements
come from [CelesTrak](https://celestrak.org/); the position is computed
on-device with SGP4 - no cloud service, no app, just a WiFi-connected panel
on your desk.

## Features

- **Track anything, not just the ISS** - punch in any NORAD catalog number,
  or pick from a short list of favourites (ISS, Tiangong, Hubble, Spectrum).
- **Live ground track** - past track dashed, future track solid, both
  scaled to the satellite's actual orbital period so a slow GEO/Molniya
  orbit doesn't fill the trail with one lap's worth of samples in a few
  minutes the way a fast LEO pass would. Drawn in white ink over land and
  black over sea, so it stays visible crossing continents instead of
  disappearing into the landmass fill.
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
- **Scan to open the config page** - a small QR code sits right next to the
  WiFi icon while connected. No need to know the device's IP or dig through
  your router's client list - just scan it with a phone camera.
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
- **Next pass over your location** - set your site's latitude/longitude on
  the config page and the display shows when the satellite will next be at
  least 10° above your horizon (a countdown plus the clock time), or "Now"
  if it's overhead already. Not every orbit passes over every location -
  it'll correctly show "None" rather than a wrong guess when the satellite
  genuinely never reaches your site.
- **Configurable elements-fetch interval** (see the config page, below) -
  how often CelesTrak is re-polled for orbital data, kept at a safe minimum
  to respect their rate limits (see "Rate limits" below).
- **Firmware version on the config page** - the status panel shows exactly
  which build is running (derived from the git tag at compile time), so you
  can tell at a glance whether a device is up to date.

## How to use it

### The config page

Scan the small QR code next to the WiFi icon on the display with your
phone, or browse to `http://<device ip>/` if you already know it (check
your router's client list).

A status panel at the top shows WiFi (connected network + signal strength),
the last elements fetch, launch-date fetch, and n2yo lookup outcomes - "OK"
or the specific problem (e.g. "HTTP 403"), highlighted so real problems
stand out from normal/unconfigured states (like "no key configured"). It
also shows a live count of CelesTrak requests made in the last 2 hours
(persisted across reboots, so a restart can't hide recent activity),
highlighted once it gets close to their actual 50-request firewall
threshold - see "Rate limits" below.

Saving refetches/recomputes whatever changed (satellite, site location,
n2yo key) immediately, and the display redraws right away too rather than
waiting for its next scheduled refresh - so you see the result of a save
within a few seconds, not up to `displayRefreshMinutes` later. WiFi and
hostname changes are the exception: those restart the device to take
effect, same as before.

- **NORAD catalog id** - type any number, or click a favourite (ISS,
  Tiangong, Hubble, Spectrum). Saving refetches elements immediately and
  clears the trail.
- **Site latitude/longitude** - optional; enables the "Next pass" prediction
  on the display (see Features above). Leave both at 0 to disable it. A
  "Use my location" button fills these in from your browser's precise
  geolocation where that's allowed (many browsers block it on this
  device's plain-HTTP address); it automatically falls back to an
  approximate, city-level location looked up by the device itself from
  its network connection if the precise version isn't available, so the
  button works either way without needing a browser workaround. You can
  still type coordinates in manually instead.
- **n2yo API key** - optional, free-tier; resolves the satellite's real name
  sooner than CelesTrak's own catalog does for freshly-launched objects (see
  Features above). Only looked up when elements were actually fetched live -
  skipped while showing fallback data, so it can't attach the wrong
  satellite's name to the ISS fallback elements. The field is masked like a
  password field. Leaving it blank on save keeps the current key - there's
  a separate checkbox to actually remove it.
- **Display full-refresh interval** - how often the e-paper redraws, in
  minutes (1-60, default 2). Takes effect immediately, no restart.
- **Elements/launch-date fetch interval** - how often CelesTrak is polled,
  in minutes (10-1440, default 1440/24h). Kept at a 10-minute floor to stay
  well clear of CelesTrak's rate limit even at the most aggressive setting
  (see "Rate limits" below). Takes effect immediately, no restart.
- **Device hostname** - shown to your router/DHCP. Takes effect on the next
  reconnect (WiFi hostnames are set at connection time, not live).
- **Update manifest URL** - advanced; leave it alone unless you're testing
  against your own server or a fork's releases. See "Firmware updates"
  below.
- **WiFi** - shows the currently-connected network, a dropdown of nearby
  scanned networks, and SSID/password fields to switch. Leave the SSID
  field blank to change other settings without touching WiFi. Saving a new
  network restarts the device to reconnect.

### Firmware updates

The status panel at the top of the config page shows the running firmware
version and an update status line, next to a **Check for updates** button.
The device checks once automatically at every boot, so the status line
already has an answer - "up to date", "update available: X.Y.Z", or a
failure reason - the first time you load the page after a power-up; the
button is there for an on-demand recheck any other time. Nothing beyond
that check happens on its own: no repeated/periodic checking, no
background download, and nothing installs without you clicking
**Install**. Nothing to configure, no toggle to find.

1. Load the config page (or click **Check for updates** for a fresh check).
   The status line says either "up to date", "update available: X.Y.Z
   (running A.B.C, N KB download)", or the specific reason it failed (no
   WiFi, no release published, a network error).
2. If a newer version was found, an **Install `<version>`** button appears
   right below the status line - click it once, no confirmation prompt.
   Installing verifies the download against the release's published
   checksum before it's written anywhere permanent; a corrupted or
   incomplete download is discarded rather than installed.
3. The device restarts on its own once the install finishes (a minute or
   two, depending on your connection). Reload the config page after a short
   wait to see the new version.
4. If the new build can't get online within about 10 minutes of that
   restart, the device automatically reverts to the previous version by
   itself - no bad update can leave the device stuck.

A dev build (anything not built from a clean `fw-v*` tag - the version
string on the page will show a commit hash or a `-dirty`/`-N-gHASH` suffix
instead of a plain `X.Y.Z`) shows "update check disabled" instead, since
there's no released version to compare it against.

### Changing WiFi later without the config page

Hold the board's **BOOT** button (built into every ESP32 dev board, no
extra wiring) for **3 seconds while the device is already running** (not
while powering it on - see the warning below). It blinks the onboard LED
5 times to confirm, forgets the stored WiFi network, and reopens the
`SatelliteTrackerBuddy-Setup` portal - same flow as first boot.

> **Don't hold BOOT while powering the device on.** GPIO0 (the BOOT button)
> doubles as the ESP32's boot-mode strapping pin - holding it low at the
> exact moment the chip leaves reset makes the ROM bootloader drop into its
> UART flashing mode instead of running the app at all. There's no LED
> blink, no serial output, nothing - it's just sitting there waiting for a
> programmer. Power on normally, let it reach the tracking screen, *then*
> hold BOOT.

### Display behavior

Full refresh every 2 minutes by default (configurable, see above) - each
refresh takes ~2.6s and visibly flashes, which is normal e-paper behavior,
not a fault.

### Rate limits

The firmware is intentionally polite to both outside services it talks to -
elements and the launch date are refetched once a day by default (this is
the "Elements/launch-date fetch interval" config-page field, adjustable
10-1440 minutes), plus immediately whenever you change the tracked
satellite, and never polled continuously.

- **CelesTrak** (orbital elements, launch date): no API key needed, but
  their [usage policy](https://celestrak.org/usage-policy.php) firewalls an
  IP after 50 HTTP error responses (403/404/301/50x) within a 2-hour
  window. Normal use - about one fetch a day, plus the occasional satellite
  change - stays nowhere near that. Even at the fetch interval's minimum
  10-minute setting, that's at most 24 requests/2h (2 requests per fetch,
  elements + launch date) - still well under the 50-error threshold. If the
  config page ever shows "HTTP 403", it clears on its own after a while;
  the device keeps tracking on its last-known (stale) data in the
  meantime, marked OFFLINE.
- **n2yo** (optional name lookup): the free tier allows up to 1000
  requests/hour. The firmware makes at most one n2yo call per satellite
  selection/refetch - far under that limit even with frequent manual
  satellite changes or the fetch interval set to its minimum.

## Building the hardware

### What you need

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
   named **`SatelliteTrackerBuddy-Setup`**. Join it from a phone - a sign-in page
   should pop up automatically (if it doesn't, browse to `192.168.4.1`).
2. Pick your network from the scanned list (or type one in for hidden
   networks) and enter the password. Save - the device restarts and joins
   your network.
3. From here on, see "How to use it" above.

## Known limitations / not built yet

- No favourites-cycle physical button (BOOT is currently spoken for by the
  WiFi-reset gesture above).
- No deep sleep / battery power path - the device expects to be USB-powered.
- No frame/CAD yet.
- No MQTT control.
- Firmware updates are manual-only by design (see "Firmware updates"
  above) - there's no auto-check timer or auto-install, and the update
  download isn't cryptographically signed yet (only checksum-verified),
  so it isn't hardened against a hostile network the way a background
  auto-update would need to be.
- CelesTrak rate-limiting can occasionally show OFFLINE - see Rate limits
  above.

## Disclaimer

This is a hobby project, not a certified kit. Building and using it -
wiring the board, flashing firmware, powering it from USB, 3D-printing and
assembling a frame - is at your own risk. Nothing here is liable for any
damage to your hardware, property, or person that results from building or
using it. See [LICENSE](LICENSE) for the software itself, which is provided
as-is with no warranty.

## License

[MIT](LICENSE).

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

- Keep this file up to date whenever a user-facing feature changes (config
  page fields, button behavior, setup steps, etc.) - it's the "what is this
  and how do I use it" doc.
- Keep the demo and firmware renderers visually identical (change a
  rendering rule in the demo first, check it there, then port it).
- All times UTC internally. Only convert for display.
- No blocking network calls inside the render loop (fetch in setup or a
  scheduled task) - and never from inside an AsyncWebServer request
  handler, which runs on the `async_tcp` task and will crash the device via
  watchdog if blocked for more than a couple seconds.
- Commit small. Don't add dependencies without a comment saying why.
