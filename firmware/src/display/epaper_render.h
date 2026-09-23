#pragma once
#include <Arduino.h>
#include "../core/sgp4_track.h"
#include "../core/elements.h"
#include "../core/pass_predict.h"
#include "trail_buffer.h"

// One-time GxEPD2 + SPI init. Call once from setup().
void epaperInit();

// "No WiFi yet" instruction screen, shown while the captive portal is open
// (first boot, or after the BOOT-button WiFi reset, or a stored network that
// can't be joined). Numbered steps on the left, a scan-to-join-the-AP QR on
// the right. apSsid/apIp are the portal's own values (see
// WiFiSetup::runCaptivePortal) so the text can never drift from what the
// device actually opened. Firmware-only - the PC demo has no WiFi to mirror.
// failNote (optional): replaces the subtitle when a stored network could not
// be joined, e.g. `Could not join "Home" (wrong password?).`.
void epaperRenderSetup(const char* apSsid, const String& apIp, const String& failNote);

// Full-refresh render: map + land + night shading + past(dashed)/future(solid)
// track + "you are here" reticle + name/orbit-class title + apogee/perigee/
// incl/period/time-in-space info row + refresh-time clock/online status row.
// Mirrors demo/sat_display_demo.py's EPaper.render()/draw(), see CLAUDE.md's
// "Keep the demo and firmware renderers visually identical" convention.
// launchDate/haveLaunchDate: time-in-space is hidden if haveLaunchDate is
// false (launch-date fetch failed), matching the demo's `if age:` pattern.
// wifiBars: drives the WiFi signal-bars icon (bottom-right corner), 0-4 -
// separate from `online`, which reflects whether the last CelesTrak fetch
// succeeded, not whether the radio is associated at all or how strong the
// link is (these can all disagree - e.g. WiFi weak but CelesTrak itself
// rate-limiting). 0 means not connected (all bars drawn outline-only, no
// QR - see below); 1-4 is bucketed from RSSI dBm by wifiRssiToBars()
// (core/wifi_setup.h), bars below the count filled, the rest outline-only.
// configUrl: the live config-page address (e.g. "http://192.168.1.42/"),
// drawn as a small QR code just left of the WiFi icon - see CLAUDE.md's
// TODO for the on-device scan test that picked its size (25x25 physical
// pixels, 1px/module - much smaller than the module-count math alone
// suggested, but that's what a real phone camera actually resolved).
// haveNextPassInfo/passState/passTime: next-pass-overhead prediction (see
// core/pass_predict.h) - the whole "Next pass" column is hidden if
// haveNextPassInfo is false (no site lat/lon configured), matching the
// existing haveLaunchDate pattern for the "In space" column.
void epaperRender(Sgp4Track& track, const OrbitalElements& el,
                   const TrailBuffer& trail, time_t now, bool online,
                   time_t launchDate, bool haveLaunchDate, int wifiBars,
                   const String& configUrl,
                   bool haveNextPassInfo, PassState passState, time_t passTime);
