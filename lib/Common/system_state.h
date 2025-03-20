/* 09:32 15/03/2023 - change triggering comment */
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

enum InitPhase {
  INITIAL,
  TARE_SCALES,
  MEASURE_START_WEIGHT,
  FILLING_BOILER,
  WAITING_FOR_FILL,
  FINISHED
};

enum PostShotPhase {
  IDLE,
  POST_SHOT,
  WAITING_FOR_BREW_BUTTON_RELEASE,
  START_CLEAN_SHOWERHEAD,
  STOP_CLEAN_SHOWERHEAD
};


struct SystemState {
  InitPhase initPhase;
  PostShotPhase postShotPhase;
};

#endif
