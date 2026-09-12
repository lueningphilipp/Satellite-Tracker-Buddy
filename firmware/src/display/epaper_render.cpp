#include "epaper_render.h"
#include "epaper_pins.h"
#include "astro.h"
#include "land_mask.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <math.h>
#include <time.h>
#include <stdio.h>

// Same GxEPD2_750_T7 pin wiring verified for the hello-world test. One
// display instance for the whole firmware.
static GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(
    GxEPD2_750_T7(EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY));

// Matches demo's EPaper.W/H/MH (map height within the 800x480 canvas; the
// remaining 80px at the bottom is the text area).
static const int W = 800, H = 480, MH = 400;

void epaperInit() {
    SPI.begin(EPD_PIN_SCK, /*miso=*/-1, EPD_PIN_DIN, EPD_PIN_CS);
    display.init(115200, true, 2, false);
    // rotation(1) (copied from a generic example for the hello-world test,
    // never actually verified) came out portrait 480x800 - Adafruit_GFX
    // rotation steps 90 deg CW per unit, and GxEPD2_750_T7's native size IS
    // 800x480 landscape already, so rotation 0 (no rotation) is correct.
    display.setRotation(0);
}

static void proj(double lat, double lon, int w, int h, int& x, int& y) {
    x = (int)((lon + 180.0) / 360.0 * w);
    y = (int)((90.0 - lat) / 180.0 * h);
}

static void drawMap(double sunDec, double sunLon) {
    double sx = (double)W / LAND_MASK_W, sy = (double)MH / LAND_MASK_H;
    for (int y = 0; y < LAND_MASK_H; y++) {
        for (int x = 0; x < LAND_MASK_W; x++) {
            if (landAt(x, y))
                display.fillRect((int)(x * sx), (int)(y * sy), (int)sx + 1, (int)sy + 1, GxEPD_BLACK);
        }
    }
    // Night-side hatch: the demo uses soft gray dots; a true B/W panel has
    // no gray, so these become sparse black dots instead - same stippled
    // effect, just higher contrast.
    for (int y = 0; y < MH; y += 6) {
        for (int x = 0; x < W; x += 6) {
            double lat = 90.0 - (double)y / MH * 180.0;
            double lon = (double)x / W * 360.0 - 180.0;
            if (!isDay(lat, lon, sunDec, sunLon)) display.drawPixel(x, y, GxEPD_BLACK);
        }
    }
}

static void drawTrackSegment(int x0, int y0, int x1, int y1, bool thick) {
    if (abs(x1 - x0) > W / 2) return;   // skip dateline wrap, matches demo
    display.drawLine(x0, y0, x1, y1, GxEPD_BLACK);
    if (thick) {   // approximate the demo's 3px solid future line
        display.drawLine(x0, y0 + 1, x1, y1 + 1, GxEPD_BLACK);
        display.drawLine(x0, y0 - 1, x1, y1 - 1, GxEPD_BLACK);
    }
}

// Past track: drawn from the persisted trail ring buffer (real samples
// collected over time), not re-propagated - see CLAUDE.md's TODO and
// trail_buffer.h. Dashed by skipping every other segment, like the demo.
static void drawPastTrack(const TrailBuffer& trail) {
    int n = trail.count();
    if (n < 2) return;
    double plat, plon;
    trail.get(0, plat, plon);
    int px, py;
    proj(plat, plon, W, MH, px, py);
    for (int i = 1; i < n; i++) {
        double lat, lon;
        trail.get(i, lat, lon);
        int x, y;
        proj(lat, lon, W, MH, x, y);
        if (i % 2 == 0) drawTrackSegment(px, py, x, y, false);
        px = x; py = y;
    }
}

// Future track: fresh, bounded propagation (not persisted - no need, it's
// cheap: N=60 SGP4 calls once per 5-minute refresh, not per second).
static void drawFutureTrack(Sgp4Track& track, time_t now, double periodMin) {
    const int N = 60;
    double stepSec = (periodMin * 60.0) / N;
    int px = -1, py = -1;
    for (int i = 0; i <= N; i++) {
        SatPosition p = track.positionAt(now + (time_t)(i * stepSec));
        if (!p.valid) continue;
        int x, y;
        proj(p.lat, p.lon, W, MH, x, y);
        if (px >= 0) drawTrackSegment(px, py, x, y, true);
        px = x; py = y;
    }
}

// "You are here" reticle - same design as the demo's: a white halo clears
// underlying ink, a thin ring + crosshair ticks make it easy to spot, a
// black-ring/white-center dot pinpoints the exact position.
static void drawReticle(int x, int y) {
    display.fillCircle(x, y, 16, GxEPD_WHITE);
    display.drawCircle(x, y, 16, GxEPD_BLACK);
    static const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
    for (int i = 0; i < 4; i++)
        display.drawLine(x + dx[i] * 11, y + dy[i] * 11, x + dx[i] * 16, y + dy[i] * 16, GxEPD_BLACK);
    display.fillCircle(x, y, 9, GxEPD_BLACK);
    display.fillCircle(x, y, 5, GxEPD_WHITE);
}

void epaperRender(Sgp4Track& track, const OrbitalElements& el,
                   const TrailBuffer& trail, time_t now, bool online,
                   time_t launchDate, bool haveLaunchDate) {
    double sunDec, sunLon;
    subsolar(now, sunDec, sunLon);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawMap(sunDec, sunLon);
        drawPastTrack(trail);
        drawFutureTrack(track, now, track.periodMin());

        SatPosition here = track.positionAt(now);
        if (here.valid) {
            int x, y;
            proj(here.lat, here.lon, W, MH, x, y);
            drawReticle(x, y);
        }
        display.drawFastHLine(0, MH, W, GxEPD_BLACK);
        display.drawFastHLine(0, MH + 1, W, GxEPD_BLACK);

        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(20, MH + 26);
        display.print(el.name);
        display.print("  (");
        display.print(track.orbitClass());
        display.print(")");

        // Online/offline indicator: shares the title's row, right-aligned,
        // directly under the map border. Small built-in GFX font (not the
        // 9pt custom one) and a small dot.
        display.setFont(nullptr);
        display.setTextSize(1);
        const char* label = online ? "ONLINE" : "OFFLINE";
        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
        int r = 4, gap = 8;
        int indicatorTy = MH + 8;   // top of text (built-in font convention)
        int x0 = (W - 20) - (2 * r + gap + (int)bw);
        int cx = x0 + r, cy = indicatorTy + bh / 2;
        if (online) display.fillCircle(cx, cy, r, GxEPD_BLACK);
        else        display.drawCircle(cx, cy, r, GxEPD_BLACK);
        display.setCursor(x0 + 2 * r + gap, indicatorTy);
        display.print(label);

        // Orbit numbers as a 3-column grid (apogee/perigee, incl/period,
        // in-space-days/years each stacked two rows) instead of one long
        // line - the single-line version wrapped/overflowed the 800px
        // width. Left-aligned per column (labels read naturally left-to-
        // right, flush with each column's left edge) - right-aligning the
        // combined "Label Value" strings made the labels themselves start
        // at uneven x positions, since "Apogee"/"Perigee" etc. differ in
        // length. Column 1 starts flush with the satellite name above it.
        display.setFont(&FreeSans9pt7b);
        const int col1X = 20, col2X = 220, col3X = 420;
        const int row1Y = MH + 44, row2Y = MH + 62;

        display.setCursor(col1X, row1Y);
        display.printf("Apogee %.0f km", track.apogeeKm());
        display.setCursor(col1X, row2Y);
        display.printf("Perigee %.0f km", track.perigeeKm());

        display.setCursor(col2X, row1Y);
        display.printf("Incl %.1f deg", el.inclinationDeg);
        display.setCursor(col2X, row2Y);
        display.printf("Period %.1f min", track.periodMin());

        // "Last refreshed" rather than "LIVE" (unlike the pygame demo)
        // because a real e-paper refresh is a slow (~2.6s), visibly-flashing
        // full-panel update - it can only ever be as fresh as the last
        // refresh, never actually live.
        if (haveLaunchDate) {
            double days = difftime(now, launchDate) / 86400.0;
            display.setCursor(col3X, row1Y);
            display.printf("In space %.0f d", days);
            display.setCursor(col3X, row2Y);
            display.printf("(%.1f yr)", days / 365.25);
        }

        display.setFont(nullptr);
        display.setTextSize(1);
        char clockStr[32];
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(clockStr, sizeof(clockStr), "Last refreshed %02d:%02d:%02d UTC",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        display.getTextBounds(clockStr, 0, 0, &bx, &by, &bw, &bh);
        int clockTy = MH + 70;   // nudged down to clear row2Y (62) now that it moved
        display.setCursor((W - 20) - (int)bw, clockTy);
        display.print(clockStr);
    } while (display.nextPage());
    display.hibernate();
}
