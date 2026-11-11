#include <unity.h>
#include "../src/functional/scales_calibration.h"

namespace {
  // Raw readings for `grams` placed so that `shareCell1` of the load sits on cell 1.
  scalesCalibration::Sample place(float grams, float shareCell1, float f1, float f2, float noise1 = 0.f, float noise2 = 0.f) {
    return { grams * shareCell1 * f1 + noise1, grams * (1.f - shareCell1) * f2 + noise2, grams };
  }
}

void test_calibration_recovers_both_factors(void) {
  const float f1 = 3920.f, f2 = 4210.f;
  scalesCalibration::Sample samples[3] = {
    place(100.f, 0.80f, f1, f2),  // X near one end
    place(100.f, 0.15f, f1, f2),  // X near the other end
    place(250.f, 0.50f, f1, f2),  // Y in the middle
  };
  scalesCalibration::Result result = scalesCalibration::solve(samples, 3);
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_FLOAT_WITHIN(1.f, f1, result.factor1);
  TEST_ASSERT_FLOAT_WITHIN(1.f, f2, result.factor2);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, result.maxErrorGrams);
}

void test_calibration_handles_negative_factor_and_noise(void) {
  const float f1 = -4000.f, f2 = 4100.f;  // a cell wired the other way round
  scalesCalibration::Sample samples[3] = {
    place(100.f, 0.70f, f1, f2, 150.f, -80.f),
    place(100.f, 0.25f, f1, f2, -120.f, 60.f),
    place(200.f, 0.55f, f1, f2, 90.f, 110.f),
  };
  scalesCalibration::Result result = scalesCalibration::solve(samples, 3);
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_FLOAT_WITHIN(40.f, f1, result.factor1);
  TEST_ASSERT_FLOAT_WITHIN(40.f, f2, result.factor2);
  TEST_ASSERT_TRUE(result.maxErrorGrams < 0.5f);
  // New factors read the middle weight correctly
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.f,
    scalesCalibration::grams(samples[2].raw1, samples[2].raw2, result.factor1, result.factor2));
}

void test_calibration_rejects_same_placement(void) {
  const float f1 = 4000.f, f2 = 4000.f;
  scalesCalibration::Sample samples[3] = {
    place(100.f, 0.5f, f1, f2),
    place(100.f, 0.5f, f1, f2),
    place(200.f, 0.5f, f1, f2),
  };
  TEST_ASSERT_FALSE(scalesCalibration::solve(samples, 3).ok);
}

void test_calibration_rejects_dead_cell(void) {
  scalesCalibration::Sample samples[3] = {
    { 400000.f, 0.f, 100.f },
    { 300000.f, 0.f, 100.f },
    { 800000.f, 0.f, 200.f },
  };
  TEST_ASSERT_FALSE(scalesCalibration::solve(samples, 3).ok);
}

void test_calibration_per_cell_weighings(void) {
  // Tray off: X and Y right on cell 1, then on cell 2 (each loads one cell only).
  const float f1 = 3920.f, f2 = 4210.f;
  scalesCalibration::Sample samples[4] = {
    place(100.f, 1.f, f1, f2, 0.f, 12.f),   // tiny noise on the unloaded cell
    place(250.f, 1.f, f1, f2, 0.f, -9.f),
    place(100.f, 0.f, f1, f2, 15.f, 0.f),
    place(250.f, 0.f, f1, f2, -11.f, 0.f),
  };
  scalesCalibration::Result result = scalesCalibration::solve(samples, 4);
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_FLOAT_WITHIN(2.f, f1, result.factor1);
  TEST_ASSERT_FLOAT_WITHIN(2.f, f2, result.factor2);
  TEST_ASSERT_TRUE(result.maxErrorGrams < 0.1f);
}

void test_calibration_tray_check_pulls_factors_toward_tray(void) {
  // The cells read slightly differently with the tray on (e.g. mounting). Per-cell factors are
  // 1% off for the tray; adding the tray weighing (counted twice) must reduce the tray error.
  const float f1 = 4000.f, f2 = 4000.f;
  scalesCalibration::Sample samples[6] = {
    place(100.f, 1.f, f1, f2), place(250.f, 1.f, f1, f2),
    place(100.f, 0.f, f1, f2), place(250.f, 0.f, f1, f2),
  };
  scalesCalibration::Result perCell = scalesCalibration::solve(samples, 4);
  TEST_ASSERT_TRUE(perCell.ok);

  // 350 g on the tray reads as if it were 353.5 g with the per-cell factors.
  samples[4] = place(350.f * 1.01f, 0.5f, f1, f2);
  samples[4].grams = 350.f;
  samples[5] = samples[4];
  float before = scalesCalibration::grams(samples[4].raw1, samples[4].raw2, perCell.factor1, perCell.factor2);
  scalesCalibration::Result refined = scalesCalibration::solve(samples, 6);
  TEST_ASSERT_TRUE(refined.ok);
  float after = scalesCalibration::grams(samples[4].raw1, samples[4].raw2, refined.factor1, refined.factor2);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 353.5f, before);
  TEST_ASSERT_TRUE(fabsf(after - 350.f) < fabsf(before - 350.f) / 2.f);
}

void runAllScalesCalibrationTests() {
  RUN_TEST(test_calibration_per_cell_weighings);
  RUN_TEST(test_calibration_tray_check_pulls_factors_toward_tray);
  RUN_TEST(test_calibration_recovers_both_factors);
  RUN_TEST(test_calibration_handles_negative_factor_and_noise);
  RUN_TEST(test_calibration_rejects_same_placement);
  RUN_TEST(test_calibration_rejects_dead_cell);
}
