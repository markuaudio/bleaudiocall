#pragma once
#include "esp_err.h"

// Supply guard: the LyraT has no 5 V sense pin, but the REC button line
// (GPIO36 / SENSOR_VP) is pulled up to the 3.3 V rail through 10 kΩ and the
// ESP32 ADC uses an internal reference. Reading it therefore tracks the 3.3 V
// rail: when the amplifiers pull the 5 V input down, the LDO drops out and the
// reading falls *before* the IMP809 supervisor (2.63 V) resets the chip.
// Pressing REC shorts the line to ground, which looks like a deep sag: handy
// for testing the ducking without a weak supply.
esp_err_t supply_init(void);
int supply_read_raw(void); // averaged 12-bit raw ADC count, -1 if unavailable
