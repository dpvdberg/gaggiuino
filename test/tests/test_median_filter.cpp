#include <unity.h>
#include <median_filter.h>
#include <profiling_phases.h>
#include <sensors_state.h>

void test_median_filter_first_value_passes_through(void) {
  MedianFilter3 filter;
  TEST_ASSERT_EQUAL_FLOAT(12.5f, filter.add(12.5f));
}

void test_median_filter_ignores_single_spike(void) {
  MedianFilter3 filter;
  filter.reset(10.f);
  TEST_ASSERT_EQUAL_FLOAT(10.f, filter.add(10.f));
  TEST_ASSERT_EQUAL_FLOAT(10.f, filter.add(0.f));   // dropout to 0
  TEST_ASSERT_EQUAL_FLOAT(10.f, filter.add(10.f));
  TEST_ASSERT_EQUAL_FLOAT(10.f, filter.add(500.f)); // spike up
  TEST_ASSERT_EQUAL_FLOAT(10.f, filter.add(10.f));
}

void test_median_filter_follows_real_step(void) {
  // The old jump rejection froze on a step like this forever.
  MedianFilter3 filter;
  filter.reset(0.f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, filter.add(20.f));
  TEST_ASSERT_EQUAL_FLOAT(20.f, filter.add(20.f));
  TEST_ASSERT_EQUAL_FLOAT(20.f, filter.add(21.f));
  TEST_ASSERT_EQUAL_FLOAT(21.f, filter.add(22.f));
}

void test_median_filter_follows_rising_weight(void) {
  MedianFilter3 filter;
  filter.reset(0.f);
  float out = 0.f;
  for (int i = 1; i <= 50; i++) {
    out = filter.add(i * 0.4f); // ~4 g/s at 100 ms reads
  }
  // Lags at most one reading behind.
  TEST_ASSERT_FLOAT_WITHIN(0.41f, 20.f, out);
}

void test_median_filter_reset_after_tare(void) {
  MedianFilter3 filter;
  filter.reset(0.f);
  filter.add(30.f);
  filter.add(30.f);
  filter.reset(0.f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, filter.add(30.f)); // stale pre-tare value ignored
  TEST_ASSERT_EQUAL_FLOAT(0.1f, filter.add(0.1f));
}

void test_global_stop_safety_net_with_stuck_scales(void) {
  GlobalStopConditions stop{ .time = 120000L, .weight = 36.f, .waterPumped = 250.f };
  SensorState state{};
  state.shotWeight = 5.f; // scales stuck well below target
  state.weight = 5.f;

  state.waterPumped = 100.f;
  TEST_ASSERT_FALSE(stop.isReached(state, 60000));
  TEST_ASSERT_TRUE(stop.isReached(state, 120000));

  state.waterPumped = 251.f;
  TEST_ASSERT_TRUE(stop.isReached(state, 60000));
}

void runAllMedianFilterTests() {
  RUN_TEST(test_median_filter_first_value_passes_through);
  RUN_TEST(test_median_filter_ignores_single_spike);
  RUN_TEST(test_median_filter_follows_real_step);
  RUN_TEST(test_median_filter_follows_rising_weight);
  RUN_TEST(test_median_filter_reset_after_tare);
  RUN_TEST(test_global_stop_safety_net_with_stuck_scales);
}
