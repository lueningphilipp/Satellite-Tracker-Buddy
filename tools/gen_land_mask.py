#!/usr/bin/env python3
"""
Regenerates the equirectangular land/sea mask embedded in
demo/sat_display_demo.py's MASK_B64, from Natural Earth's public-domain
land-polygon dataset (https://www.naturalearthdata.com/ - "No permission is
needed to use Natural Earth... Crediting the authors is unnecessary."). See
the comment above MASK_B64 in the demo for the full history of why this
exists and why the resolution/source below were chosen.

Default output is 800x400 - the map area's own native pixel size (see
CLAUDE.md/EPaper.W,MH), not the 360x180 "1 pixel per degree" size this
started at. At 360x180, each mask pixel was drawn as a ~2.2x2.2
physical-pixel block on the real panel - visibly blocky on real hardware,
and the actual bottleneck behind an earlier attempt to fix coastline detail
by switching source-dataset resolution alone (see the demo's comment). At
native 800x400 that upscaling step disappears entirely.

No third-party geometry library needed - point-in-polygon fill is done by
rasterizing each polygon (exterior ring white, hole rings black) onto a
supersampled canvas with PIL's ImageDraw, then box-downsampling to the
final resolution for antialiased coastlines. This mirrors the threshold
convention the rest of the project already uses (R channel > 100 == land -
see the demo's land_grid() and tools/mask_to_progmem.py).

    python tools/gen_land_mask.py                    # preview only, writes
                                                       # nothing (prints
                                                       # where a PNG would go)
    python tools/gen_land_mask.py --write-demo        # regenerates the mask
                                                       # and patches MASK_B64
                                                       # in sat_display_demo.py
                                                       # in place
    python tools/gen_land_mask.py --out-dir DIR       # also/instead dumps
                                                       # mask_<W>x<H>.png +
                                                       # mask_b64.txt to DIR
    python tools/gen_land_mask.py --width 360 --height 180   # override size

After --write-demo, also regenerate firmware's copy:
    python tools/mask_to_progmem.py
"""
import argparse
import base64
import io
import json
import pathlib
import re
import urllib.request

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEMO = ROOT / "demo" / "sat_display_demo.py"

# Natural Earth ships this same public-domain land-polygon data at three
# simplification levels (1:110m/50m/10m - the "m" is a cartographic scale
# denominator, not meters). At the old 360x180 output size, 1:110m visibly
# lost small islands/inlets (e.g. the Lesser Antilles) that 1:50m recovered,
# but 1:50m vs 1:10m made almost no difference - the 360x180 grid itself was
# the bottleneck past 1:50m, not the source data. Re-checked at this file's
# current 800x400 native target, where that bottleneck is gone: 1:50m vs
# 1:10m stays visually near-identical even there - checked with zoomed
# crops over the Caribbean, Iberia and the Aegean - so 1:50m remains the
# pick: ~6x smaller/faster to fetch for no visible loss at this mask size
# either.
NE_URL = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_land.geojson"
W, H = 800, 400
SUPERSAMPLE = 4  # -> 3200x1600 working canvas, exact integer box-downsample to WxH


def lonlat_to_px(lon, lat, sw, sh):
    x = (lon + 180.0) / 360.0 * sw
    y = (90.0 - lat) / 180.0 * sh
    return (x, y)


def polygons_from_geometry(geom):
    """Yields (exterior_ring, [hole_rings]) tuples of (lon, lat) point lists."""
    t = geom["type"]
    if t == "Polygon":
        coords = geom["coordinates"]
        yield coords[0], coords[1:]
    elif t == "MultiPolygon":
        for poly in geom["coordinates"]:
            yield poly[0], poly[1:]
    else:
        raise ValueError(f"unexpected geometry type {t}")


def build_mask(source_url, w, h, supersample):
    print(f"Fetching {source_url} ...")
    with urllib.request.urlopen(source_url, timeout=30) as resp:
        geojson = json.load(resp)
    features = geojson["features"]
    print(f"  {len(features)} land features")

    sw, sh = w * supersample, h * supersample
    canvas = Image.new("L", (sw, sh), 0)   # 0 = sea
    draw = ImageDraw.Draw(canvas)

    ring_count = 0
    for feat in features:
        for exterior, holes in polygons_from_geometry(feat["geometry"]):
            pts = [lonlat_to_px(lon, lat, sw, sh) for lon, lat in exterior]
            if len(pts) >= 3:
                draw.polygon(pts, fill=255)
                ring_count += 1
            for hole in holes:
                hpts = [lonlat_to_px(lon, lat, sw, sh) for lon, lat in hole]
                if len(hpts) >= 3:
                    draw.polygon(hpts, fill=0)
                    ring_count += 1
    print(f"  rasterized {ring_count} rings onto a {sw}x{sh} canvas")

    # Exact integer box-average downsample - matches supersample exactly, so
    # each output pixel is a true average of an NxN block (antialiased
    # coastlines) rather than a resampling-filter approximation.
    mask = canvas.resize((w, h), Image.BOX)

    land_px = sum(1 for v in mask.getdata() if v > 100)
    print(f"  {land_px}/{w*h} pixels ({100*land_px/(w*h):.1f}%) classify as land at >100 threshold")
    return mask


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source-url", default=NE_URL)
    ap.add_argument("--width", type=int, default=W)
    ap.add_argument("--height", type=int, default=H)
    ap.add_argument("--supersample", type=int, default=SUPERSAMPLE)
    ap.add_argument("--out-dir", help="also write mask_<W>x<H>.png + mask_b64.txt here")
    ap.add_argument("--write-demo", action="store_true",
                     help="patch MASK_B64 in demo/sat_display_demo.py in place")
    args = ap.parse_args()

    mask = build_mask(args.source_url, args.width, args.height, args.supersample)

    buf = io.BytesIO()
    mask.save(buf, format="PNG", optimize=True)
    png_bytes = buf.getvalue()
    b64 = base64.b64encode(png_bytes).decode()

    if args.out_dir:
        out_dir = pathlib.Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        png_name = f"mask_{args.width}x{args.height}.png"
        (out_dir / png_name).write_bytes(png_bytes)
        (out_dir / "mask_b64.txt").write_text(b64)
        print(f"Wrote {out_dir / png_name} and {out_dir / 'mask_b64.txt'} "
              f"({len(png_bytes)} PNG bytes, {len(b64)} base64 chars)")

    if args.write_demo:
        src = DEMO.read_text()
        pattern = re.compile(r'MASK_B64\s*=\s*"([^"]+)"')
        m = pattern.search(src)
        if not m:
            raise SystemExit(f"Couldn't find MASK_B64 in {DEMO}")
        old_len = len(m.group(1))
        src = pattern.sub(f'MASK_B64 = "{b64}"', src, count=1)
        DEMO.write_text(src)
        print(f"Patched {DEMO}: MASK_B64 {old_len} -> {len(b64)} base64 chars")
        print("Now regenerate firmware's copy: python tools/mask_to_progmem.py")

    if not args.out_dir and not args.write_demo:
        print("(dry run - pass --write-demo and/or --out-dir DIR to actually write something)")


if __name__ == "__main__":
    main()
