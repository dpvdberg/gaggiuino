#ifndef STAGE_SCREENS_H
#define STAGE_SCREENS_H

#include "lcd.h"
#include "../eeprom_data/eeprom_data.h"

/**
 * Controller for the stage list (stages page) and stage editor (stageTarget / stageExit pages).
 *
 * List: tap a row to select it, tap it again to edit. Add inserts a copy of the selected stage
 * after it, Delete asks for a second tap, Move switches the arrows from paging to moving the
 * selected stage. While a shot runs, the profile can be viewed but not changed.
 *
 * The screen triggers (lcdStage*Trigger in lcd.h) are implemented in stage_screens.cpp.
 */
namespace stageScreens {

  struct Context {
    eepromValues_t *cfg;                                // running config; the active profile is edited in place
    bool (*isBrewing)(void);                            // edits are refused while true
    void (*stagesChanged)(void);                        // rebuild the profiler after an edit
    void (*save)(const eepromValues_t &values);         // write to EEPROM and tell the user
  };

  void init(const Context &context);
  // Forget selection / move mode, e.g. after the active profile changed elsewhere.
  void reset(void);
  // Call once per screen page change.
  void pageChanged(NextionPage from, NextionPage to);
}

#endif
