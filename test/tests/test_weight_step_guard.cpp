#include <unity.h>
#include <median_filter.h>
#include <weight_step_guard.h>

void test_weight_step_guard_passes_normal_flow(void) {
  WeightStepGuard guard;
  guard.reset(0.f, 1000);
  float out = 0.f;
  for (int i = 1; i <= 100; i++) {
    out = guard.apply(i * 0.6f, 1000 + i * 100); // 6 g/s, a fast shot
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.f, out);
}

void test_weight_step_guard_absorbs_cup_placed_after_tare(void) {
  WeightStepGuard guard;
  guard.reset(0.f, 1000);
  bool absorbed = false;
  TEST_ASSERT_EQUAL_FLOAT(0.f, guard.apply(0.f, 1100));
  TEST_ASSERT_EQUAL_FLOAT(0.f, guard.apply(120.f, 1200, &absorbed)); // cup placed
  TEST_ASSERT_TRUE(absorbed);
  // Coffee on top of the cup still counts.
  TEST_ASSERT_EQUAL_FLOAT(0.5f, guard.apply(120.5f, 1300, &absorbed));
  TEST_ASSERT_FALSE(absorbed);
  TEST_ASSERT_EQUAL_FLOAT(1.f, guard.apply(121.f, 1400));
}

void test_weight_step_guard_absorbs_failed_tare_through_median(void) {
  // Tare gave up: the filters restart at 0 but the scales still read the cup.
  MedianFilter3 filter;
  WeightStepGuard guard;
  filter.reset(0.f);
  guard.reset(0.f, 1000);
  float out = 0.f;
  for (int i = 1; i <= 5; i++) {
    out = guard.apply(filter.add(150.f), 1000 + i * 100);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.f, out);
  guard.apply(filter.add(152.f), 1600);
  out = guard.apply(filter.add(152.f), 1700); // median follows after 2 readings
  TEST_ASSERT_EQUAL_FLOAT(2.f, out);
}

void test_weight_step_guard_absorbs_cup_lifted(void) {
  WeightStepGuard guard;
  guard.reset(0.f, 0);
  guard.apply(10.f, 2000);  // 10 g over 2 s: real coffee
  TEST_ASSERT_EQUAL_FLOAT(10.f, guard.apply(10.f, 2100));
  TEST_ASSERT_EQUAL_FLOAT(10.f, guard.apply(-110.f, 2200)); // cup lifted
  TEST_ASSERT_EQUAL_FLOAT(10.f, guard.apply(10.f, 2300));   // and put back
}

void test_weight_step_guard_accepts_gain_after_missed_readings(void) {
  // Scales had no reading for 2 s while coffee kept flowing: not a step.
  WeightStepGuard guard;
  guard.reset(5.f, 1000);
  TEST_ASSERT_EQUAL_FLOAT(13.f, guard.apply(13.f, 3000));
}

void test_weight_step_guard_reset_clears_offset(void) {
  WeightStepGuard guard;
  guard.reset(0.f, 0);
  guard.apply(100.f, 100);
  guard.reset(0.f, 200);
  TEST_ASSERT_EQUAL_FLOAT(1.f, guard.apply(1.f, 300));
}

void runAllWeightStepGuardTests() {
  RUN_TEST(test_weight_step_guard_passes_normal_flow);
  RUN_TEST(test_weight_step_guard_absorbs_cup_placed_after_tare);
  RUN_TEST(test_weight_step_guard_absorbs_failed_tare_through_median);
  RUN_TEST(test_weight_step_guard_absorbs_cup_lifted);
  RUN_TEST(test_weight_step_guard_accepts_gain_after_missed_readings);
  RUN_TEST(test_weight_step_guard_reset_clears_offset);
}
