#!/usr/bin/env python3
"""
Satellite Tracker Buddy - PC simulator for the e-paper build (see CLAUDE.md;
the LED-matrix and HUB75 modes that used to live here were dropped along with
that decision - this is now just the spec for firmware's e-paper renderer).

    pip install pygame sgp4

    python sat_display_demo.py            # default NORAD id
    python sat_display_demo.py 25544      # any NORAD id
    python sat_display_demo.py 25544 --speed 60   # 60x time-lapse
    python sat_display_demo.py 25544 --lat 52.5 --lon 13.4   # + next-pass prediction

Keys:  +/- = time speed   space = real time   q = quit
"""
import sys, os, io, json, math, base64, time, urllib.request
from datetime import datetime, timedelta, timezone
import pygame
from sgp4.api import Satrec, jday
import sgp4.omm as omm

# ---------- world land mask, 800x400 equirectangular, embedded PNG ----------
# Rasterized from Natural Earth's public-domain 1:50m land-polygon dataset
# (naturalearthdata.com - "No permission is needed to use Natural Earth...
# Crediting the authors is unnecessary."), fetched from
# https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_land.geojson
# and rendered to this exact size/threshold convention by
# tools/gen_land_mask.py. Regenerate with that script (not by hand) if this
# ever needs to change - it re-fetches the same source and reproduces this
# byte-for-byte.
#
# History, since none of this was obvious up front: an earlier version had
# no recorded source at all - that's why tools/gen_land_mask.py exists.
# First replacement was Natural Earth 1:110m at the *old* 360x180 size (1
# pixel/degree) - a size chosen for no recorded reason either, as it turns
# out. Tried 1:50m at that same 360x180 size next, expecting a clear
# improvement (110m does lose some small islands/inlets at the source-
# vector level, e.g. the Lesser Antilles), and it did recover that - but on
# the real e-paper panel it read as *worse* over Iberia, and a pixel diff
# against the 110m version found only single-pixel coastal edge changes
# there, not an actual data problem. The real culprit: at 360x180, every
# mask pixel was drawn as a ~2.2x2.2 *physical-pixel block* on the 800x400
# map area (both demo and firmware upscaled it that way) - visibly blocky
# on real hardware, and a bottleneck no amount of source-dataset
# resolution can fix. This version is rasterized directly at the map's own
# native 800x400 - no upscaling step at all. At that resolution 1:50m vs
# 1:10m source data converges to visually indistinguishable (checked with
# zoomed crops), so 1:50m stays the pick on size/fetch-time grounds alone.
MASK_B64 = "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"
FALLBACK_OMM = {  # ISS, used if CelesTrak is unreachable (epoch will be stale)
    "OBJECT_NAME": "ISS (fallback)", "OBJECT_ID": "1998-067A",
    "EPOCH": "2026-09-10T11:11:07.892448", "MEAN_MOTION": "15.49068969",
    "ECCENTRICITY": ".00049899", "INCLINATION": "51.6301",
    "RA_OF_ASC_NODE": "238.0248", "ARG_OF_PERICENTER": "125.6916",
    "MEAN_ANOMALY": "234.4537", "EPHEMERIS_TYPE": "0", "CLASSIFICATION_TYPE": "U",
    "NORAD_CAT_ID": "25544", "ELEMENT_SET_NO": "999", "REV_AT_EPOCH": "58497",
    "BSTAR": ".98181333E-4", "MEAN_MOTION_DOT": ".4975E-4", "MEAN_MOTION_DDOT": "0",
}

def fetch_omm(norad):
    """Fetch orbital elements as OMM/CSV rather than legacy TLE text: catalog
    numbers >=100000 (newly launched objects) don't fit the TLE format's
    fixed 5-digit satellite-number field, so CelesTrak 404s FORMAT=tle/3le
    for them while CSV/JSON still work fine.
    Returns (fields, online) - `online` is False whenever we had to fall back
    to the stale hardcoded data, so callers/displays can show that."""
    url = f"https://celestrak.org/NORAD/elements/gp.php?CATNR={norad}&FORMAT=csv"
    try:
        txt = urllib.request.urlopen(url, timeout=10).read().decode()
        rows = list(omm.parse_csv(io.StringIO(txt)))
        if not rows:
            raise ValueError("No GP data found")
        return rows[0], True
    except Exception as e:
        print("Orbital element fetch failed, using fallback:", e)
        return dict(FALLBACK_OMM), False

def fetch_launch_date(norad):
    """Launch date isn't part of the OMM/CSV element set, so pull it
    separately from CelesTrak's satcat. Best-effort: on any failure we just
    don't show time-in-space rather than blocking startup on it."""
    url = f"https://celestrak.org/satcat/records.php?CATNR={norad}&FORMAT=json"
    try:
        rows = json.loads(urllib.request.urlopen(url, timeout=10).read().decode())
        d = rows[0]["LAUNCH_DATE"]
        return datetime.strptime(d, "%Y-%m-%d").replace(tzinfo=timezone.utc) if d else None
    except Exception as e:
        print("Launch date fetch failed:", e)
        return None

# ---------- local secrets (gitignored, never committed - see .gitignore) ----------
# On the firmware this becomes one more field in the NVS config set via the
# WiFi setup page, alongside the WiFi creds, NORAD id and site lat/lon.
SECRETS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "secrets.local.json")
def load_secrets():
    try:
        with open(SECRETS_PATH) as f:
            return json.load(f)
    except FileNotFoundError:
        return {}
    except Exception as e:
        print(f"Couldn't read {SECRETS_PATH}:", e)
        return {}

def fetch_name(norad, api_key):
    """Best-effort display name via n2yo. n2yo curates real names (e.g.
    "SPECTRUM") faster than CelesTrak folds them into its official catalog
    for freshly-launched, multi-payload objects - CelesTrak may still show a
    generic "OBJECT F" for days/weeks. One lookup per satellite selection
    (not per frame), well under n2yo's free-tier rate limit. Returns None on
    any failure/missing key so the caller keeps CelesTrak's own name."""
    if not api_key:
        return None
    url = f"https://api.n2yo.com/rest/v1/satellite/tle/{norad}&apiKey={api_key}"
    try:
        data = json.loads(urllib.request.urlopen(url, timeout=10).read().decode())
        name = data.get("info", {}).get("satname")
        return name.strip() if name else None
    except Exception as e:
        print("n2yo name lookup failed, keeping CelesTrak name:", e)
        return None

# ---------- orbit maths ----------
MU, RE = 398600.4418, 6371.0

def classify_orbit(apogee, perigee, incl, period):
    """Rough, human-friendly orbit label from apogee/perigee/inclination/period.
    Not a rigorous classification, just enough to be a recognizable word on
    the display (LEO/SSO/MEO/GEO/Molniya/HEO/GTO)."""
    alt, spread = (apogee+perigee)/2, apogee-perigee
    if spread > 15000 and 55 <= incl <= 65:      # near the 63.4 deg "critical
        return "Molniya"                          # inclination" that avoids apsidal drift
    if abs(period-1436) < 60 and spread < 500 and incl < 15:
        return "GEO"
    if spread > 15000:
        return "GTO" if perigee < 5000 else "HEO"
    if alt < 2000:
        return "SSO" if 95 <= incl <= 105 else "LEO"
    return "MEO" if alt < 35000 else "HEO"

class Sat:
    def __init__(self, fields, launch_date=None, online=True):
        self.name = fields["OBJECT_NAME"].strip()
        self.launch_date = launch_date
        self.online = online   # False if we had to fall back to stale hardcoded data
        self.rec = Satrec()
        omm.initialize(self.rec, fields)
        n = float(fields["MEAN_MOTION"]) * 2*math.pi/86400   # rad/s
        e = float(fields["ECCENTRICITY"])
        a = (MU / n**2) ** (1/3)
        self.apogee, self.perigee = a*(1+e)-RE, a*(1-e)-RE
        self.incl, self.period = float(fields["INCLINATION"]), 2*math.pi/n/60
        self.orbit_class = classify_orbit(self.apogee, self.perigee, self.incl, self.period)

    def age(self, now):
        """Days/years in space as of `now`, or None if launch date is unknown.
        Days is the count of *complete* elapsed days (floored, not rounded) -
        matches the usual "N days old" convention. Rounding to nearest
        (the original behavior) made this tick over to the next day at the
        halfway mark instead of at a full day boundary - confirmed as a
        real discrepancy: NORAD 100614 launched 2026-09-05, and on
        2026-09-13 at 12:50 UTC (8.53 exact days later) it showed "9 d"
        instead of the expected "8 d"."""
        if not self.launch_date: return None
        days = (now - self.launch_date).total_seconds() / 86400
        return math.floor(days), days / 365.25

    def latlon(self, t):
        jd, fr = jday(t.year, t.month, t.day, t.hour, t.minute, t.second + t.microsecond/1e6)
        err, r, _ = self.rec.sgp4(jd, fr)
        if err: return None
        x, y, z = r
        gmst = (280.46061837 + 360.98564736629*(jd + fr - 2451545.0)) % 360
        lon = (math.degrees(math.atan2(y, x)) - gmst + 540) % 360 - 180
        lat = math.degrees(math.atan2(z, math.hypot(x, y)))
        alt = math.sqrt(x*x+y*y+z*z) - RE
        return lat, lon, alt

def subsolar(t):
    d = (t - datetime(2000,1,1,12,tzinfo=timezone.utc)).total_seconds()/86400
    L = math.radians((280.46 + 0.9856474*d) % 360)
    g = math.radians((357.528 + 0.9856003*d) % 360)
    lam = L + math.radians(1.915)*math.sin(g) + math.radians(0.02)*math.sin(2*g)
    dec = math.degrees(math.asin(math.sin(math.radians(23.44))*math.sin(lam)))
    gmst = (280.46061837 + 360.98564736629*d) % 360
    ra = math.degrees(math.atan2(math.cos(math.radians(23.44))*math.sin(lam), math.cos(lam)))
    return dec, (ra - gmst + 540) % 360 - 180

def is_day(lat, lon, sun):
    sl, sn = map(math.radians, sun)
    cosz = (math.sin(math.radians(lat))*math.sin(sl) +
            math.cos(math.radians(lat))*math.cos(sl)*math.cos(math.radians(lon)-sn))
    return cosz > 0

# ---------- next-pass prediction ----------
def elevation_deg(obs_lat, obs_lon, sat_lat, sat_lon, sat_alt):
    """Topocentric elevation angle (deg) of the satellite as seen from an
    observer at obs_lat/obs_lon (assumed sea level), spherical-Earth
    approximation - consistent with Sat.latlon()'s own spherical lat/lon/alt
    (same RE, no WGS84 correction), so the two stay geometrically consistent
    with each other even though a real geodetic observer position would be
    marginally different."""
    olat, olon = math.radians(obs_lat), math.radians(obs_lon)
    slat, slon = math.radians(sat_lat), math.radians(sat_lon)
    obs = (RE*math.cos(olat)*math.cos(olon), RE*math.cos(olat)*math.sin(olon), RE*math.sin(olat))
    r = RE + sat_alt
    sat = (r*math.cos(slat)*math.cos(slon), r*math.cos(slat)*math.sin(slon), r*math.sin(slat))
    d = tuple(sat[i] - obs[i] for i in range(3))
    # topocentric East-North-Up frame at the observer
    up = (math.cos(olat)*math.cos(olon), math.cos(olat)*math.sin(olon), math.sin(olat))
    east = (-math.sin(olon), math.cos(olon), 0.0)
    north = (-math.sin(olat)*math.cos(olon), -math.sin(olat)*math.sin(olon), math.cos(olat))
    du = sum(d[i]*up[i] for i in range(3))
    de = sum(d[i]*east[i] for i in range(3))
    dn = sum(d[i]*north[i] for i in range(3))
    return math.degrees(math.atan2(du, math.hypot(de, dn)))

def find_next_pass(sat, obs_lat, obs_lon, start, min_elev=10.0, max_days=3, step_scale=100):
    """Searches forward from `start` for the next time the satellite rises
    above min_elev degrees as seen from obs_lat/obs_lon. Returns
    ('now', start) if it's already above min_elev; ('found', datetime) for
    the next rise; ('none', None) if no crossing turns up within max_days -
    which is the "not possible for some orbits" case (a GEO satellite
    parked over a different longitude, or an inclination that never reaches
    this latitude at all - if a pass is geometrically possible it recurs
    within a day or two for anything except a GEO/near-GEO object, so a
    multi-day bounded search is enough to tell "rare" from "impossible").
    Coarse step search only (no bisection refinement) - within a minute or
    so of accuracy, plenty for a "next pass in Xh Ym" display."""
    step_s = max(30, sat.period * 60 / step_scale)
    t = start
    end = start + timedelta(days=max_days)

    def elev_at(tt):
        p = sat.latlon(tt)
        return elevation_deg(obs_lat, obs_lon, *p) if p else -90.0

    prev = elev_at(t)
    if prev >= min_elev:
        return "now", t
    t += timedelta(seconds=step_s)
    while t <= end:
        e = elev_at(t)
        if e >= min_elev and prev < min_elev:
            return "found", t
        prev = e
        t += timedelta(seconds=step_s)
    return "none", None

def format_pass(state, when, now):
    if state == "now": return "overhead now"
    if state == "none": return "none in next 3d"
    delta = when - now
    days, rem = divmod(int(delta.total_seconds()), 86400)
    hours, rem = divmod(rem, 3600)
    mins = rem // 60
    if days: return f"in {days}d {hours}h"
    if hours: return f"in {hours}h {mins}m"
    return f"in {mins}m"

# ---------- projection / land helpers ----------
land_img = pygame.image.load(io.BytesIO(base64.b64decode(MASK_B64)))
def land_grid(w, h):
    """resample the mask to w x h booleans. Called once at EPaper.__init__
    with (s.W, s.MH) - the mask is generated at that exact native size (see
    tools/gen_land_mask.py), so this is a same-size no-op resize in
    practice, kept generic rather than hard-assuming the sizes always
    match."""
    small = pygame.transform.smoothscale(land_img.convert(24), (w, h))
    return [[small.get_at((x, y))[0] > 100 for x in range(w)] for y in range(h)]
def land_layer(land, w, h):
    """Pre-renders `land` (a land_grid() result) as a single RGBA surface -
    land cells opaque dark gray, sea cells fully transparent - so the map
    background is one surf.blit() per render() call instead of one
    pygame.draw.rect() per land cell. Matters now that the grid is the
    map's own native 800x400 resolution (up from the 360x180 this used to
    be, each cell then blown up into a ~2.2x2.2-pixel block to fill the
    same area - see the comment above MASK_B64 for why that changed):
    800x400 is ~5x the cell count 360x180 was, and a real per-cell draw
    call each render would needlessly repeat work land_grid() already
    settled once at startup. PixelArray, not surfarray, so this carries no
    numpy dependency (pip install pygame sgp4 stays the whole install)."""
    surf = pygame.Surface((w, h), pygame.SRCALPHA)
    pa = pygame.PixelArray(surf)
    for y in range(h):
        row = land[y]
        for x in range(w):
            if row[x]:
                pa[x, y] = (40, 40, 40, 255)
    del pa   # PixelArray locks the surface until released
    return surf
def proj(lat, lon, w, h):
    return (lon+180)/360*w, (90-lat)/180*h

def draw_rocket_icon(surf, x, y):
    """Easter egg for OBJECT_NAME == "SPECTRUM" (Isar Aerospace's Spectrum
    rocket, whose second stage flies a single Aquila engine) - mark it with
    a tiny rocket silhouette instead of the generic reticle. Same halo
    treatment as the reticle so it stays legible over map/track ink, and
    stays within the reticle's ~16px radius footprint so the two are
    interchangeable in the layout.

    v2: the first version (a squat box + two small triangles) read as a
    blob, not a rocket - this one is a slim tapered body with a sharp nose,
    a pair of splayed fins at the base, and a single engine nozzle, which
    is the actual recognizable silhouette."""
    pygame.draw.circle(surf, (250,250,250), (x, y), 16)
    pygame.draw.rect(surf, (0,0,0), (x-3, y-8, 6, 14))                      # slim body
    # Nose base is narrower than the body (4px vs 6px) and overlaps 1px into
    # it, rather than a same-width triangle exactly abutting the rect -
    # pygame.draw.polygon's flat-base rasterization is inconsistent by ~1px
    # right at a shared edge row (moved when the seam row did, so it's the
    # algorithm, not a coincidence - found by rendering at native size and
    # diffing rows). Keeping the triangle's base strictly narrower than the
    # rect guarantees any such rounding slop lands inside the already-black
    # body instead of poking out past its silhouette.
    pygame.draw.polygon(surf, (0,0,0), [(x-2,y-7), (x+2,y-7), (x,y-14)])    # sharp nose cone
    pygame.draw.polygon(surf, (0,0,0), [(x-3,y+2), (x-3,y+6), (x-7,y+8)])   # left fin, splayed out
    pygame.draw.polygon(surf, (0,0,0), [(x+3,y+2), (x+3,y+6), (x+7,y+8)])   # right fin, splayed out
    pygame.draw.polygon(surf, (0,0,0), [(x-2,y+6), (x+2,y+6), (x,y+11)])    # single engine nozzle, between the fins

# ---------- the e-paper display ----------
class EPaper:               # 7.5" 800x480 e-ink, full refresh every 5 min
    W, H, MH = 800, 480, 400   # MH = map height within the cached image
    def __init__(s):
        s.land = land_grid(s.W, s.MH); s.land_layer = land_layer(s.land, s.W, s.MH)
        s.last = None; s.cache = None
    def size(s): return (s.W+60, s.H+60)
    def draw(s, scr, sat, trail, now, sun, font):
        scr.fill((235, 232, 225))
        if s.last is None or (now - s.last).total_seconds() >= 300:
            s.cache = s.render(sat, now, sun); s.last = now
        scr.blit(s.cache, (30, 30))
        pygame.draw.rect(scr, (60,60,60), (30,30,s.W,s.H), 3)
        # Online/offline indicator + live clock: drawn straight to scr, *not*
        # into the cached e-ink surface above, so the clock ticks every frame
        # even though the panel content itself only redraws every 5 min
        # (that's the real e-paper constraint, not a bug). Two separate rows,
        # both right-aligned inside the frame - mirrors firmware's
        # src/display/epaper_render.cpp layout (kept in sync per CLAUDE.md's
        # "keep the demo and firmware renderers visually identical"
        # convention; firmware's own status text reads "Last refreshed"
        # there instead of "LIVE", since a real e-paper refresh is slow and
        # visibly flashes - it can't actually redraw every frame like this).
        # Pure black ink only (filled = online, outline = offline) - a real
        # e-ink panel has no green/red to spend on this; on hardware this dot
        # would be driven by a small status LED near the button instead.
        x1 = 30 + s.W - 20                         # right edge inside the frame

        label = "ONLINE" if sat.online else "OFFLINE"
        lw, lh = font.size(label)
        r, gap = 4, 8
        x0 = x1 - (2*r + gap + lw)
        ty = 30 + s.MH + 12
        cx, cy = x0+r, ty+lh//2
        pygame.draw.circle(scr, (0,0,0), (cx, cy), r, 0 if sat.online else 2)
        scr.blit(font.render(label, True, (0,0,0)), (x0+2*r+gap, ty))

        clock = font.render(now.strftime("LIVE %H:%M:%S UTC"), True, (0,0,0))
        scr.blit(clock, (x1-clock.get_width(), ty+28))

        # WiFi-connected signal, bottom-right corner of the frame - distinct
        # from the ONLINE/OFFLINE dot above (that one reflects whether the
        # last CelesTrak fetch succeeded, not whether the radio is
        # associated at all - the two can disagree, e.g. WiFi is fine but
        # CelesTrak itself is rate-limiting). The demo has no real radio to
        # check, so it's always drawn connected; firmware wires the same
        # icon to WiFi.status() == WL_CONNECTED.
        wifi_connected = True
        bars, bw, gap = 4, 5, 3
        base_y = 30 + s.H - 10
        for i in range(bars):
            bar_h = 5 + i*4
            bx = x1 - (bars - i) * (bw + gap)
            rect = (bx, base_y - bar_h, bw, bar_h)
            pygame.draw.rect(scr, (0,0,0), rect, 0 if wifi_connected else 1)
    def render(s, sat, now, sun):
        surf = pygame.Surface((s.W, s.H)); surf.fill((250, 250, 250))
        mw, mh = s.W, s.MH
        surf.blit(s.land_layer, (0, 0))
        # night side hatch
        for y in range(0, mh, 6):
            for x in range(0, mw, 6):
                lat, lon = 90-y/mh*180, x/mw*360-180
                if not is_day(lat, lon, sun): surf.set_at((x, y), (120,120,120))
        def land_at_px(x, y):
            """Looks up s.land (the mask, already built at __init__ at the
            map's own native mw x mh resolution) for the map pixel at
            (x, y) - a direct index now that the grid *is* map pixel space,
            no separate scaling needed."""
            mx = max(0, min(mw - 1, int(x))) % mw
            my = max(0, min(mh - 1, int(y)))
            return s.land[my][mx]
        def track(t0, t1, step, dashed):
            pts, t, k = [], t0, 0
            while t <= t1:
                p = sat.latlon(t)
                if p: pts.append(proj(p[0], p[1], mw, mh))
                t += timedelta(seconds=step); k += 1
            for a, b in zip(pts, pts[1:]):
                if abs(a[0]-b[0]) > mw/2: continue           # skip dateline wrap
                if dashed and (k := k+1) % 2: continue
                # White ink over land (drawn dark), black over sea (light
                # background) - a flat black track used to disappear into
                # the landmass fill wherever it crossed land. Colored per
                # segment (by its start point) rather than per pixel - segments
                # are short enough, and the mask coarse enough, that per-pixel
                # precision wouldn't look any different.
                color = (255,255,255) if land_at_px(*a) else (0,0,0)
                pygame.draw.line(surf, color, a, b, 3 if not dashed else 1)
        track(now - timedelta(minutes=sat.period), now, 30, True)
        track(now, now + timedelta(minutes=sat.period), 30, False)
        p = sat.latlon(now)
        if p:
            x, y = proj(p[0], p[1], mw, mh)
            if sat.name.strip().upper() == "SPECTRUM":
                draw_rocket_icon(surf, x, y)
            else:
                # "you are here" reticle: a quiet halo clears the map/track
                # ink right around the point so the dot doesn't blend in,
                # then a thin ring + crosshair ticks make it easy to spot
                # without covering much extra area (halo is unfilled apart
                # from a light fill).
                pygame.draw.circle(surf, (250,250,250), (x,y), 16)
                pygame.draw.circle(surf, (0,0,0), (x,y), 16, 2)
                for dx, dy in ((-1,0), (1,0), (0,-1), (0,1)):
                    pygame.draw.line(surf, (0,0,0), (x+dx*11,y+dy*11), (x+dx*16,y+dy*16), 2)
                pygame.draw.circle(surf, (0,0,0), (x,y), 9); pygame.draw.circle(surf, (250,250,250), (x,y), 5)
        pygame.draw.line(surf, (0,0,0), (0, mh), (mw, mh), 2)
        big = pygame.font.SysFont("dejavuserif", 30, bold=True); small = pygame.font.SysFont("dejavusans", 20)
        surf.blit(big.render(f"{sat.name}  ({sat.orbit_class})", True, (0,0,0)), (20, mh+12))
        info = (f"Apogee {sat.apogee:.0f} km    Perigee {sat.perigee:.0f} km    "
                f"Inclin. {sat.incl:.1f}°    Period {sat.period:.1f} min")
        age = sat.age(now)
        if age: info += f"    In space {age[0]:.0f} d ({age[1]:.1f} yr)"
        if sat.next_pass_text: info += f"    Next pass: {sat.next_pass_text}"
        surf.blit(small.render(info, True, (0,0,0)), (20, mh+50))
        return surf

# ---------- main ----------
def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    norad = args[0] if args else "100614"
    speed = 1.0
    if "--speed" in sys.argv: speed = float(sys.argv[sys.argv.index("--speed")+1])
    obs_lat = float(sys.argv[sys.argv.index("--lat")+1]) if "--lat" in sys.argv else None
    obs_lon = float(sys.argv[sys.argv.index("--lon")+1]) if "--lon" in sys.argv else None
    fields, online = fetch_omm(norad)
    # Look up by fields["NORAD_CAT_ID"], NOT the requested `norad` - when
    # fetch_omm() fails and falls back to FALLBACK_OMM (the ISS), the object
    # actually being displayed is the ISS, not whatever was requested. Using
    # `norad` here looked up (and showed) a *different* satellite's name/
    # launch-date next to the ISS's orbital data - e.g. "in space 8d" next to
    # the ISS's elements, because that was some other, freshly-launched
    # object's age, not the ISS's ~27 years.
    shown_norad = fields["NORAD_CAT_ID"]
    name = fetch_name(shown_norad, load_secrets().get("n2yo_api_key")) if online else None
    if name: fields["OBJECT_NAME"] = name
    sat = Sat(fields, fetch_launch_date(shown_norad), online)
    print(f"{sat.name}: apogee {sat.apogee:.0f} km, perigee {sat.perigee:.0f} km, period {sat.period:.1f} min")
    if not online: print("  (offline - showing stale fallback data)")
    age = sat.age(datetime.now(timezone.utc))
    if age: print(f"  in space {age[0]:.0f} days ({age[1]:.1f} years)")

    # Next pass, if a site location was given. Computed once here (not per
    # frame) - matches firmware's cadence (on satellite selection), and
    # avoids the simulated clock's --speed time-lapse racing past the
    # predicted time within seconds of runtime; a stale prediction during
    # fast-forward testing is an acceptable simplification for a dev tool
    # (real firmware has no artificial time acceleration to worry about).
    sat.next_pass_text = None
    if obs_lat is not None and obs_lon is not None:
        now0 = datetime.now(timezone.utc)
        state, when = find_next_pass(sat, obs_lat, obs_lon, now0)
        sat.next_pass_text = format_pass(state, when, now0)
        print(f"  next pass (>=10 deg elevation): {sat.next_pass_text}")

    pygame.init(); pygame.display.set_caption("Satellite Tracker Buddy - Demo")
    ep = EPaper()
    scr = pygame.display.set_mode(ep.size())
    font = pygame.font.SysFont("dejavusansmono", 16)
    clock = pygame.time.Clock()
    simt = datetime.now(timezone.utc); trail = []; last_sample = None

    while True:
        for e in pygame.event.get():
            if e.type == pygame.QUIT: return
            if e.type == pygame.KEYDOWN:
                if e.key == pygame.K_q: return
                if e.unicode in "+=": speed *= 2
                if e.unicode == "-": speed = max(1, speed/2)
                if e.key == pygame.K_SPACE: speed = 1; simt = datetime.now(timezone.utc)
        simt += timedelta(seconds=clock.get_time()/1000*speed)
        if last_sample is None or (simt-last_sample).total_seconds() >= 20:
            p = sat.latlon(simt)
            if p: trail.append((p[0], p[1])); trail = trail[-150:]
            last_sample = simt
        ep.draw(scr, sat, trail, simt, subsolar(simt), font)
        pygame.display.set_caption(f"Satellite Tracker Buddy - Demo  -  {speed:g}x")
        pygame.display.flip(); clock.tick(30)

if __name__ == "__main__":
    main()
