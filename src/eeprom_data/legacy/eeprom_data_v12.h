#ifndef EEPROM_DATA_V12_H
#define EEPROM_DATA_V12_H

#include "../eeprom_data.h"
#include "../eeprom_metadata.h"

/**
* Version 12:
* - Multiple brew profiles with fixed preinfusion / soak / transition / profile sections.
*
* Upgrading to stage based profiles: machine settings and per-profile name, temperature
* and dose settings are kept. The stages themselves are reset to the defaults.
*/
struct eepromValues_t_v12 {
  uint16_t steamSetPoint;
  uint16_t offsetTemp;
  uint16_t hpwr;
  uint16_t mainDivider;
  uint16_t brewDivider;
  uint8_t activeProfile;
  struct profile_t_v12 {
    char     name[25];
    // Preinfusion vars section
    bool     preinfusionState;
    bool     preinfusionFlowState;
    uint16_t preinfusionSec;
    float    preinfusionBar;
    float    preinfusionFlowVol;
    uint16_t preinfusionFlowTime;
    float    preinfusionFlowPressureTarget;
    float    preinfusionPressureFlowTarget;
    float    preinfusionFilled;
    bool     preinfusionPressureAbove;
    float    preinfusionWeightAbove;
    // Soak vars section
    bool     soakState;
    uint16_t soakTimePressure;
    uint16_t soakTimeFlow;
    float    soakKeepPressure;
    float    soakKeepFlow;
    float    soakBelowPressure;
    float    soakAbovePressure;
    float    soakAboveWeight;
    // PI -> PF ramp settings
    uint16_t preinfusionRamp;
    uint16_t preinfusionRampSlope;
    // Profiling vars section
    bool     tpState;
    bool     tpType;
    float    tpProfilingStart;
    float    tpProfilingFinish;
    uint16_t tpProfilingHold;
    float    tpProfilingHoldLimit;
    uint16_t tpProfilingSlope;
    uint16_t tpProfilingSlopeShape;
    float    tpProfilingFlowRestriction;
    float    tfProfileStart;
    float    tfProfileEnd;
    uint16_t tfProfileHold;
    float    tfProfileHoldLimit;
    uint16_t tfProfileSlope;
    uint16_t tfProfileSlopeShape;
    float    tfProfilingPressureRestriction;
    bool     profilingState;
    bool     mfProfileState;
    float    mpProfilingStart;
    float    mpProfilingFinish;
    uint16_t mpProfilingSlope;
    uint16_t mpProfilingSlopeShape;
    float    mpProfilingFlowRestriction;
    float    mfProfileStart;
    float    mfProfileEnd;
    uint16_t mfProfileSlope;
    uint16_t mfProfileSlopeShape;
    float    mfProfilingPressureRestriction;
    /*-----------OTHER---------------------*/
    uint16_t setpoint;
    bool     stopOnWeightState;
    float    shotDose;
    float    shotStopOnCustomWeight;
    uint16_t shotPreset;
  } profiles[5];
  // Settings vars section
  uint16_t powerLineFrequency;
  uint16_t lcdSleep;
  bool     warmupState;
  bool     homeOnShotFinish;
  bool     brewDeltaState;
  bool     basketPrefill;
  int      scalesF1;
  int      scalesF2;
  float    pumpFlowAtZero;
  bool     ledState;
  bool     ledDisco;
  uint8_t  ledR;
  uint8_t  ledG;
  uint8_t  ledB;
};

static bool upgradeSchema_v12(eepromValues_t &targetValues, eepromValues_t_v12 &loadedValues) {
  targetValues.steamSetPoint = loadedValues.steamSetPoint;
  targetValues.offsetTemp = loadedValues.offsetTemp;
  targetValues.hpwr = loadedValues.hpwr;
  targetValues.mainDivider = loadedValues.mainDivider;
  targetValues.brewDivider = loadedValues.brewDivider;
  targetValues.activeProfile = loadedValues.activeProfile < MAX_PROFILES ? loadedValues.activeProfile : 0;

  for (int i = 0; i < 5 && i < MAX_PROFILES; i++) {
    snprintf(targetValues.profiles[i].name, PROFILE_NAME_LENGTH, "%s", loadedValues.profiles[i].name);
    targetValues.profiles[i].setpoint = loadedValues.profiles[i].setpoint;
    targetValues.profiles[i].stopOnWeightState = loadedValues.profiles[i].stopOnWeightState;
    targetValues.profiles[i].shotDose = loadedValues.profiles[i].shotDose;
    targetValues.profiles[i].shotStopOnCustomWeight = loadedValues.profiles[i].shotStopOnCustomWeight;
    targetValues.profiles[i].shotPreset = loadedValues.profiles[i].shotPreset;
  }

  targetValues.powerLineFrequency = loadedValues.powerLineFrequency;
  targetValues.lcdSleep = loadedValues.lcdSleep;
  targetValues.warmupState = loadedValues.warmupState;
  targetValues.homeOnShotFinish = loadedValues.homeOnShotFinish;
  targetValues.brewDeltaState = loadedValues.brewDeltaState;
  targetValues.basketPrefill = loadedValues.basketPrefill;
  targetValues.scalesF1 = loadedValues.scalesF1;
  targetValues.scalesF2 = loadedValues.scalesF2;
  targetValues.pumpFlowAtZero = loadedValues.pumpFlowAtZero;
  targetValues.ledState = loadedValues.ledState;
  targetValues.ledDisco = loadedValues.ledDisco;
  targetValues.ledR = loadedValues.ledR;
  targetValues.ledG = loadedValues.ledG;
  targetValues.ledB = loadedValues.ledB;
  return true;
}

REGISTER_LEGACY_EEPROM_DATA(12, eepromValues_t_v12, upgradeSchema_v12)

#endif
