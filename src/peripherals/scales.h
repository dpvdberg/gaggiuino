/* 09:32 15/03/2023 - change triggering comment */#ifndef SCALES_H
#define SCALES_H

// Needs to be here so it pleases so cppcheck is pleased.
#ifndef FORCE_PREDICTIVE_SCALES
#define FORCE_PREDICTIVE_SCALES 0
#endif

#include "measurements.h"

void scalesInit(float scalesF1, float scalesF2);
// Returns false if the tare could not be done (hardware scales not ready).
bool scalesTare(void);
// Returns a measurement with millis == 0 when no reading was available.
Measurement scalesGetWeight(void);
bool scalesIsPresent(void);
float scalesDripTrayWeight();

// Calibration helpers (built-in load cells only)
bool scalesHardwarePresent(void);
// Tared raw reading per load cell, averaged over `times` reads. False if the cells aren't ready.
bool scalesReadCells(float raw[2], uint8_t times);
void scalesSetFactors(float factor1, float factor2);

#endif
