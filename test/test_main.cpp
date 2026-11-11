#include <unity.h>
#include "./tests/test_pressure_profiler.cpp"
#include "./tests/test_pump.cpp"
#include "./tests/test_profile_serializer.cpp"
#include "./tests/test_median_filter.cpp"
#include "./tests/test_stages.cpp"
#include "./tests/test_scales_calibration.cpp"
#include "./tests/test_machine_state.cpp"
#include "./tests/test_weight_step_guard.cpp"

// Unity 2.6+ no longer provides default (weak) hooks.
void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    runAllPressureProfilerTests();
    runAllPumpTests();
    runAllProfileSerializerTests();
    runAllMedianFilterTests();
    runAllStagesTests();
    runAllScalesCalibrationTests();
    runAllMachineStateTests();
    runAllWeightStepGuardTests();
    UNITY_END();
}
