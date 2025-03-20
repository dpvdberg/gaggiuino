/* 09:32 15/03/2023 - change triggering comment */
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

enum StartupPhase {
  INITIAL,
  TARE_SCALES,
  MEASURE_START_WEIGHT,
  FILLING_BOILER,
  WAITING_FOR_FILL,
  FINISHED
};

struct SystemState {
  StartupPhase phase;
};

#endif
