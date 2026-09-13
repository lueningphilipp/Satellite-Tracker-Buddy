#pragma once
#include <Arduino.h>

// Snapshot of the last network operations' outcomes - lets the config page
// show connection problems (e.g. CelesTrak's gp.php returning HTTP 403, see
// CLAUDE.md's TODO) without needing the serial monitor. Populated by
// main.cpp's refetchAndInit() (each fetch function fills in its own field
// via an optional String* out-param - see core/elements.h); read-only from
// web_server.cpp.
struct ConnectionStatus {
    String elementsStatus = "not fetched yet";
    String launchDateStatus = "not fetched yet";
    String n2yoStatus = "no key configured";
};

extern ConnectionStatus connStatus;
