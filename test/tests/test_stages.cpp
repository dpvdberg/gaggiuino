#include <unity.h>
#include <string.h>
#include "../src/functional/stages.cpp"

namespace {
  stage_t pressureStage(float end, float exitTime = 0.f) {
    stage_t s = {};
    s.type = StageType::PRESSURE;
    s.curve = StageCurve::LINEAR;
    s.end = end;
    s.exitTime = exitTime;
    return s;
  }

  // The example shot: PI to 3 bar, soak 5 s, ramp to 9 over 3 s, hold 10 s, 6 bar until weight.
  eepromValues_t::profile_t exampleProfile() {
    eepromValues_t::profile_t p = {};
    snprintf(p.name, sizeof(p.name), "Example");
    p.stageCount = 5;
    p.stages[0] = { StageType::FLOW, StageCurve::LINEAR, false, false, 0.f, 4.f, 0.f, 3.f, 15.f, 3.f, 0.f, 0.f };
    p.stages[1] = { StageType::PRESSURE, StageCurve::LINEAR, false, false, 0.f, 0.f, 0.f, 0.f, 5.f, 0.f, 0.f, 0.f };
    p.stages[2] = { StageType::PRESSURE, StageCurve::LINEAR, false, false, 0.f, 9.f, 3.f, 0.f, 3.f, 0.f, 0.f, 0.f };
    p.stages[3] = { StageType::PRESSURE, StageCurve::LINEAR, false, false, 9.f, 9.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f };
    p.stages[4] = { StageType::PRESSURE, StageCurve::LINEAR, false, false, 6.f, 6.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
    return p;
  }
}

void test_stages_insert_after_selected(void) {
  eepromValues_t::profile_t p = {};
  p.stageCount = 2;
  p.stages[0] = pressureStage(1.f);
  p.stages[1] = pressureStage(3.f);

  int at = stages::insertAfter(p, 0, pressureStage(2.f));
  TEST_ASSERT_EQUAL(1, at);
  TEST_ASSERT_EQUAL(3, p.stageCount);
  TEST_ASSERT_EQUAL_FLOAT(1.f, p.stages[0].end);
  TEST_ASSERT_EQUAL_FLOAT(2.f, p.stages[1].end);
  TEST_ASSERT_EQUAL_FLOAT(3.f, p.stages[2].end);

  at = stages::insertAfter(p, -1, pressureStage(4.f));  // nothing selected: append
  TEST_ASSERT_EQUAL(3, at);
  TEST_ASSERT_EQUAL_FLOAT(4.f, p.stages[3].end);
}

void test_stages_insert_stops_at_max(void) {
  eepromValues_t::profile_t p = {};
  p.stageCount = MAX_STAGES;
  TEST_ASSERT_EQUAL(-1, stages::insertAfter(p, 0, pressureStage(1.f)));
  TEST_ASSERT_EQUAL(MAX_STAGES, p.stageCount);
}

void test_stages_remove_and_keep_last(void) {
  eepromValues_t::profile_t p = {};
  p.stageCount = 3;
  p.stages[0] = pressureStage(1.f);
  p.stages[1] = pressureStage(2.f);
  p.stages[2] = pressureStage(3.f);

  TEST_ASSERT_TRUE(stages::remove(p, 1));
  TEST_ASSERT_EQUAL(2, p.stageCount);
  TEST_ASSERT_EQUAL_FLOAT(3.f, p.stages[1].end);

  TEST_ASSERT_TRUE(stages::remove(p, 0));
  TEST_ASSERT_FALSE(stages::remove(p, 0));  // the only stage stays
  TEST_ASSERT_EQUAL(1, p.stageCount);
}

void test_stages_move(void) {
  eepromValues_t::profile_t p = {};
  p.stageCount = 3;
  p.stages[0] = pressureStage(1.f);
  p.stages[1] = pressureStage(2.f);
  p.stages[2] = pressureStage(3.f);

  TEST_ASSERT_EQUAL(2, stages::move(p, 1, +1));
  TEST_ASSERT_EQUAL_FLOAT(3.f, p.stages[1].end);
  TEST_ASSERT_EQUAL_FLOAT(2.f, p.stages[2].end);
  TEST_ASSERT_EQUAL(2, stages::move(p, 2, +1));  // already last
  TEST_ASSERT_EQUAL(0, stages::move(p, 0, -1));  // already first
}

void test_stages_to_phase(void) {
  stage_t ramp = { StageType::PRESSURE, StageCurve::EASE_OUT, true, false, 2.f, 9.f, 3.f, 2.5f, 4.f, 8.5f, 0.f, 0.f };
  Phase phase = stages::toPhase(ramp);
  TEST_ASSERT_TRUE(phase.type == PHASE_TYPE::PHASE_TYPE_PRESSURE);
  TEST_ASSERT_EQUAL_FLOAT(2.f, phase.target.start);
  TEST_ASSERT_EQUAL_FLOAT(9.f, phase.target.end);
  TEST_ASSERT_EQUAL(3000, phase.target.time);
  TEST_ASSERT_TRUE(phase.target.curve == TransitionCurve::EASE_OUT);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, phase.restriction);
  TEST_ASSERT_EQUAL(4000, phase.stopConditions.time);
  TEST_ASSERT_EQUAL_FLOAT(8.5f, phase.stopConditions.pressureAbove);
  TEST_ASSERT_EQUAL_FLOAT(-1.f, phase.stopConditions.pressureBelow);
  TEST_ASSERT_EQUAL_FLOAT(-1.f, phase.stopConditions.weight);
  TEST_ASSERT_TRUE(phase.tareAtStart);
  TEST_ASSERT_FALSE(phase.tareAtEnd);

  // No ramp, no limits, no exits: flat target, everything off
  Phase flat = stages::toPhase(pressureStage(6.f));
  TEST_ASSERT_TRUE(flat.target.isInstant());
  TEST_ASSERT_EQUAL_FLOAT(6.f, flat.target.end);
  TEST_ASSERT_EQUAL_FLOAT(-1.f, flat.restriction);
  TEST_ASSERT_EQUAL(-1, flat.stopConditions.time);
}

void test_stages_build_phases_with_prefill_and_hints(void) {
  eepromValues_t::profile_t p = exampleProfile();
  Profile profile;
  stages::PredictiveHints hints = stages::buildPhases(p, true, profile);

  TEST_ASSERT_EQUAL(6, profile.phaseCount());  // prefill + 5 stages
  TEST_ASSERT_TRUE(profile.phases[0].type == PHASE_TYPE::PHASE_TYPE_FLOW);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, profile.phases[0].stopConditions.pressureAbove);
  TEST_ASSERT_TRUE(profile.phases[1].type == PHASE_TYPE::PHASE_TYPE_FLOW);
  TEST_ASSERT_EQUAL(2, hints.soakPhaseIdx);                  // stage 2 (soak) is phase 2
  TEST_ASSERT_EQUAL_FLOAT(3.f, hints.preinfusionPressure);   // PI exits above 3 bar
}

void test_stages_example_shot_runs_in_order(void) {
  eepromValues_t::profile_t p = exampleProfile();
  Profile profile;
  stages::buildPhases(p, false, profile);
  PhaseProfiler profiler{profile};
  SensorState state{};

  // Preinfusion: flow until pressure above 3 bar
  profiler.updatePhase(1000, state);
  TEST_ASSERT_EQUAL(0, profiler.getCurrentPhase().getIndex());
  state.smoothedPressure = 3.2f;
  profiler.updatePhase(4000, state);
  profiler.updatePhase(4010, state);
  TEST_ASSERT_EQUAL(1, profiler.getCurrentPhase().getIndex());  // soak
  TEST_ASSERT_EQUAL_FLOAT(0.f, profiler.getCurrentPhase().getTarget());

  // Soak 5 s, then ramp 3 s, then hold 10 s
  profiler.updatePhase(9100, state);
  profiler.updatePhase(9110, state);
  TEST_ASSERT_EQUAL(2, profiler.getCurrentPhase().getIndex());
  profiler.updatePhase(12200, state);
  profiler.updatePhase(12210, state);
  TEST_ASSERT_EQUAL(3, profiler.getCurrentPhase().getIndex());
  TEST_ASSERT_EQUAL_FLOAT(9.f, profiler.getCurrentPhase().getTarget());
  profiler.updatePhase(22300, state);
  profiler.updatePhase(22310, state);
  TEST_ASSERT_EQUAL(4, profiler.getCurrentPhase().getIndex());
  TEST_ASSERT_EQUAL_FLOAT(6.f, profiler.getCurrentPhase().getTarget());

  // Last stage has no exit of its own: runs until the global stop
  profiler.updatePhase(60000, state);
  TEST_ASSERT_FALSE(profiler.isFinished());
}

void test_stages_summary_text(void) {
  char buf[40];
  eepromValues_t::profile_t p = exampleProfile();

  stages::formatSummary(p.stages[0], 1, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1 F 4ml  15s >3b", buf);
  stages::formatSummary(p.stages[2], 3, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("3 P >9b/3s  3s", buf);
  stages::formatSummary(p.stages[4], 5, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("5 P 6b ", buf);

  char small[8];
  stages::formatSummary(p.stages[0], 1, small, sizeof(small));  // truncates safely
  TEST_ASSERT_EQUAL(7, (int)strlen(small));
}

void test_stages_preview_samples_and_markers(void) {
  eepromValues_t::profile_t p = exampleProfile();
  stages::Preview preview(p);

  // 15 + 5 + 3 + 10 + 25 (estimated last stage)
  TEST_ASSERT_EQUAL_FLOAT(58.f, preview.totalSeconds());
  TEST_ASSERT_TRUE(preview.hasEstimates());

  stages::PreviewSample pi = preview.sampleAt(5.f);
  TEST_ASSERT_EQUAL(0, pi.stage);
  TEST_ASSERT_EQUAL_FLOAT(4.f, pi.flow);
  TEST_ASSERT_EQUAL_FLOAT(3.f, pi.pressure);  // limit shown as pressure

  stages::PreviewSample rampHalf = preview.sampleAt(20.f + 1.5f);
  TEST_ASSERT_EQUAL(2, rampHalf.stage);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 4.5f, rampHalf.pressure);  // halfway 0 -> 9

  TEST_ASSERT_EQUAL(3, preview.sampleAt(25.f).stage);
  TEST_ASSERT_EQUAL_FLOAT(9.f, preview.sampleAt(25.f).pressure);
  TEST_ASSERT_EQUAL(4, preview.sampleAt(57.f).stage);
  TEST_ASSERT_EQUAL_FLOAT(6.f, preview.sampleAt(57.f).pressure);
}

void test_stages_exit_checks(void) {
  eepromValues_t::profile_t p = exampleProfile();
  TEST_ASSERT_EQUAL(-1, stages::firstStageWithoutExit(p));  // the last stage may run until weight

  p.stages[1].exitTime = 0.f;                               // soak without an exit stalls the shot
  TEST_ASSERT_FALSE(stages::hasExit(p.stages[1]));
  TEST_ASSERT_EQUAL(1, stages::firstStageWithoutExit(p));

  char buf[48];
  stages::formatSummary(p.stages[1], 2, buf, sizeof(buf), true);
  TEST_ASSERT_NOT_NULL(strstr(buf, "no exit!"));
  stages::formatSummary(p.stages[4], 5, buf, sizeof(buf), false);
  TEST_ASSERT_NULL(strstr(buf, "no exit!"));

  p.stages[1].exitWeight = 2.f;
  TEST_ASSERT_TRUE(stages::hasExit(p.stages[1]));
  TEST_ASSERT_EQUAL(-1, stages::firstStageWithoutExit(p));
}

void runAllStagesTests() {
  RUN_TEST(test_stages_exit_checks);
  RUN_TEST(test_stages_insert_after_selected);
  RUN_TEST(test_stages_insert_stops_at_max);
  RUN_TEST(test_stages_remove_and_keep_last);
  RUN_TEST(test_stages_move);
  RUN_TEST(test_stages_to_phase);
  RUN_TEST(test_stages_build_phases_with_prefill_and_hints);
  RUN_TEST(test_stages_example_shot_runs_in_order);
  RUN_TEST(test_stages_summary_text);
  RUN_TEST(test_stages_preview_samples_and_markers);
}
