#pragma once
#include <stdint.h>

// SPI pins for the Waveshare "e-Paper ESP32 Driver Board" - fixed on its PCB,
// not something we chose. Verified two ways: (1) GxEPD2's own
// GxEPD2_wiring_examples.h has a dedicated block for this exact board with
// these pins; (2) Waveshare's own e-Paper_ESP32_Driver_Board user manual PDF
// lists the identical `#define PIN_SPI_*` block. No MISO - the panel is
// write-only over SPI, matches the driver board manual not listing one.
static const int8_t EPD_PIN_SCK  = 13;
static const int8_t EPD_PIN_DIN  = 14;   // MOSI
static const int8_t EPD_PIN_CS   = 15;
static const int8_t EPD_PIN_BUSY = 25;
static const int8_t EPD_PIN_RST  = 26;
static const int8_t EPD_PIN_DC   = 27;
