#ifndef DEFAULT_PROFILES_H
#define DEFAULT_PROFILES_H

#include <Arduino.h>
#include "eeprom_data.h"

namespace {
  typedef struct {
    char     name[PROFILE_NAME_LENGTH];
    uint8_t  stageCount;
    stage_t  stages[MAX_STAGES];
    uint16_t setpoint;
    bool     stopOnWeightState;
    float    shotDose;
    float    shotStopOnCustomWeight;
    uint16_t shotPreset;
  } profileDefaults_t;

  constexpr StageType P = StageType::PRESSURE;
  constexpr StageType F = StageType::FLOW;
  constexpr StageCurve LIN = StageCurve::LINEAR;
  constexpr StageCurve E_IN = StageCurve::EASE_IN;
  constexpr StageCurve E_OUT = StageCurve::EASE_OUT;

  // Stage fields, in order:
  // type, curve, tareAtStart, tareAtEnd, start, end, rampTime, limit, exitTime, exitPressureAbove, exitPressureBelow, exitWeight
  const profileDefaults_t defaultsProfile[MAX_PROFILES] = {
    { "PI-Soak-9-6", 5, {
        { F, LIN,   false, false, 0.f, 4.f, 0.f,  3.f, 15.f, 3.f, 0.f, 0.f },  // preinfuse 4 ml/s until 3 bar
        { P, LIN,   false, false, 0.f, 0.f, 0.f,  0.f,  5.f, 0.f, 0.f, 0.f },  // soak, pump off, 5 s
        { P, LIN,   false, false, 0.f, 9.f, 3.f,  0.f,  3.f, 0.f, 0.f, 0.f },  // ramp to 9 bar over 3 s
        { P, LIN,   false, false, 9.f, 9.f, 0.f,  0.f, 10.f, 0.f, 0.f, 0.f },  // hold 9 bar 10 s
        { P, LIN,   false, false, 6.f, 6.f, 0.f,  0.f,  0.f, 0.f, 0.f, 0.f },  // 6 bar until target weight
      }, 93, true, 18.f, 0.f, 2 },
    { "Londinium", 4, {
        { F, LIN,   false, false, 0.f, 9.f, 0.f,  4.f, 10.f, 4.f, 0.f, 0.f },
        { P, LIN,   false, false, 0.f, 0.f, 0.f,  0.f, 15.f, 0.f, 0.7f, 0.f },
        { P, E_IN,  false, false, 0.f, 9.f, 1.f,  0.f,  5.f, 0.f, 0.f, 0.f },
        { P, LIN,   false, false, 9.f, 3.f, 20.f, 3.f,  0.f, 0.f, 0.f, 0.f },
      }, 92, true, 20.f, 0.f, 2 },
    { "Blooming espresso", 4, {
        { F, LIN,   false, false, 0.f, 4.f, 0.f,  7.f, 20.f, 7.f, 0.f, 0.f },
        { P, LIN,   false, false, 0.f, 0.f, 0.f,  0.f, 30.f, 0.f, 0.6f, 5.f },
        { F, E_OUT, false, false, 0.f, 2.f, 5.f,  9.f,  5.f, 0.f, 0.f, 0.f },
        { F, LIN,   false, false, 2.f, 2.f, 0.f,  6.f,  0.f, 0.f, 0.f, 0.f },
      }, 93, true, 18.f, 0.f, 2 },
    { "Filter 2.1", 3, {
        { F, LIN,   false, false, 0.f, 4.f, 0.f,  1.f, 15.f, 0.f, 0.f, 0.f },
        { F, LIN,   false, false, 0.2f, 0.2f, 0.f, 0.f, 90.f, 0.f, 0.f, 45.f },
        { F, LIN,   false, false, 3.f, 3.f, 0.f,  9.f,  0.f, 0.f, 0.f, 0.f },
      }, 89, true, 18.f, 0.f, 2 },
    { "Flat 9 bar", 1, {
        { P, LIN,   false, false, 9.f, 9.f, 0.f,  0.f,  0.f, 0.f, 0.f, 0.f },
      }, 93, true, 18.f, 0.f, 2 },
  };

}

#endif
