/* 09:32 15/03/2023 - change triggering comment */
#ifndef EEPROM_DATA_H
#define EEPROM_DATA_H

#include <Arduino.h>

/**
* NOTE: changing these constants requires:
*        - bumping the version number! the serialized data will have
*          a different layout to accommodate more array elements
*          or longer strings
*        - hardcoding the current value in the archived version and
*          its upgrade function, to ensure loading that version
*          deserializes correctly
*/
#define MAX_PROFILES 5
#define PROFILE_NAME_LENGTH 25
#define MAX_STAGES 12

/**
* current data version definition below
*
* changing the schema requires bumping version number. this forces a schema update
* on boards with a lower version after flash. to make this graceful for users:
* - archive the current version in its corresponding header in legacy/
* - in that header, make sure to register the upgrade function for
*   the loader to pick it up
* - bump up the version in eeprom_metadata.h
* - make schema changes
*
* adding fields to data below shouldn't require changes to legacy upgrade
* functions - they just won't populate the new field, and it will use
* default value.
*
* removing fields might require deleting assignments in existing legacy
* functions that reference them. this will pop up as a compile time failure
*/

/**
* A stage is one step of a brew profile: drive the pump to a pressure or flow target
* (optionally ramping from start to end) until one of the exit conditions is met.
* A value of 0 means "off" / "not set".
*/
enum class StageType : uint8_t { PRESSURE = 0, FLOW = 1 };
enum class StageCurve : uint8_t { LINEAR = 0, EASE_IN = 1, EASE_OUT = 2, EASE_IN_OUT = 3 };

struct stage_t {
  StageType  type;
  StageCurve curve;
  bool       tareAtStart;
  bool       tareAtEnd;
  float      start;              // bar or ml/s; 0 = start from the current value
  float      end;                // bar or ml/s
  float      rampTime;           // seconds to go from start to end; 0 = jump straight to end
  float      limit;              // max flow (pressure stage) or max pressure (flow stage); 0 = none
  float      exitTime;           // seconds in this stage
  float      exitPressureAbove;  // bar
  float      exitPressureBelow;  // bar
  float      exitWeight;         // grams in the cup
};

/**
* Version 13:
* - Brew profiles are a list of stages instead of fixed preinfusion/soak/profile sections
*/
struct eepromValues_t {
  uint16_t steamSetPoint;
  uint16_t offsetTemp;
  uint16_t hpwr;
  uint16_t mainDivider;
  uint16_t brewDivider;
  uint8_t activeProfile;
  struct profile_t {
    char     name[PROFILE_NAME_LENGTH];
    uint8_t  stageCount;  // 1..MAX_STAGES
    stage_t  stages[MAX_STAGES];
    /*-----------OTHER---------------------*/
    uint16_t setpoint;
    bool     stopOnWeightState;
    float    shotDose;
    float    shotStopOnCustomWeight;
    uint16_t shotPreset;
  } profiles[MAX_PROFILES];
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

void eepromInit(void);
bool eepromWrite(eepromValues_t);
struct eepromValues_t eepromGetDefaultValues(void);
struct eepromValues_t eepromGetCurrentValues(void);

#define ACTIVE_PROFILE(eepromValues) eepromValues.profiles[eepromValues.activeProfile]

#endif
