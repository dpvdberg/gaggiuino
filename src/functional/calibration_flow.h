#ifndef CALIBRATION_FLOW_H
#define CALIBRATION_FLOW_H

#include "../lcd/lcd.h"
#include "../eeprom_data/eeprom_data.h"
#include "sensors_state.h"

/**
 * The calibration page: guided load-cell calibration with two known weights, and the pump
 * zero calibration run. The screen triggers (lcdCalibration*Trigger in lcd.h) are implemented
 * in calibration_flow.cpp.
 *
 * The pump run is owned by the machine state machine: once the flow starts a run,
 * pumpRunActive() becomes true, the machine switches to CALIBRATING and calls runPump() every
 * loop. If the machine leaves CALIBRATING for another reason (a switch was turned on) it calls
 * abortPumpRun().
 */
namespace calibrationFlow {

  struct Context {
    eepromValues_t *cfg;          // scales factors, pump zero, mains frequency
    const SensorState *state;     // weight and pressure
    bool (*canRunPump)(void);     // machine idle, no switches on, a brew mode selected
  };

  void init(const Context &context);
  void pageChanged(NextionPage from, NextionPage to);
  void liveUpdate(void);          // from the screen refresh while on the calibration page

  bool pumpRunActive(void);
  void runPump(void);
  void abortPumpRun(const char *reason);
}

#endif
