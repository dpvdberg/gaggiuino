#ifndef MEDIAN_FILTER_H
#define MEDIAN_FILTER_H

#include <stdint.h>

/**
 * Median of the last 3 values.
 * A single outlier is ignored, while a real step change is followed
 * after 2 readings. Unlike rejecting jumps, it can never get stuck.
 */
class MedianFilter3 {
private:
  float values[3] = { 0.f, 0.f, 0.f };
  uint8_t next = 0;
  bool primed = false;

  static float median(float a, float b, float c) {
    float lo = a < b ? a : b;
    float hi = a < b ? b : a;
    // median = max(lo, min(hi, c))
    float m = hi < c ? hi : c;
    return lo > m ? lo : m;
  }

public:
  /** Forget all values; the next value fills the window. */
  void reset() { primed = false; next = 0; }

  /** Fill the window with a known value (e.g. 0 right after a tare). */
  void reset(float value) {
    values[0] = values[1] = values[2] = value;
    next = 0;
    primed = true;
  }

  /** Add a value and return the filtered result. */
  float add(float value) {
    if (!primed) {
      reset(value);
      return value;
    }
    values[next] = value;
    next = (next + 1) % 3;
    return median(values[0], values[1], values[2]);
  }
};

#endif
