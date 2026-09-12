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

        // Online/offline indicator + "Last refreshed": stacked together in
        // the top-right corner, sharing the title's row band (small built-in
        // GFX font, not the 9pt custom one) - frees up the rest of the text
        // area for the orbit-numbers grid below with more breathing room
        // under the title. "Last refreshed" rather than "LIVE" (unlike the
        // pygame demo) because a real e-paper refresh is a slow (~2.6s),
        // visibly-flashing full-panel update - it can only ever be as fresh
        // as the last refresh, never actually live.
        display.setFont(nullptr);
        display.setTextSize(1);
        int16_t bx, by;
        uint16_t bw, bh;

        const char* label = online ? "ONLINE" : "OFFLINE";
        display.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
        int r = 4, gap = 8;
        int indicatorTy = MH + 8;   // top of text (built-in font convention)
        int x0 = (W - 20) - (2 * r + gap + (int)bw);
        int cx = x0 + r, cy = indicatorTy + bh / 2;
        if (online) display.fillCircle(cx, cy, r, GxEPD_BLACK);
        else        display.drawCircle(cx, cy, r, GxEPD_BLACK);
        display.setCursor(x0 + 2 * r + gap, indicatorTy);
        display.print(label);

        char clockStr[32];
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(clockStr, sizeof(clockStr), "Last refreshed %02d:%02d:%02d UTC",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        display.getTextBounds(clockStr, 0, 0, &bx, &by, &bw, &bh);
        int clockTy = MH + 18;
        display.setCursor((W - 20) - (int)bw, clockTy);
        display.print(clockStr);

        // Orbit numbers as a 3-column table (apogee/perigee, incl/period,
        // in-space-days/years each stacked two rows): label left-aligned at
        // the column's start, value right-aligned to the column's end - so
        // labels read naturally left-to-right while numbers still line up
        // on the right, like a spreadsheet. Column 1 starts flush with the
        // satellite name above it. Pushed further down from the title than
        // before, for more visual separation.
        display.setFont(&FreeSans9pt7b);
        auto printLabelValue = [&](int labelX, int valueRightEdge, int y,
                                    const char* lbl, const char* val) {
            display.setCursor(labelX, y);
            display.print(lbl);
            int16_t tbx, tby; uint16_t tbw, tbh;
            display.getTextBounds(val, 0, 0, &tbx, &tby, &tbw, &tbh);
            display.setCursor(valueRightEdge - (int)tbw, y);
            display.print(val);
        };
        auto printRightAligned = [&](int rightEdge, int y, const char* text) {
            int16_t tbx, tby; uint16_t tbw, tbh;
            display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
            display.setCursor(rightEdge - (int)tbw, y);
            display.print(text);
        };
        // Narrower columns than before (was 180px wide, leaving a big gap
        // between a short label like "Apogee" and its right-aligned value) -
        // tightened by about one tab-width per column.
        const int col1X = 20, col1Right = 150;
        const int col2X = 170, col2Right = 300;
        const int col3X = 320, col3Right = 450;
        const int row1Y = MH + 52, row2Y = MH + 72;

        char valBuf[24];
        snprintf(valBuf, sizeof(valBuf), "%.0f km", track.apogeeKm());
        printLabelValue(col1X, col1Right, row1Y, "Apogee", valBuf);
        snprintf(valBuf, sizeof(valBuf), "%.0f km", track.perigeeKm());
        printLabelValue(col1X, col1Right, row2Y, "Perigee", valBuf);

        snprintf(valBuf, sizeof(valBuf), "%.1f deg", el.inclinationDeg);
        printLabelValue(col2X, col2Right, row1Y, "Incl", valBuf);
        snprintf(valBuf, sizeof(valBuf), "%.1f min", track.periodMin());
        printLabelValue(col2X, col2Right, row2Y, "Period", valBuf);

        if (haveLaunchDate) {
            double days = difftime(now, launchDate) / 86400.0;
            snprintf(valBuf, sizeof(valBuf), "%.0f d", days);
            printLabelValue(col3X, col3Right, row1Y, "In space", valBuf);
            snprintf(valBuf, sizeof(valBuf), "(%.1f yr)", days / 365.25);
            printRightAligned(col3Right, row2Y, valBuf);   // continuation, no label
        }
    } while (display.nextPage());
    display.hibernate();
}
