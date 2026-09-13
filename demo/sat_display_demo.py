#!/usr/bin/env python3
"""
Satellite display demo - PC simulator for the e-paper build (see CLAUDE.md;
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

# ---------- world land mask, 360x180 equirectangular, embedded PNG ----------
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
    """downsample the 360x180 mask to w x h booleans"""
    small = pygame.transform.smoothscale(land_img.convert(24), (w, h))
    return [[small.get_at((x, y))[0] > 100 for x in range(w)] for y in range(h)]
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
        s.land = land_grid(360, 180); s.last = None; s.cache = None
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
        mw, mh = s.W, s.MH; sx, sy = mw/360, mh/180
        for y in range(180):
            for x in range(360):
                if s.land[y][x]: pygame.draw.rect(surf, (40,40,40), (x*sx, y*sy, sx+1, sy+1))
        # night side hatch
        for y in range(0, mh, 6):
            for x in range(0, mw, 6):
                lat, lon = 90-y/mh*180, x/mw*360-180
                if not is_day(lat, lon, sun): surf.set_at((x, y), (120,120,120))
        def land_at_px(x, y):
            """Looks up s.land (the 360x180 mask, already built at __init__)
            for the map pixel at (x, y) in the mw x mh map area - reuses the
            same grid drawMap() itself paints from, just indexed back from
            pixel space instead of degrees."""
            mx = int(x / mw * 360) % 360
            my = max(0, min(179, int(y / mh * 180)))
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

    pygame.init(); pygame.display.set_caption("Satellite display demo")
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
        pygame.display.set_caption(f"Satellite display demo  -  {speed:g}x")
        pygame.display.flip(); clock.tick(30)

if __name__ == "__main__":
    main()
