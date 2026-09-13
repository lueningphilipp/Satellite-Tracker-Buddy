#pragma once
#include <Arduino.h>
#include "../core/sgp4_track.h"
#include "../core/elements.h"
#include "trail_buffer.h"

// One-time GxEPD2 + SPI init. Call once from setup().
void epaperInit();

// Full-refresh render: map + land + night shading + past(dashed)/future(solid)
// track + "you are here" reticle + name/orbit-class title + apogee/perigee/
// incl/period/time-in-space info row + refresh-time clock/online status row.
// Mirrors demo/sat_display_demo.py's EPaper.render()/draw(), see CLAUDE.md's
// "Keep the demo and firmware renderers visually identical" convention.
// launchDate/haveLaunchDate: time-in-space is hidden if haveLaunchDate is
// false (launch-date fetch failed), matching the demo's `if age:` pattern.
// wifiConnected: drives the WiFi signal-bars icon (bottom-right corner) -
// separate from `online`, which reflects whether the last CelesTrak fetch
// succeeded, not whether the radio is associated at all (the two can
// disagree - e.g. WiFi fine but CelesTrak itself rate-limiting).
// configUrl: the live config-page address (e.g. "http://192.168.1.42/"),
// drawn as a small QR code just left of the WiFi icon - see CLAUDE.md's
// TODO for the on-device scan test that picked its size (25x25 physical
// pixels, 1px/module - much smaller than the module-count math alone
// suggested, but that's what a real phone camera actually resolved).
void epaperRender(Sgp4Track& track, const OrbitalElements& el,
                   const TrailBuffer& trail, time_t now, bool online,
                   time_t launchDate, bool haveLaunchDate, bool wifiConnected,
                   const String& configUrl);
