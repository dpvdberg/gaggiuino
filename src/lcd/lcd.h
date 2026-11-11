/* 09:32 15/03/2023 - change triggering comment */
#ifndef LCD_H
#define LCD_H

#include <EasyNextionLibrary.h>
#include "eeprom_data/eeprom_data.h"
#include "../../lib/Common/system_state.h"


enum class NextionPage: byte {
  /* 00 */ Home,
  /* 01 */ Stages,           // stage list (was the preinfusion page)
  /* 02 */ UnusedBrewSoak,   // page removed
  /* 03 */ UnusedBrewProfiling, // page removed
  /* 04 */ BrewManual,
  /* 05 */ Flush,
  /* 06 */ Descale,
  /* 07 */ SettingsBoiler,
  /* 08 */ SettingsSystem,
  /* 09 */ BrewGraph,
  /* 0A */ BrewMore,
  /* 0B */ ShotSettings,
  /* 0C */ UnusedBrewTransitionProfile, // page removed
  /* 0D */ GraphPreview,
  /* 0E */ KeyboardNumeric,
  /* 0F */ Led,
  /* 10 */ StageTarget,
  /* 11 */ StageExit,
  /* 12 */ Calibration
};

extern volatile NextionPage lcdCurrentPageId;
extern volatile NextionPage lcdLastCurrentPageId;

void lcdInit(void);
bool lcdCheckSerialInit(const char* expectedOutput, size_t expectedLen);
void lcdUploadProfile(eepromValues_t &eepromCurrentValues);
void lcdUploadCfg(eepromValues_t &eepromCurrentValues);
void uploadPageCfg(eepromValues_t &eepromCurrentValues, SystemState &sys);
void lcdListen(void);
void lcdWakeUp(void);

void lcdFetchCurrentProfile(eepromValues_t & settings);
void lcdFetchLed(eepromValues_t & settings);
void lcdFetchPage(eepromValues_t &settings, NextionPage page, int targetProfile);
uint8_t lcdGetSelectedProfile(void);
int lcdGetHomeScreenScalesEnabled(void);
int lcdGetSelectedOperationalMode(void);
int lcdGetManualFlowVol(void);
int lcdGetDescaleCycle(void);

void lcdSetDescaleCycle(int cycle);
void lcdSetPressure(float val);
void lcdSetTemperature(uint16_t val);
void lcdSetTemperatureDecimal(uint16_t val);
void lcdTargetState(int val);
void lcdSetWeight(float val);
void lcdSetFlow(int val);
void lcdSetUpTime(float val);
void lcdSetTankWaterLvl(uint16_t val);

void lcdShowDebug(int val1, int val2);
void lcdShowPopup(const char *msg);

void lcdBrewTimerStart(void);
void lcdBrewTimerStop(void);
void lcdSetBrewTimer(int seconds);
void lcdWarmupStateStop(void);

/* Stage screens */
void lcdShowStageList(const eepromValues_t::profile_t &profile, uint8_t firstRow, int selected, bool moveMode, const char *note);
void lcdShowStageEditor(const eepromValues_t::profile_t &profile, uint8_t index);
void lcdFetchStageEditor(stage_t &stage);
void lcdDrawStagePreview(const eepromValues_t::profile_t &profile);
void lcdGoToPage(const char *pageName);

/* Scales calibration screen */
void lcdShowCalibrationMessage(const char *message);
void lcdShowCalibrationLive(const char *text);
void lcdGetCalibrationWeights(float &weightX, float &weightY);
void lcdSetScalesFactors(int factor1, int factor2);
void lcdSetPumpZero(float pumpZero);

/* Triggers */
void lcdSaveSettingsTrigger(void);
void lcdScalesTareTrigger(void);
void lcdHomeScreenScalesTrigger(void);
void lcdBrewGraphScalesTareTrigger(void);
void lcdRefreshElementsTrigger(void);
void lcdQuickProfileSwitch(void);
void lcdSaveProfileTrigger(void);
void lcdResetSettingsTrigger(void);
void lcdLoadDefaultProfileTrigger(void);
void lcdStageProfileSwitchTrigger(int delta);
void lcdStageRowTrigger(uint8_t row);
void lcdStagePageTrigger(int delta);
void lcdStageAddTrigger(void);
void lcdStageDeleteTrigger(void);
void lcdStageSaveTrigger(void);
void lcdStageMoveModeTrigger(void);
void lcdStageEditStepTrigger(int delta);
void lcdStageEditDoneTrigger(void);
void lcdStageTypeTrigger(void);
void lcdStageCurveTrigger(void);
void lcdStageTareTrigger(bool atStart);
void lcdCalibrationNextTrigger(void);
void lcdCalibrationRestartTrigger(void);
void lcdCalibrationSaveTrigger(void);

#endif
