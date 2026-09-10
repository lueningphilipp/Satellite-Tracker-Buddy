#!/usr/bin/env python3
"""
Satellite display demo - simulates the three proposed builds on your PC.

    pip install pygame sgp4

    python sat_display_demo.py            # ISS, LED-matrix mode
    python sat_display_demo.py 25544      # any NORAD id
    python sat_display_demo.py 25544 --speed 60   # 60x time-lapse

Keys:  1 = 16x16 LED map frame   2 = 64x32 HUB75 panel   3 = 7.5" e-paper
       +/- = time speed          space = real time        q = quit
"""
import sys, io, json, math, base64, time, urllib.request
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

NAME_OVERRIDES = {  # CelesTrak lists these under a generic placeholder name
                     # ("OBJECT F" etc.) until the operator-reported name gets
                     # folded in. Hardcode the real ones here for now, until
                     # we pull from another API that already resolves them.
    "100614": "SPECTRUM",
}

def fetch_omm(norad):
    """Fetch orbital elements as OMM/CSV rather than legacy TLE text: catalog
    numbers >=100000 (newly launched objects) don't fit the TLE format's
    fixed 5-digit satellite-number field, so CelesTrak 404s FORMAT=tle/3le
    for them while CSV/JSON still work fine."""
    url = f"https://celestrak.org/NORAD/elements/gp.php?CATNR={norad}&FORMAT=csv"
    try:
        txt = urllib.request.urlopen(url, timeout=10).read().decode()
        rows = list(omm.parse_csv(io.StringIO(txt)))
        if not rows:
            raise ValueError("No GP data found")
        fields = rows[0]
        if str(norad) in NAME_OVERRIDES:
            fields["OBJECT_NAME"] = NAME_OVERRIDES[str(norad)]
        return fields
    except Exception as e:
        print("Orbital element fetch failed, using fallback:", e)
        return dict(FALLBACK_OMM)

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

# ---------- orbit maths ----------
MU, RE = 398600.4418, 6371.0
class Sat:
    def __init__(self, fields, launch_date=None):
        self.name = fields["OBJECT_NAME"].strip()
        self.launch_date = launch_date
        self.rec = Satrec()
        omm.initialize(self.rec, fields)
        n = float(fields["MEAN_MOTION"]) * 2*math.pi/86400   # rad/s
        e = float(fields["ECCENTRICITY"])
        a = (MU / n**2) ** (1/3)
        self.apogee, self.perigee = a*(1+e)-RE, a*(1-e)-RE
        self.incl, self.period = float(fields["INCLINATION"]), 2*math.pi/n/60

    def age(self, now):
        """Days/years in space as of `now`, or None if launch date is unknown."""
        if not self.launch_date: return None
        days = (now - self.launch_date).total_seconds() / 86400
        return days, days / 365.25

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

# ---------- projection / land helpers ----------
land_img = pygame.image.load(io.BytesIO(base64.b64decode(MASK_B64)))
def land_grid(w, h):
    """downsample the 360x180 mask to w x h booleans"""
    small = pygame.transform.smoothscale(land_img.convert(24), (w, h))
    return [[small.get_at((x, y))[0] > 100 for x in range(w)] for y in range(h)]
def proj(lat, lon, w, h):
    return (lon+180)/360*w, (90-lat)/180*h

# ---------- the three simulated displays ----------
class LedFrame:            # 1: 16x16 WS2812 behind a printed map, plus OLED
    W, H, PIX = 16, 16, 34
    def __init__(s): s.land = land_grid(s.W, s.H)
    def size(s): return (s.W*s.PIX + 40, s.H*s.PIX + 140)
    def draw(s, scr, sat, trail, now, sun, font):
        scr.fill((25, 22, 20)); ox, oy = 20, 20
        buf = [[[0,0,0] for _ in range(s.W)] for _ in range(s.H)]
        def add(x, y, c, k, blend=False):
            x, y = int(x) % s.W, int(y)
            if 0 <= y < s.H:
                for i in range(3):
                    v = c[i]*k
                    buf[y][x][i] = min(255, buf[y][x][i] + v) if blend else max(buf[y][x][i], v)
        n = len(trail)
        for i, (la, lo) in enumerate(trail):
            px, py = proj(la, lo, s.W, s.H); add(px, py, (60, 120, 255), (i+1)/n*0.8)
        if trail:
            la, lo = trail[-1]; px, py = proj(la, lo, s.W, s.H)
            fx, fy = px - int(px), py - int(py)          # sub-pixel blend over 2x2
            for dx in (0,1):
                for dy in (0,1):
                    w = (fx if dx else 1-fx)*(fy if dy else 1-fy)
                    add(int(px)+dx, int(py)+dy, (255,255,255), w, blend=True)
        for y in range(s.H):
            for x in range(s.W):
                r = pygame.Rect(ox+x*s.PIX, oy+y*s.PIX, s.PIX-2, s.PIX-2)
                # printed map layer: land dim warm, ocean shows LED glow
                base = (70, 62, 50) if s.land[y][x] else (30, 40, 55)
                c = [min(255, int(base[i]*0.6 + buf[y][x][i])) for i in range(3)]
                pygame.draw.rect(scr, c, r, border_radius=6)
        # OLED
        o = pygame.Rect(ox, oy+s.H*s.PIX+12, s.W*s.PIX, 90)
        pygame.draw.rect(scr, (0,0,0), o); pygame.draw.rect(scr, (90,90,90), o, 2)
        age = sat.age(now)
        age_line = f"IN SPACE {age[0]:.0f}d ({age[1]:.1f}y)" if age else "IN SPACE unknown"
        for i, t in enumerate([sat.name[:16], f"APO {sat.apogee:5.0f} km  PERI {sat.perigee:5.0f} km",
                               now.strftime("%H:%M:%S UTC"), age_line]):
            scr.blit(font.render(t, True, (120, 200, 255)), (o.x+8, o.y+6+i*20))

class Hub75:               # 2: 64x32 RGB matrix, bottom 8 rows text
    W, H, PIX = 64, 32, 12
    def __init__(s): s.land = land_grid(s.W, s.H-8)
    def size(s): return (s.W*s.PIX + 20, s.H*s.PIX + 20)
    def draw(s, scr, sat, trail, now, sun, font):
        scr.fill((15,15,15)); ox = oy = 10
        buf = [[(0,0,0) for _ in range(s.W)] for _ in range(s.H)]
        for y in range(s.H-8):
            for x in range(s.W):
                lat, lon = 90-(y+.5)/(s.H-8)*180, (x+.5)/s.W*360-180
                day = is_day(lat, lon, sun)
                if s.land[y][x]: buf[y][x] = (0,90,30) if day else (0,25,8)
                else:            buf[y][x] = (0,20,60) if day else (0,4,14)
        n = len(trail)
        for i,(la,lo) in enumerate(trail):
            px, py = proj(la, lo, s.W, s.H-8); k = (i+1)/n
            buf[int(py)][int(px)%s.W] = (int(255*k), int(120*k), 0)
        if trail:
            la, lo = trail[-1]; px, py = proj(la, lo, s.W, s.H-8)
            buf[int(py)][int(px)%s.W] = (255,255,255)
        # text rows as bitmap font: render small then downscale to pixel grid
        txt = pygame.Surface((s.W, 8)); txt.fill((0,0,0))
        f = pygame.font.SysFont("dejavusansmono", 8)
        txt.blit(f.render(f"A{sat.apogee:4.0f} P{sat.perigee:4.0f}", False, (255,200,0)), (0,0))
        for y in range(8):
            for x in range(s.W):
                if txt.get_at((x,y))[0] > 80: buf[s.H-8+y][x] = (255,200,0)
        for y in range(s.H):
            for x in range(s.W):
                pygame.draw.circle(scr, buf[y][x], (ox+x*s.PIX+s.PIX//2, oy+y*s.PIX+s.PIX//2), s.PIX//2-1)

class EPaper:              # 3: 7.5" 800x480 e-ink, full refresh every 5 min
    W, H = 800, 480
    def __init__(s):
        s.land = land_grid(360, 180); s.last = None; s.cache = None
    def size(s): return (s.W+60, s.H+60)
    def draw(s, scr, sat, trail, now, sun, font):
        scr.fill((235, 232, 225))
        if s.last is None or (now - s.last).total_seconds() >= 300:
            s.cache = s.render(sat, now, sun); s.last = now
        scr.blit(s.cache, (30, 30))
        pygame.draw.rect(scr, (60,60,60), (30,30,s.W,s.H), 3)
    def render(s, sat, now, sun):
        surf = pygame.Surface((s.W, s.H)); surf.fill((250, 250, 250))
        mw, mh = s.W, 400; sx, sy = mw/360, mh/180
        for y in range(180):
            for x in range(360):
                if s.land[y][x]: pygame.draw.rect(surf, (40,40,40), (x*sx, y*sy, sx+1, sy+1))
        # night side hatch
        for y in range(0, mh, 6):
            for x in range(0, mw, 6):
                lat, lon = 90-y/mh*180, x/mw*360-180
                if not is_day(lat, lon, sun): surf.set_at((x, y), (120,120,120))
        def track(t0, t1, step, dashed):
            pts, t, k = [], t0, 0
            while t <= t1:
                p = sat.latlon(t)
                if p: pts.append(proj(p[0], p[1], mw, mh))
                t += timedelta(seconds=step); k += 1
            for a, b in zip(pts, pts[1:]):
                if abs(a[0]-b[0]) > mw/2: continue           # skip dateline wrap
                if dashed and (k := k+1) % 2: continue
                pygame.draw.line(surf, (0,0,0), a, b, 3 if not dashed else 1)
        track(now - timedelta(minutes=sat.period), now, 30, True)
        track(now, now + timedelta(minutes=sat.period), 30, False)
        p = sat.latlon(now)
        if p:
            x, y = proj(p[0], p[1], mw, mh)
            # "you are here" reticle: a quiet halo clears the map/track ink
            # right around the point so the dot doesn't blend in, then a thin
            # ring + crosshair ticks make it easy to spot without covering
            # much extra area (halo is unfilled apart from a light fill).
            pygame.draw.circle(surf, (250,250,250), (x,y), 16)
            pygame.draw.circle(surf, (0,0,0), (x,y), 16, 2)
            for dx, dy in ((-1,0), (1,0), (0,-1), (0,1)):
                pygame.draw.line(surf, (0,0,0), (x+dx*11,y+dy*11), (x+dx*16,y+dy*16), 2)
            pygame.draw.circle(surf, (0,0,0), (x,y), 9); pygame.draw.circle(surf, (250,250,250), (x,y), 5)
        pygame.draw.line(surf, (0,0,0), (0, mh), (mw, mh), 2)
        big = pygame.font.SysFont("dejavuserif", 30, bold=True); small = pygame.font.SysFont("dejavusans", 20)
        surf.blit(big.render(sat.name, True, (0,0,0)), (20, mh+12))
        info = (f"Apogee {sat.apogee:.0f} km    Perigee {sat.perigee:.0f} km    "
                f"Incl {sat.incl:.1f}°    Period {sat.period:.1f} min")
        age = sat.age(now)
        if age: info += f"    In space {age[0]:.0f} d ({age[1]:.1f} yr)"
        surf.blit(small.render(info, True, (0,0,0)), (20, mh+50))
        surf.blit(small.render(now.strftime("Updated %Y-%m-%d %H:%M UTC"), True, (90,90,90)), (mw-330, mh+12))
        return surf

# ---------- main ----------
def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    norad = args[0] if args else "100614"
    speed = 1.0
    if "--speed" in sys.argv: speed = float(sys.argv[sys.argv.index("--speed")+1])
    sat = Sat(fetch_omm(norad), fetch_launch_date(norad))
    print(f"{sat.name}: apogee {sat.apogee:.0f} km, perigee {sat.perigee:.0f} km, period {sat.period:.1f} min")
    age = sat.age(datetime.now(timezone.utc))
    if age: print(f"  in space {age[0]:.0f} days ({age[1]:.1f} years)")

    pygame.init(); pygame.display.set_caption("Satellite display demo")
    displays = {1: LedFrame(), 2: Hub75(), 3: EPaper()}; mode = 1
    scr = pygame.display.set_mode(displays[mode].size())
    font = pygame.font.SysFont("dejavusansmono", 16)
    clock = pygame.time.Clock()
    simt = datetime.now(timezone.utc); trail = []; last_sample = None

    while True:
        for e in pygame.event.get():
            if e.type == pygame.QUIT: return
            if e.type == pygame.KEYDOWN:
                if e.key == pygame.K_q: return
                if e.unicode in "123":
                    mode = int(e.unicode); scr = pygame.display.set_mode(displays[mode].size())
                if e.unicode in "+=": speed *= 2
                if e.unicode == "-": speed = max(1, speed/2)
                if e.key == pygame.K_SPACE: speed = 1; simt = datetime.now(timezone.utc)
        simt += timedelta(seconds=clock.get_time()/1000*speed)
        if last_sample is None or (simt-last_sample).total_seconds() >= 20:
            p = sat.latlon(simt)
            if p: trail.append((p[0], p[1])); trail = trail[-150:]
            last_sample = simt
        displays[mode].draw(scr, sat, trail, simt, subsolar(simt), font)
        pygame.display.set_caption(f"Satellite display demo  -  mode {mode}  -  {speed:g}x")
        pygame.display.flip(); clock.tick(30)

if __name__ == "__main__":
    main()
