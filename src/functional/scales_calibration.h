#ifndef SCALES_CALIBRATION_H
#define SCALES_CALIBRATION_H

#include <math.h>

/**
 * Calibration for the two load cells under the drip tray.
 *
 * The scales report grams = raw1 / factor1 + raw2 / factor2. A weight shares its load
 * between both cells depending on where it sits, so each weighing gives one equation
 * with two unknowns. Weighing known weights in different spots (near one end, near the
 * other end, in the middle) and fitting a least-squares solution gives both factors.
 */
namespace scalesCalibration {

  struct Sample {
    float raw1;   // tared raw reading of cell 1
    float raw2;   // tared raw reading of cell 2
    float grams;  // known weight on the tray
  };

  struct Result {
    bool  ok = false;
    float factor1 = 0.f;
    float factor2 = 0.f;
    float maxErrorGrams = 0.f;  // worst difference between a known weight and what the new factors read
  };

  inline Result solve(const Sample* samples, int count) {
    Result result;
    double s11 = 0, s12 = 0, s22 = 0, t1 = 0, t2 = 0;
    for (int i = 0; i < count; i++) {
      double r1 = samples[i].raw1, r2 = samples[i].raw2, w = samples[i].grams;
      s11 += r1 * r1;
      s12 += r1 * r2;
      s22 += r2 * r2;
      t1 += r1 * w;
      t2 += r2 * w;
    }
    double det = s11 * s22 - s12 * s12;
    // Too similar placements (or a dead cell) leave the two factors undetermined.
    if (count < 2 || s11 <= 0 || s22 <= 0 || det <= 1e-3 * s11 * s22) return result;

    double a = (t1 * s22 - t2 * s12) / det;  // 1 / factor1
    double b = (s11 * t2 - s12 * t1) / det;  // 1 / factor2
    if (fabs(a) < 1e-9 || fabs(b) < 1e-9) return result;

    double maxError = 0;
    for (int i = 0; i < count; i++) {
      double error = fabs(a * static_cast<double>(samples[i].raw1) + b * static_cast<double>(samples[i].raw2)
                          - static_cast<double>(samples[i].grams));
      if (error > maxError) maxError = error;
    }

    result.ok = true;
    result.factor1 = static_cast<float>(1.0 / a);
    result.factor2 = static_cast<float>(1.0 / b);
    result.maxErrorGrams = static_cast<float>(maxError);
    return result;
  }

  inline float grams(float raw1, float raw2, float factor1, float factor2) {
    float g = 0.f;
    if (factor1 != 0.f) g += raw1 / factor1;
    if (factor2 != 0.f) g += raw2 / factor2;
    return g;
  }
}

#endif
