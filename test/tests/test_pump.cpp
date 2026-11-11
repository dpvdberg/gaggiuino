#include "mock.h"
#include "../src/peripherals/pump.cpp"
#include "utils/test-utils.h"

void test_pump_clicks_for_flow_correct_binary_search(void) {
  //corrected from 31 old value due to correcting the formula for flow/click calculations
  TEST_ASSERT_EQUAL_FLOAT_ACCURACY(24, getClicksPerSecondForFlow(5, 2), 0);
  TEST_ASSERT_EQUAL_FLOAT_ACCURACY(50, getClicksPerSecondForFlow(10, 9), 0);
  TEST_ASSERT_EQUAL_FLOAT_ACCURACY(0, getClicksPerSecondForFlow(0, 0), 0);
}

void test_pump_get_flow(void) {
  TEST_ASSERT_EQUAL_FLOAT_ACCURACY(5.f, getPumpFlow(getClicksPerSecondForFlow(5, 2), 2), 1);
  TEST_ASSERT_EQUAL_FLOAT_ACCURACY(4.5f, getPumpFlow(getClicksPerSecondForFlow(4.5, 5), 5), 1);
}

void test_pump_flow_per_click_at_zero_bar_is_finite(void) {
  // Used to compute 0.009 / 0 * 0 = NaN, which a flow stage turned into full pump power.
  float atZero = getPumpFlowPerClick(0.f);
  TEST_ASSERT_FALSE(isnan(atZero));
  TEST_ASSERT_FLOAT_WITHIN(0.002f, getPumpFlowPerClick(0.001f), atZero);  // continuous at 0
  TEST_ASSERT_FALSE(isnan(getClicksPerSecondForFlow(4.f, 0.f)));
  TEST_ASSERT_TRUE(getClicksPerSecondForFlow(4.f, 0.f) < 50.f);  // not "full power"
}

#include "../src/functional/pump_calibration.h"

void test_pump_zero_calibration_recovers_true_value(void) {
  pumpInit(50, 0.2225f);  // value in use
  const float pressure = 0.3f;
  float modelPerStroke = getPumpFlowPerClick(pressure);

  // The real pump delivers what the model would give with a pump zero of 0.2400.
  pumpInit(50, 0.2400f);
  float realPerStroke = getPumpFlowPerClick(pressure);
  long strokes = 600;
  float grams = realPerStroke * strokes;

  float zero = pumpCalibration::newPumpZero(0.2225f, grams, strokes, modelPerStroke, getPumpClickMultiplier());
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.2400f, zero);
  TEST_ASSERT_TRUE(pumpCalibration::inRange(zero));
  TEST_ASSERT_TRUE(pumpCalibration::newPumpZero(0.2225f, 0.f, strokes, modelPerStroke, 1.2f) < 0.f);  // no water
  TEST_ASSERT_TRUE(pumpCalibration::newPumpZero(0.2225f, grams, 0, modelPerStroke, 1.2f) < 0.f);      // no strokes
  pumpInit(50, 0.2225f);
}

void runAllPumpTests() {
  RUN_TEST(test_pump_clicks_for_flow_correct_binary_search);
  RUN_TEST(test_pump_get_flow);
  RUN_TEST(test_pump_flow_per_click_at_zero_bar_is_finite);
  RUN_TEST(test_pump_zero_calibration_recovers_true_value);
}
