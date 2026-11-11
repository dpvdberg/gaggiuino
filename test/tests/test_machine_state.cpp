#include <unity.h>
#include "../src/functional/machine_state.cpp"

namespace {
  // Idle machine after startup: everything off, brew switch armed, water in the tank.
  MachineInputs idleInputs() {
    MachineInputs in = {};
    in.brewArmed = true;
    in.mode = BrewMode::PROFILE;
    in.startupDone = true;
    in.temperatureValid = true;
    in.readyToBrew = true;
    in.pressureReleased = true;
    return in;
  }

  void assertState(MachineState expected, MachineState actual) {
    TEST_ASSERT_EQUAL_STRING(machineStateName(expected), machineStateName(actual));
  }
}

void test_machine_startup_fill_until_done(void) {
  MachineInputs in = idleInputs();
  in.startupDone = false;
  in.brewSwitch = true;
  assertState(MachineState::STARTUP_FILL, nextMachineState(MachineState::STARTUP_FILL, in));
  in.startupDone = true;
  in.brewSwitch = false;
  assertState(MachineState::IDLE, nextMachineState(MachineState::STARTUP_FILL, in));
}

void test_machine_temp_fault_from_any_state(void) {
  MachineInputs in = idleInputs();
  in.brewSwitch = true;
  in.temperatureValid = false;
  assertState(MachineState::FAULT_TEMP, nextMachineState(MachineState::BREWING, in));
  in.temperatureValid = true;
  in.brewSwitch = false;
  assertState(MachineState::IDLE, nextMachineState(MachineState::FAULT_TEMP, in));
}

void test_machine_brew_needs_fresh_press_and_water(void) {
  MachineInputs in = idleInputs();
  in.brewSwitch = true;
  in.brewArmed = false;
  assertState(MachineState::IDLE, nextMachineState(MachineState::IDLE, in));
  in.brewArmed = true;
  in.readyToBrew = false;
  assertState(MachineState::IDLE, nextMachineState(MachineState::IDLE, in));
  in.readyToBrew = true;
  assertState(MachineState::BREWING, nextMachineState(MachineState::IDLE, in));
}

void test_machine_brew_switch_follows_mode(void) {
  MachineInputs in = idleInputs();
  in.brewSwitch = true;
  in.mode = BrewMode::MANUAL;
  assertState(MachineState::MANUAL_BREWING, nextMachineState(MachineState::IDLE, in));
  in.mode = BrewMode::FLUSH;
  assertState(MachineState::FLUSH, nextMachineState(MachineState::IDLE, in));
  in.mode = BrewMode::DESCALE;
  assertState(MachineState::DESCALE, nextMachineState(MachineState::IDLE, in));
  in.mode = BrewMode::STEAM;  // group purge from the steam page
  assertState(MachineState::FLUSH, nextMachineState(MachineState::IDLE, in));
}

void test_machine_shot_done_then_cleaning(void) {
  MachineInputs in = idleInputs();
  in.brewSwitch = true;
  in.shotFinished = true;
  assertState(MachineState::SHOT_DONE, nextMachineState(MachineState::BREWING, in));
  assertState(MachineState::SHOT_DONE, nextMachineState(MachineState::SHOT_DONE, in));
  in.onHomePage = true;
  assertState(MachineState::WAIT_SWITCH_OFF, nextMachineState(MachineState::SHOT_DONE, in));
  assertState(MachineState::WAIT_SWITCH_OFF, nextMachineState(MachineState::WAIT_SWITCH_OFF, in));
  in.brewSwitch = false;
  in.cleaningWanted = true;
  assertState(MachineState::CLEANING, nextMachineState(MachineState::WAIT_SWITCH_OFF, in));
  in.cleaningDone = true;
  assertState(MachineState::IDLE, nextMachineState(MachineState::CLEANING, in));
}

void test_machine_brew_switch_off_stops_session(void) {
  MachineInputs in = idleInputs();
  assertState(MachineState::IDLE, nextMachineState(MachineState::BREWING, in));
  assertState(MachineState::IDLE, nextMachineState(MachineState::MANUAL_BREWING, in));
  assertState(MachineState::IDLE, nextMachineState(MachineState::DESCALE, in));
}

void test_machine_steam_and_hot_water(void) {
  MachineInputs in = idleInputs();
  in.steamSwitch = true;
  assertState(MachineState::STEAM, nextMachineState(MachineState::IDLE, in));
  in.mode = BrewMode::MANUAL;
  assertState(MachineState::IDLE, nextMachineState(MachineState::IDLE, in));
  in = idleInputs();
  in.hotWaterSwitch = true;
  assertState(MachineState::HOT_WATER, nextMachineState(MachineState::IDLE, in));
  in.hotWaterSwitch = false;
  assertState(MachineState::IDLE, nextMachineState(MachineState::HOT_WATER, in));
}

void test_machine_steam_forgotten_holds_until_switch_off(void) {
  MachineInputs in = idleInputs();
  in.steamSwitch = true;
  in.steamForgotten = true;
  assertState(MachineState::FAULT_STEAM, nextMachineState(MachineState::STEAM, in));
  in.steamForgotten = false;
  assertState(MachineState::FAULT_STEAM, nextMachineState(MachineState::FAULT_STEAM, in));
  in.steamSwitch = false;
  assertState(MachineState::IDLE, nextMachineState(MachineState::FAULT_STEAM, in));
}

void test_machine_calibration_blocks_pressure_release(void) {
  MachineInputs in = idleInputs();
  in.calibrationRunning = true;
  in.pressureReleaseDue = true;
  assertState(MachineState::CALIBRATING, nextMachineState(MachineState::IDLE, in));
  assertState(MachineState::CALIBRATING, nextMachineState(MachineState::CALIBRATING, in));
  in.brewSwitch = true;
  assertState(MachineState::IDLE, nextMachineState(MachineState::CALIBRATING, in));
}

void test_machine_pressure_release(void) {
  MachineInputs in = idleInputs();
  in.pressureReleaseDue = true;
  in.pressureReleased = false;
  assertState(MachineState::PRESSURE_RELEASE, nextMachineState(MachineState::IDLE, in));
  assertState(MachineState::PRESSURE_RELEASE, nextMachineState(MachineState::PRESSURE_RELEASE, in));
  in.brewSwitch = true;
  assertState(MachineState::BREWING, nextMachineState(MachineState::PRESSURE_RELEASE, in));
  in.brewSwitch = false;
  in.pressureReleased = true;
  assertState(MachineState::IDLE, nextMachineState(MachineState::PRESSURE_RELEASE, in));
}

void test_machine_brew_session(void) {
  TEST_ASSERT_TRUE(isBrewSession(MachineState::BREWING));
  TEST_ASSERT_TRUE(isBrewSession(MachineState::DESCALE));
  TEST_ASSERT_FALSE(isBrewSession(MachineState::SHOT_DONE));
  TEST_ASSERT_FALSE(isBrewSession(MachineState::CALIBRATING));
}

void runAllMachineStateTests() {
  RUN_TEST(test_machine_startup_fill_until_done);
  RUN_TEST(test_machine_temp_fault_from_any_state);
  RUN_TEST(test_machine_brew_needs_fresh_press_and_water);
  RUN_TEST(test_machine_brew_switch_follows_mode);
  RUN_TEST(test_machine_shot_done_then_cleaning);
  RUN_TEST(test_machine_brew_switch_off_stops_session);
  RUN_TEST(test_machine_steam_and_hot_water);
  RUN_TEST(test_machine_steam_forgotten_holds_until_switch_off);
  RUN_TEST(test_machine_calibration_blocks_pressure_release);
  RUN_TEST(test_machine_pressure_release);
  RUN_TEST(test_machine_brew_session);
}
