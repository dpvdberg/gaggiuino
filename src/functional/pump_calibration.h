#ifndef PUMP_CALIBRATION_H
#define PUMP_CALIBRATION_H

/**
 * Pump zero calibration.
 *
 * The pump model gives ml per stroke as (pumpZero - pressureTerms(p)) * multiplier, where the
 * multiplier depends on the mains frequency. pumpZero enters linearly, so after measuring the real
 * ml per stroke at (near) zero pressure, the new value follows directly from the model's current
 * estimate at the same average pressure.
 */
namespace pumpCalibration {

  // Prime until the cup gained this much: well above what pump vibration does to the reading.
  constexpr float PRIME_GRAMS = 15.f;
  constexpr unsigned long PRIME_CONFIRM_MS = 500;
  constexpr unsigned long PRIME_TIMEOUT_MS = 25000;
  constexpr unsigned long SETTLE_MS = 3000;
  constexpr unsigned long MEASURE_MS = 12000;
  constexpr float MEASURE_MAX_GRAMS = 150.f;

  // Same limits the EEPROM validation uses.
  constexpr float MIN_PUMP_ZERO = 0.210f;
  constexpr float MAX_PUMP_ZERO = 0.310f;

  /**
   * oldPumpZero:      the value currently in use
   * grams, strokes:   water collected and pump strokes counted while measuring
   * modelPerStroke:   getPumpFlowPerClick(averagePressure) with the old value (ml per stroke)
   * multiplier:       getPumpClickMultiplier()
   * Returns the new pump zero, or a negative value if the measurement can't be used.
   */
  inline float newPumpZero(float oldPumpZero, float grams, long strokes, float modelPerStroke, float multiplier) {
    if (strokes <= 0 || grams <= 0.f || multiplier <= 0.f) return -1.f;
    float measuredPerStroke = grams / static_cast<float>(strokes);
    return oldPumpZero + (measuredPerStroke - modelPerStroke) / multiplier;
  }

  inline bool inRange(float pumpZero) {
    return pumpZero >= MIN_PUMP_ZERO && pumpZero <= MAX_PUMP_ZERO;
  }
}

#endif
