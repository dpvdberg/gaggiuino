#ifndef MACHINE_STATE_H
#define MACHINE_STATE_H

#include <stdint.h>

/**
 * The machine's operating state. Every loop the firmware gathers MachineInputs, asks
 * nextMachineState() where to go, runs the entry/exit actions of a state change, and then
 * drives the outputs (pump, valve, heaters) for the current state. Nothing blocks.
 */
enum class MachineState : uint8_t {
  STARTUP_FILL,      // filling the boiler after power-on (LEGO / single board)
  IDLE,              // keeping temperature, pump off
  BREWING,           // running the stage profile
  MANUAL_BREWING,    // manual flow control from the screen
  SHOT_DONE,         // profile finished, waiting for the user to go back to the home screen
  WAIT_SWITCH_OFF,   // on the home screen, waiting for the brew switch to go off
  CLEANING,          // short showerhead rinse after a shot
  STEAM,
  HOT_WATER,
  FLUSH,             // backflush (or group purge in steam mode)
  DESCALE,
  PRESSURE_RELEASE,  // venting left-over pressure (LEGO / single board)
  CALIBRATING,       // pump zero calibration run, driven by the calibration screen
  FAULT_TEMP,        // no valid temperature reading: everything off
  FAULT_STEAM,       // steam left on too long: everything off until the steam switch is off
};

// What the brew switch does, chosen on the screen.
// Sub-steps of the STARTUP_FILL state.
enum class StartupFillStep : uint8_t { WAIT_FOR_HOME, TARE, BASELINE, FILLING, DONE };

enum class BrewMode : uint8_t { PROFILE, MANUAL, FLUSH, DESCALE, STEAM };

struct MachineInputs {
  bool brewSwitch = false;
  bool steamSwitch = false;
  bool hotWaterSwitch = false;
  bool brewArmed = false;            // brew switch was off since the last brew/flush/descale session
  BrewMode mode = BrewMode::PROFILE;

  bool startupDone = false;          // startup boiler fill finished or skipped
  bool temperatureValid = true;
  bool steamForgotten = false;
  bool readyToBrew = true;           // e.g. enough water in the tank

  bool shotFinished = false;         // the profiler reached the end of the profile
  bool onHomePage = false;
  bool cleaningWanted = false;       // boiler cool enough to rinse the showerhead
  bool cleaningDone = false;

  bool pressureReleaseDue = false;   // left-over pressure should be vented now
  bool pressureReleased = false;     // pressure is back below the threshold

  bool calibrationRunning = false;   // the calibration screen is running the pump
};

MachineState nextMachineState(MachineState current, const MachineInputs &in);
const char *machineStateName(MachineState state);

// States where the brew switch session is active (brew timer, flow tracking, LED effects).
inline bool isBrewSession(MachineState state) {
  return state == MachineState::BREWING || state == MachineState::MANUAL_BREWING
    || state == MachineState::FLUSH || state == MachineState::DESCALE;
}

#endif
