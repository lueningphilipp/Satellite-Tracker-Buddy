#include "epaper_render.h"
#include "epaper_pins.h"
#include "astro.h"
#include "land_mask.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <qrcode.h>   // ricmoo/QRCode - draws the "scan to open config page" QR in epaperRender()
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

// LAND_MASK is generated at the map area's own native size (see
// tools/gen_land_mask.py and the comment above MASK_B64 in the demo for why
// - it used to be a separate 360x180 "1 pixel/degree" grid, upscaled into
// ~2.2x2.2-pixel blocks to fill this same area, which is what made the
// coastlines look blocky on real hardware). Caught here at compile time
// rather than as a silent visual regression if the mask is ever
// regenerated at a different size than this file expects.
static_assert(LAND_MASK_W == W && LAND_MASK_H == MH,
              "LAND_MASK size doesn't match the map area (W x MH) - "
              "regenerate it: python tools/gen_land_mask.py --write-demo "
              "&& python tools/mask_to_progmem.py");

static void proj(double lat, double lon, int w, int h, int& x, int& y) {
    x = (int)((lon + 180.0) / 360.0 * w);
    y = (int)((90.0 - lat) / 180.0 * h);
}

static void drawMap(double sunDec, double sunLon) {
    // One call, not a 320,000-iteration app-level loop: LAND_MASK is
    // already packed in exactly the format Adafruit_GFX::drawBitmap()
    // expects (row-padded-to-byte, MSB-first, PROGMEM - see
    // tools/mask_to_progmem.py), and unset bits are left transparent, so
    // this paints land black and leaves sea as whatever fillScreen() set
    // (white) - same effect the old per-pixel landAt()+fillRect() loop
    // had, just as a single well-optimized library call.
    display.drawBitmap(0, 0, LAND_MASK, LAND_MASK_W, LAND_MASK_H, GxEPD_BLACK);
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

// Looks up landAt() for a map pixel in W x MH space - mirrors the demo's
// land_at_px(). Direct index, no scaling: the static_assert above
// guarantees LAND_MASK is already at native W x MH resolution.
static bool landAtPx(int x, int y) {
    int mx = ((x % W) + W) % W;   // wrap longitude
    int my = y < 0 ? 0 : (y >= MH ? MH - 1 : y);
    return landAt(mx, my);
}

static void drawTrackSegment(int x0, int y0, int x1, int y1, bool thick) {
    if (abs(x1 - x0) > W / 2) return;   // skip dateline wrap, matches demo
    // White ink over land (drawn black), black over sea (white background) -
    // ported from the demo's track(): a flat black track used to disappear
    // into the landmass fill wherever it crossed land. Colored by the
    // segment's start point, same as the demo - segments are short enough,
    // and the mask coarse enough, that per-pixel precision wouldn't look
    // any different.
    uint16_t color = landAtPx(x0, y0) ? GxEPD_WHITE : GxEPD_BLACK;
    display.drawLine(x0, y0, x1, y1, color);
    if (thick) {   // approximate the demo's 3px solid future line
        display.drawLine(x0, y0 + 1, x1, y1 + 1, color);
        display.drawLine(x0, y0 - 1, x1, y1 - 1, color);
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

// Easter egg for OBJECT_NAME == "SPECTRUM" (Isar Aerospace's Spectrum
// rocket, whose second stage flies a single Aquila engine) - mark it with a
// tiny rocket silhouette instead of the generic reticle. Ported as-is from
// the demo's draw_rocket_icon() v2 (slim tapered body, sharp nose, splayed
// fins, single engine nozzle - the original squat-box version read as a
// blob, not a rocket).
static void drawRocketIcon(int x, int y) {
    display.fillCircle(x, y, 16, GxEPD_WHITE);
    display.fillRect(x - 3, y - 8, 6, 14, GxEPD_BLACK);                           // slim body
    display.fillTriangle(x - 2, y - 7, x + 2, y - 7, x, y - 14, GxEPD_BLACK);     // sharp nose cone
    display.fillTriangle(x - 3, y + 2, x - 3, y + 6, x - 7, y + 8, GxEPD_BLACK);  // left fin, splayed out
    display.fillTriangle(x + 3, y + 2, x + 3, y + 6, x + 7, y + 8, GxEPD_BLACK);  // right fin, splayed out
    display.fillTriangle(x - 2, y + 6, x + 2, y + 6, x, y + 11, GxEPD_BLACK);     // single engine nozzle, between the fins
}

void epaperRenderSetup(const char* apSsid, const String& apIp) {
    // WiFi-join QR ("WIFI:" payload, understood by iOS/Android camera apps).
    // The portal AP is open, hence T:nopass. Version 3 (29x29 modules) at
    // 4px/module: version 2's 32-byte capacity is too small for the
    // ~45-byte payload, and this screen has the room to draw it big.
    String wifiPayload = String("WIFI:T:nopass;S:") + apSsid + ";;";
    const uint8_t qrVersion = 3;
    const int qrPxPerModule = 4;
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];
    qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_LOW, wifiPayload.c_str());
    int codeSize = qrPxPerModule * qrcode.size;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        display.setFont(&FreeSansBold18pt7b);
        display.setCursor(40, 65);
        display.print("WiFi setup needed");
        display.setFont(&FreeSans12pt7b);
        display.setCursor(40, 105);
        display.print("No WiFi connected yet - set it up in 4 steps:");
        display.drawFastHLine(40, 125, W - 80, GxEPD_BLACK);
        display.drawFastHLine(40, 126, W - 80, GxEPD_BLACK);

        // Steps: number in bold, text in regular, continuation lines
        // indented under the text. Line breaks are hand-picked to stay left
        // of the QR column (x >= ~590) at 12pt.
        const int numX = 40, textX = 75, lineH = 30, stepGap = 22;
        int y = 175;
        auto step = [&](const char* num, const char* l1, const char* l2,
                        bool l2Bold) {
            display.setFont(&FreeSansBold12pt7b);
            display.setCursor(numX, y);
            display.print(num);
            display.setFont(&FreeSans12pt7b);
            display.setCursor(textX, y);
            display.print(l1);
            if (l2) {
                y += lineH;
                display.setFont(l2Bold ? &FreeSansBold12pt7b : &FreeSans12pt7b);
                display.setCursor(textX, y);
                display.print(l2);
            }
            y += lineH + stepGap;
        };
        step("1.", "On your phone or PC, join the WiFi network:", apSsid, true);
        // Step 2's second line mixes regular + bold (the URL), which the
        // helper can't express, so it's drawn by hand.
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(numX, y);
        display.print("2.");
        display.setFont(&FreeSans12pt7b);
        display.setCursor(textX, y);
        display.print("A setup page opens by itself. If it doesn't,");
        y += lineH;
        display.setCursor(textX, y);
        display.print("open ");
        display.setFont(&FreeSansBold12pt7b);
        display.print("http://");
        display.print(apIp);
        display.print("/");
        y += lineH + stepGap;
        step("3.", "Pick your home WiFi, enter its password", "and tap Save.", false);
        step("4.", "The tracker restarts and shows the satellite.", nullptr, false);

        // QR column, right side, vertically centred on the steps area.
        int qrX = W - 40 - codeSize, qrY = 175;
        for (uint8_t my = 0; my < qrcode.size; my++) {
            for (uint8_t mx = 0; mx < qrcode.size; mx++) {
                if (qrcode_getModule(&qrcode, mx, my)) {
                    display.fillRect(qrX + mx * qrPxPerModule, qrY + my * qrPxPerModule,
                                      qrPxPerModule, qrPxPerModule, GxEPD_BLACK);
                }
            }
        }
        display.setFont(&FreeSans9pt7b);
        const char* cap1 = "Scan to join";
        const char* cap2 = "the setup WiFi";
        int16_t cbx, cby; uint16_t cbw, cbh;
        display.getTextBounds(cap1, 0, 0, &cbx, &cby, &cbw, &cbh);
        display.setCursor(qrX + (codeSize - (int)cbw) / 2, qrY + codeSize + 24);
        display.print(cap1);
        display.getTextBounds(cap2, 0, 0, &cbx, &cby, &cbw, &cbh);
        display.setCursor(qrX + (codeSize - (int)cbw) / 2, qrY + codeSize + 46);
        display.print(cap2);
    } while (display.nextPage());
}

void epaperRender(Sgp4Track& track, const OrbitalElements& el,
                   const TrailBuffer& trail, time_t now, bool online,
                   time_t launchDate, bool haveLaunchDate, int wifiBars,
                   const String& configUrl,
                   bool haveNextPassInfo, PassState passState, time_t passTime) {
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
            String nameUpper = el.name;
            nameUpper.toUpperCase();
            if (nameUpper == "SPECTRUM") {
                drawRocketIcon(x, y);
            } else {
                drawReticle(x, y);
            }
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
        // MH+18 (a 10px gap under the indicator row above) read as cramped
        // on the real panel - MH+22 gives a clearer 14px gap instead.
        int clockTy = MH + 22;
        display.setCursor((W - 20) - (int)bw, clockTy);
        display.print(clockStr);

        // WiFi signal-strength bars, bottom-right corner of the whole canvas -
        // separate from the ONLINE/OFFLINE dot above (see epaperRender()'s
        // header comment for why they can disagree). Same shape as the demo:
        // bars below wifiBars filled, the rest outline-only.
        const int wifiBarCount = 4, wifiBarW = 5, wifiBarGap = 3;
        const int wifiBaseX = W - 20, wifiBaseY = H - 10;
        {
            for (int i = 0; i < wifiBarCount; i++) {
                int barH = 5 + i * 4;
                int bx2 = wifiBaseX - (wifiBarCount - i) * (wifiBarW + wifiBarGap);
                if (i < wifiBars) display.fillRect(bx2, wifiBaseY - barH, wifiBarW, barH, GxEPD_BLACK);
                else              display.drawRect(bx2, wifiBaseY - barH, wifiBarW, barH, GxEPD_BLACK);
            }
        }

        // Small "scan to open the config page" QR code, just left of the
        // WiFi icon, sharing its baseline. Only shown while actually
        // connected - configUrl is the STA IP, which is unreachable anyway
        // if the radio is down, so a QR to nowhere would be misleading.
        //
        // Size/placement were picked via an on-device scan test (see
        // CLAUDE.md's TODO): a set of candidate sizes was rendered and
        // physically scanned with a phone. The smallest one - 25x25
        // physical pixels, i.e. 1 device pixel per QR module - was the one
        // that worked, well below what naive module-count math would
        // suggest was needed; real phone cameras handle a lot more than
        // that math assumes. Version 2 (25x25 modules) because version 1's
        // byte-mode capacity (17 bytes at LOW ECC) is too small for a full
        // "http://<ip>/" string (~22 bytes); version 2's 32 bytes fits.
        if (wifiBars > 0) {
            const uint8_t qrVersion = 2;
            const int qrPxPerModule = 1;
            QRCode qrcode;
            uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];
            qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_LOW, configUrl.c_str());

            int codeSize = qrPxPerModule * qrcode.size;
            int wifiIconLeft = wifiBaseX - wifiBarCount * (wifiBarW + wifiBarGap);
            int qrRight = wifiIconLeft - 10;   // gap between the QR and the WiFi icon
            int x0 = qrRight - codeSize, y0 = wifiBaseY - codeSize;   // shares the WiFi icon's baseline

            for (uint8_t my = 0; my < qrcode.size; my++) {
                for (uint8_t mx = 0; mx < qrcode.size; mx++) {
                    if (qrcode_getModule(&qrcode, mx, my)) {
                        display.fillRect(x0 + mx * qrPxPerModule, y0 + my * qrPxPerModule,
                                          qrPxPerModule, qrPxPerModule, GxEPD_BLACK);
                    }
                }
            }
        }

        // Orbit numbers as a 3-column table (apogee/perigee, incl/period,
        // in-space-days/years each stacked two rows): label left-aligned at
        // the column's start, value right-aligned to the column's end - so
        // labels read naturally left-to-right while numbers still line up
        // on the right, like a spreadsheet. Column 1 starts flush with the
        // satellite name above it. Pushed further down from the title than
        // before, for more visual separation.
        display.setFont(&FreeSans9pt7b);
        // Labels in the bold weight of the same 9pt face, values in the
        // regular weight - makes "Apogee"/"Perigee"/etc read as labels at a
        // glance instead of blending into the numbers next to them.
        auto printLabelValue = [&](int labelX, int valueRightEdge, int y,
                                    const char* lbl, const char* val) {
            display.setFont(&FreeSansBold9pt7b);
            display.setCursor(labelX, y);
            display.print(lbl);
            int16_t lbx, lby; uint16_t lbw, lbh;
            display.getTextBounds(lbl, 0, 0, &lbx, &lby, &lbw, &lbh);
            int labelRight = labelX + (int)lbw;

            display.setFont(&FreeSans9pt7b);
            int16_t tbx, tby; uint16_t tbw, tbh;
            display.getTextBounds(val, 0, 0, &tbx, &tby, &tbw, &tbh);
            // Right-align to the column edge, but never closer to the label
            // than minGap - a plain right-align let a 5-digit value (e.g.
            // "12345 d" for time-in-space, or a GEO's "35786 km"/"1436.0
            // min") butt straight up against, or overlap, its own label.
            const int minGap = 14;
            int valueX = valueRightEdge - (int)tbw;
            if (valueX < labelRight + minGap) valueX = labelRight + minGap;
            display.setCursor(valueX, y);
            display.print(val);
        };
        auto printRightAligned = [&](int rightEdge, int y, const char* text) {
            int16_t tbx, tby; uint16_t tbw, tbh;
            display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
            display.setCursor(rightEdge - (int)tbw, y);
            display.print(text);
        };
        // Widened from 130px columns (20-150/170-300/320-450) - a 5-digit
        // time-in-space value ("12345 d") or a GEO's wide numbers ("35786
        // km", "1436.0 min") were butting against/overlapping their own
        // label at that width. All three columns are the same 150px width -
        // an earlier version made column 3 much wider than the others to
        // "solve" this, but that just moved the problem: a short, common
        // value ("8 d") right-aligned inside an oversized column left a
        // huge, inconsistent gap after its label, compared to columns
        // 1/2's tighter, matching gaps (seen on the real panel). Equal
        // widths plus the minGap floor in printLabelValue() above handle
        // both the common short-value case and the rare wide-value case
        // without that inconsistency.
        const int col1X = 20, col1Right = 170;
        const int col2X = 190, col2Right = 340;
        const int col3X = 360, col3Right = 510;
        const int row1Y = MH + 52, row2Y = MH + 72;

        char valBuf[24];
        snprintf(valBuf, sizeof(valBuf), "%.0f km", track.apogeeKm());
        printLabelValue(col1X, col1Right, row1Y, "Apogee", valBuf);
        snprintf(valBuf, sizeof(valBuf), "%.0f km", track.perigeeKm());
        printLabelValue(col1X, col1Right, row2Y, "Perigee", valBuf);

        snprintf(valBuf, sizeof(valBuf), "%.1f deg", el.inclinationDeg);
        printLabelValue(col2X, col2Right, row1Y, "Inclin.", valBuf);
        snprintf(valBuf, sizeof(valBuf), "%.1f min", track.periodMin());
        printLabelValue(col2X, col2Right, row2Y, "Period", valBuf);

        if (haveLaunchDate) {
            double days = difftime(now, launchDate) / 86400.0;
            // Whole elapsed days (floored), not rounded to nearest - %.0f
            // rounded up a full day early, at the halfway mark instead of a
            // full day boundary. Confirmed as a real discrepancy: NORAD
            // 100614 launched 2026-09-05, and on 2026-09-13 (8.53 exact
            // days later) this showed "9 d" instead of the expected "8 d".
            // Ported from the demo's Sat.age(), fixed there first.
            long wholeDays = (long)floor(days);
            snprintf(valBuf, sizeof(valBuf), "%ld d", wholeDays);
            printLabelValue(col3X, col3Right, row1Y, "In space", valBuf);
            snprintf(valBuf, sizeof(valBuf), "(%.1f yr)", days / 365.25);
            printRightAligned(col3Right, row2Y, valBuf);   // continuation, no label
        }

        // Next pass above the configured site (core/pass_predict.h) - a 4th
        // column in the space between "In space" and the WiFi/QR corner.
        // Hidden entirely if no site lat/lon is configured, same pattern as
        // "In space" above. kNone means the search found no crossing within
        // its window - either genuinely rare, or the orbit's inclination
        // never reaches this latitude at all (e.g. a low-inclination orbit
        // seen from a high-latitude site) or (for a GEO/near-GEO object) the
        // site is simply outside its fixed footprint - "not possible for
        // some orbits", as expected.
        if (haveNextPassInfo) {
            const int col4X = 530, col4Right = 690;
            if (passState == PassState::kNow) {
                printLabelValue(col4X, col4Right, row1Y, "Next pass", "Now");
            } else if (passState == PassState::kNone) {
                printLabelValue(col4X, col4Right, row1Y, "Next pass", "None");
            } else {   // kFound
                long deltaSec = (long)difftime(passTime, now);
                if (deltaSec < 0) deltaSec = 0;   // clock skew guard, shouldn't happen
                long days = deltaSec / 86400, hours = (deltaSec % 86400) / 3600, mins = (deltaSec % 3600) / 60;
                char countdown[16];
                if (days > 0) snprintf(countdown, sizeof(countdown), "%ldd %ldh", days, hours);
                else if (hours > 0) snprintf(countdown, sizeof(countdown), "%ldh %ldm", hours, mins);
                else snprintf(countdown, sizeof(countdown), "%ldm", mins);
                printLabelValue(col4X, col4Right, row1Y, "Next pass", countdown);

                struct tm passTmv;
                gmtime_r(&passTime, &passTmv);
                char clockBuf[16];
                snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d UTC", passTmv.tm_hour, passTmv.tm_min);
                printRightAligned(col4Right, row2Y, clockBuf);   // continuation, no label
            }
        }
    } while (display.nextPage());
    display.hibernate();
}
