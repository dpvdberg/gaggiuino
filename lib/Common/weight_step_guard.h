#ifndef WEIGHT_STEP_GUARD_H
#define WEIGHT_STEP_GUARD_H

#include <stdint.h>
#include <math.h>

/**
 * Keeps sudden steps in the weight out of the shot weight.
 * Coffee can't arrive faster than a few g/s, so a step faster than that
 * (cup placed or bumped, a tare that didn't happen) shifts the zero point
 * instead of counting as coffee. Feed it already median-filtered values,
 * so single spikes never reach it.
 */
class WeightStepGuard {
public:
  static constexpr float MAX_RATE = 25.f; // g/s, well above any real espresso flow
  static constexpr float MIN_STEP = 3.f;  // g, steps below this are always accepted

private:
  float offset = 0.f;
  float last = 0.f;
  uint32_t lastMillis = 0;
  bool primed = false;

public:
  /** Start from a known weight, e.g. 0 right after a tare. */
  void reset(float value, uint32_t millis) {
    offset = 0.f;
    last = value;
    lastMillis = millis;
    primed = true;
  }

  /** Returns the weight with sudden steps removed. Sets `absorbed` when a step was taken out. */
  float apply(float value, uint32_t millis, bool* absorbed = nullptr) {
    if (absorbed) *absorbed = false;
    if (!primed) {
      reset(value, millis);
      return value;
    }

    float step = value - last;
    float elapsedSec = (millis - lastMillis) / 1000.f;
    if (fabsf(step) > fmaxf(MIN_STEP, MAX_RATE * elapsedSec)) {
      offset += step;
      if (absorbed) *absorbed = true;
    }

    last = value;
    lastMillis = millis;
    return value - offset;
  }
};

#endif
