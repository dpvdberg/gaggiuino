#include "machine_state.h"

namespace {
  bool anySwitch(const MachineInputs &in) {
    return in.brewSwitch || in.steamSwitch || in.hotWaterSwitch;
  }

  // Where a switch takes the machine from IDLE. Returns IDLE when nothing applies.
  MachineState fromSwitches(const MachineInputs &in) {
    // Hot water (separate switch or brew+steam on a Gaggia Classic) and steam: profile and steam modes.
    bool steamModes = in.mode == BrewMode::PROFILE || in.mode == BrewMode::STEAM;
    if (in.hotWaterSwitch && in.mode == BrewMode::PROFILE) return MachineState::HOT_WATER;
    if (in.steamSwitch && steamModes) return MachineState::STEAM;

    // The brew switch only starts something on a fresh press.
    if (in.brewSwitch && in.brewArmed && in.readyToBrew) {
      switch (in.mode) {
        case BrewMode::MANUAL: return MachineState::MANUAL_BREWING;
        case BrewMode::FLUSH: return MachineState::FLUSH;
        case BrewMode::DESCALE: return MachineState::DESCALE;
        case BrewMode::STEAM: return MachineState::FLUSH;  // group purge on the steam page
        default: return MachineState::BREWING;
      }
    }
    return MachineState::IDLE;
  }
}

MachineState nextMachineState(MachineState current, const MachineInputs &in) {
  // Safety first, from any state.
  if (!in.temperatureValid) return MachineState::FAULT_TEMP;
  if (current == MachineState::FAULT_TEMP) {
    return in.startupDone ? MachineState::IDLE : MachineState::STARTUP_FILL;
  }
  if (in.steamForgotten || (current == MachineState::FAULT_STEAM && in.steamSwitch)) {
    return MachineState::FAULT_STEAM;
  }
  if (!in.startupDone) return MachineState::STARTUP_FILL;

  switch (current) {
    case MachineState::BREWING:
      if (!in.brewSwitch) return MachineState::IDLE;
      if (in.shotFinished) return MachineState::SHOT_DONE;
      return current;

    case MachineState::MANUAL_BREWING:
    case MachineState::FLUSH:
    case MachineState::DESCALE:
      return in.brewSwitch ? current : MachineState::IDLE;

    case MachineState::SHOT_DONE:
      if (in.steamSwitch || in.hotWaterSwitch) return MachineState::IDLE;
      return in.onHomePage ? MachineState::WAIT_SWITCH_OFF : current;

    case MachineState::WAIT_SWITCH_OFF:
      if (in.steamSwitch || in.hotWaterSwitch) return MachineState::IDLE;
      if (in.brewSwitch) return current;
      return in.cleaningWanted ? MachineState::CLEANING : MachineState::IDLE;

    case MachineState::CLEANING:
      if (anySwitch(in)) return MachineState::IDLE;
      return in.cleaningDone ? MachineState::IDLE : current;

    case MachineState::STEAM:
      return in.steamSwitch ? current : MachineState::IDLE;

    case MachineState::HOT_WATER:
      return in.hotWaterSwitch ? current : MachineState::IDLE;

    case MachineState::CALIBRATING:
      if (anySwitch(in) || !in.calibrationRunning) return MachineState::IDLE;
      return current;

    case MachineState::PRESSURE_RELEASE:
      if (anySwitch(in)) return fromSwitches(in);
      return in.pressureReleased ? MachineState::IDLE : current;

    case MachineState::STARTUP_FILL:
    case MachineState::FAULT_STEAM:
    case MachineState::IDLE:
    default: {
      MachineState next = fromSwitches(in);
      if (next != MachineState::IDLE) return next;
      if (in.calibrationRunning) return MachineState::CALIBRATING;
      if (in.pressureReleaseDue) return MachineState::PRESSURE_RELEASE;
      return MachineState::IDLE;
    }
  }
}

const char *machineStateName(MachineState state) {
  switch (state) {
    case MachineState::STARTUP_FILL: return "STARTUP_FILL";
    case MachineState::IDLE: return "IDLE";
    case MachineState::BREWING: return "BREWING";
    case MachineState::MANUAL_BREWING: return "MANUAL_BREWING";
    case MachineState::SHOT_DONE: return "SHOT_DONE";
    case MachineState::WAIT_SWITCH_OFF: return "WAIT_SWITCH_OFF";
    case MachineState::CLEANING: return "CLEANING";
    case MachineState::STEAM: return "STEAM";
    case MachineState::HOT_WATER: return "HOT_WATER";
    case MachineState::FLUSH: return "FLUSH";
    case MachineState::DESCALE: return "DESCALE";
    case MachineState::PRESSURE_RELEASE: return "PRESSURE_RELEASE";
    case MachineState::CALIBRATING: return "CALIBRATING";
    case MachineState::FAULT_TEMP: return "FAULT_TEMP";
    case MachineState::FAULT_STEAM: return "FAULT_STEAM";
  }
  return "?";
}
