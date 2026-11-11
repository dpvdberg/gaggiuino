/* 09:32 15/03/2023 - change triggering comment */
#pragma GCC optimize ("Ofast")
#if defined(DEBUG_ENABLED)
  #include "dbg.h"
#endif
#include "gaggiuino.h"

SimpleKalmanFilter smoothPressure(0.6f, 0.6f, 0.1f);
SimpleKalmanFilter smoothPumpFlow(0.1f, 0.1f, 0.01f);
SimpleKalmanFilter smoothScalesFlow(0.5f, 0.5f, 0.01f);
SimpleKalmanFilter smoothConsideredFlow(0.1f, 0.1f, 0.1f);

//default phases. Updated in updateProfilerPhases.
Profile profile;
PhaseProfiler phaseProfiler{profile};

PredictiveWeight predictiveWeight;

SensorState currentState;

OPERATION_MODES selectedOperationalMode;

eepromValues_t runningCfg;


LED led;
TOF tof;

void setup(void) {
  LOG_INIT();
  LOG_INFO("Gaggiuino (fw: %s) booting", AUTO_VERSION);

  // Various pins operation mode handling
  pinInit();
  LOG_INFO("Pin init");

  setBoilerOff();  // relayPin LOW
  setSteamValveRelayOff();
  setSteamBoilerRelayOff();
  LOG_INFO("Boiler turned off");

  //Pump
  setPumpOff();
  LOG_INFO("Pump turned off");

  // Valve
  closeValve();
  LOG_INFO("Valve closed");

  lcdInit();
  LOG_INFO("LCD Init");

#if defined(DEBUG_ENABLED)
  // Debug init if enabled
  dbgInit();
  LOG_INFO("DBG init");
#endif

  // Initialise comms library for talking to the ESP mcu
  espCommsInit();

  // Initialize LED
  led.begin();
  led.setColor(9u, 0u, 9u); // WHITE
  // Init the tof sensor
  tof.init(currentState);

  // Initialising the saved values or writing defaults if first start
  eepromInit();
  runningCfg = eepromGetCurrentValues();
  LOG_INFO("EEPROM Init");

  cpsInit(runningCfg);
  LOG_INFO("CPS Init");

  thermocoupleInit();
  LOG_INFO("Thermocouple Init");

  lcdUploadCfg(runningCfg);
  LOG_INFO("LCD cfg uploaded");

  adsInit();
  LOG_INFO("Pressure sensor init");

  // Scales handling
  scalesInit(runningCfg.scalesF1, runningCfg.scalesF2);
  LOG_INFO("Scales init");

  // Pump init
  pumpInit(runningCfg.powerLineFrequency, runningCfg.pumpFlowAtZero);
  LOG_INFO("Pump init");

  // The profiler keeps a pointer into profile.phases: never let the vector reallocate.
  profile.phases.reserve(MAX_STAGES + 1);

  stageScreens::init(stageScreens::Context{ &runningCfg, machineIsBrewing, updateProfilerPhases, tryEepromWrite });
  calibrationFlow::init(calibrationFlow::Context{ &runningCfg, &currentState, machineCanCalibrate });

  pageValuesRefresh();
  LOG_INFO("Setup sequence finished");

  // Change LED colour on setup exit.
  led.setColor(9u, 0u, 9u); // 64171

  iwdcInit();
}

//##############################################################################################################################
//############################################________________MAIN______________################################################
//##############################################################################################################################


//Main loop where all the logic is continuously run
void loop(void) {
  if (lcdCurrentPageId != lcdLastCurrentPageId) pageValuesRefresh();
  lcdListen();
  sensorsRead();
  machineUpdate();
  lcdRefresh();
  espCommsSendSensorData(currentState);
  watchdogReload();  // if the loop ever stalls, the MCU reboots
}

//##############################################################################################################################
//#############################################___________SENSORS_READ________##################################################
//##############################################################################################################################


static void sensorsRead(void) {
  sensorReadSwitches();
  espCommsReadData();
  sensorsReadTemperature();
  sensorsReadWeight();
  sensorsReadPressure();
  calculateWeightAndFlow();
  updateStartupTimer();
  readTankWaterLevel();
  doLed();
}

static void sensorReadSwitches(void) {
  currentState.brewSwitchState = brewState();
  currentState.steamSwitchState = steamState();
  currentState.hotWaterSwitchState = waterPinState() || (currentState.brewSwitchState && currentState.steamSwitchState); // use either an actual switch, or the GC/GCP switch combo
}

static void sensorsReadTemperature(void) {
  if (millis() > thermoTimer) {
    currentState.temperature = thermocoupleRead() - runningCfg.offsetTemp;
    thermoTimer = millis() + GET_KTYPE_READ_EVERY;
  }
}

static void sensorsReadWeight(void) {
  static uint8_t tareFailures = 0;
  uint32_t elapsedTime = millis() - scalesTimer;

  if (elapsedTime > GET_SCALES_READ_EVERY) {
    currentState.scalesPresent = scalesIsPresent();
    if (currentState.scalesPresent) {
      if (currentState.tarePending) {
        if (scalesTare() || ++tareFailures >= SCALES_TARE_MAX_RETRIES) {
          if (tareFailures >= SCALES_TARE_MAX_RETRIES) LOG_ERROR("Scales tare failed, continuing without it");
          tareFailures = 0;
          weightMeasurements.clear();
          weightMeasurements.add(Measurement{ .value = 0.f, .millis = millis() });
          weightFilter.reset(0.f);
          weightStepGuard.reset(0.f, millis());
          currentState.weight = 0.f;
          currentState.tarePending = false;
        }
      }

      Measurement measuredWeight = scalesGetWeight();

      // millis == 0 means the scales had no reading ready: skip it instead of using a fake 0 g.
      // While a tare is still pending the reading has the old offset, so skip it too.
      if (measuredWeight.millis != 0 && !currentState.tarePending) {
        // Median of 3 ignores single spikes but follows real steps within 2 readings.
        measuredWeight.value = weightFilter.add(measuredWeight.value);

        if (brewActive) {
          // A step faster than coffee can flow (cup placed/bumped, failed tare) moves the zero point.
          bool absorbed = false;
          measuredWeight.value = weightStepGuard.apply(measuredWeight.value, measuredWeight.millis, &absorbed);
          if (absorbed) LOG_INFO("Weight step ignored, shot weight now %.1f g", static_cast<double>(measuredWeight.value));
        }
        currentState.weight = measuredWeight.value;

        if (brewActive) {
          weightMeasurements.add(measuredWeight);
          currentState.shotWeight = currentState.weight;
          currentState.weightFlow = fmax(0.f, weightMeasurements.measurementChange().changeSpeed());
          currentState.smoothedWeightFlow = smoothScalesFlow.updateEstimate(currentState.weightFlow);
        }
      }
    }
    scalesTimer = millis();
  }
}

static void sensorsReadPressure(void) {
  uint32_t elapsedTime = millis() - pressureTimer;

  if (elapsedTime > GET_PRESSURE_READ_EVERY) {
    float elapsedTimeSec = elapsedTime / 1000.f;
    currentState.pressure = getPressure();
    previousSmoothedPressure = currentState.smoothedPressure;
    currentState.smoothedPressure = smoothPressure.updateEstimate(currentState.pressure);
    currentState.pressureChangeSpeed = (currentState.smoothedPressure - previousSmoothedPressure) / elapsedTimeSec;
    pressureTimer = millis();
  }
}

static long sensorsReadFlow(float elapsedTimeSec) {
  long pumpClicks = getAndResetClickCounter();
  currentState.pumpClicks = (float) pumpClicks / elapsedTimeSec;

  currentState.pumpFlow = getPumpFlow(currentState.pumpClicks, currentState.smoothedPressure);

  previousSmoothedPumpFlow = currentState.smoothedPumpFlow;
  // Some flow smoothing
  currentState.smoothedPumpFlow = smoothPumpFlow.updateEstimate(currentState.pumpFlow);
  currentState.pumpFlowChangeSpeed = (currentState.smoothedPumpFlow - previousSmoothedPumpFlow) / elapsedTimeSec;
  return pumpClicks;
}

static void calculateWeightAndFlow(void) {
  uint32_t elapsedTime = millis() - flowTimer;

  if (brewActive) {
    // Marking for tare in case smth has gone wrong and it has exited tare already.
    // if (currentState.weight < -.3f) currentState.tarePending = true;

    if (elapsedTime > REFRESH_FLOW_EVERY) {
      flowTimer = millis();
      float elapsedTimeSec = elapsedTime / 1000.f;
      long pumpClicks = sensorsReadFlow(elapsedTimeSec);
      float consideredFlow = currentState.smoothedPumpFlow * elapsedTimeSec;
      // Update predictive class with our current phase
      CurrentPhase& phase = phaseProfiler.getCurrentPhase();
      predictiveWeight.update(currentState, phase, stageHints);

      // Start the predictive weight calculations when conditions are true
      if (predictiveWeight.isOutputFlow() || currentState.weight > 0.4f) {
        float flowPerClick = getPumpFlowPerClick(currentState.smoothedPressure);
        float actualFlow = (consideredFlow > pumpClicks * flowPerClick) ? consideredFlow : pumpClicks * flowPerClick;
        /* Probabilistically the flow is lower if the shot is just started winding up and we're flow profiling,
        once pressure stabilises around the setpoint the flow is either stable or puck restriction is high af. */
        if (phase.getType() == PHASE_TYPE::PHASE_TYPE_FLOW && currentState.pressureChangeSpeed > 0.15f
          && currentState.smoothedPressure < phase.getTarget() * 0.9f) {
          actualFlow *= 0.3f;
        }
        currentState.consideredFlow = smoothConsideredFlow.updateEstimate(actualFlow);
        currentState.shotWeight = currentState.scalesPresent ? currentState.shotWeight : currentState.shotWeight + actualFlow;
      }
      currentState.waterPumped += consideredFlow;
    }
  } else {
    currentState.consideredFlow = 0.f;
    currentState.pumpClicks = getAndResetClickCounter();
    flowTimer = millis();
  }
}

// return the reading in mm of the tank water level.
static void readTankWaterLevel(void) {
  if (lcdCurrentPageId == NextionPage::Home) {
    // static uint32_t tof_timeout = millis();
    // if (millis() >= tof_timeout) {
    currentState.waterLvl = tof.readLvl();
      // tof_timeout = millis() + 500;
    // }
  }
}

//##############################################################################################################################
//############################################______PAGE_CHANGE_VALUES_REFRESH_____#############################################
//##############################################################################################################################
static void pageValuesRefresh() {
  // Read the page we're landing in: leaving keyboard page means a value could've changed in it
  if (lcdLastCurrentPageId == NextionPage::KeyboardNumeric) lcdFetchPage(runningCfg, lcdCurrentPageId, runningCfg.activeProfile);
  // Or maybe it's a page that needs constant polling
  else if (lcdLastCurrentPageId == NextionPage::Led) lcdFetchPage(runningCfg, lcdCurrentPageId, runningCfg.activeProfile);
  // Finally read the page we left, as it could've been changed in place (e.g. boolean toggles)
  else lcdFetchPage(runningCfg, lcdLastCurrentPageId, runningCfg.activeProfile);

  if (lcdCurrentPageId != lcdLastCurrentPageId) {
    stageScreens::pageChanged(lcdLastCurrentPageId, lcdCurrentPageId);
    calibrationFlow::pageChanged(lcdLastCurrentPageId, lcdCurrentPageId);
  }

  homeScreenScalesEnabled = lcdGetHomeScreenScalesEnabled();
  // MODE_SELECT should always be LAST
  selectedOperationalMode = (OPERATION_MODES) lcdGetSelectedOperationalMode();

  updateProfilerPhases();

  lcdLastCurrentPageId = lcdCurrentPageId;
}

//#############################################################################################
//################################____LCD_REFRESH_CONTROL___###################################
//#############################################################################################

static void lcdRefresh(void) {
  uint16_t tempDecimal;

  if (millis() > pageRefreshTimer) {
    /*LCD pressure output, as a measure to beautify the graphs locking the live pressure read for the LCD alone*/
    #ifdef BEAUTIFY_GRAPH
      lcdSetPressure(currentState.smoothedPressure * 10.f);
    #else
      lcdSetPressure(
        currentState.pressure > 0.f
          ? currentState.pressure * 10.f
          : 0.f
      );
    #endif

    /*LCD temp output*/
    float brewTempSetPoint = ACTIVE_PROFILE(runningCfg).setpoint + runningCfg.offsetTemp;
    // float liveTempWithOffset = currentState.temperature - runningCfg.offsetTemp;
    currentState.waterTemperature = (currentState.temperature > (float)ACTIVE_PROFILE(runningCfg).setpoint && currentState.brewSwitchState)
      ? currentState.temperature / (float)brewTempSetPoint + (float)ACTIVE_PROFILE(runningCfg).setpoint
      : currentState.temperature;

    lcdSetTemperature(std::floor((uint16_t)currentState.waterTemperature));

    /*LCD weight & temp & water lvl output*/
    switch (lcdCurrentPageId) {
      case NextionPage::Home:
        // temp decimal handling
        tempDecimal = (currentState.waterTemperature - (uint16_t)currentState.waterTemperature) * 10;
        lcdSetTemperatureDecimal(tempDecimal);
        // water lvl
        lcdSetTankWaterLvl(currentState.waterLvl);
        //weight
        if (homeScreenScalesEnabled) lcdSetWeight(currentState.weight);
        break;
      case NextionPage::Calibration:
        calibrationFlow::liveUpdate();
        break;
      case NextionPage::BrewGraph:
      case NextionPage::BrewManual:
        // temp decimal handling
        tempDecimal = (currentState.waterTemperature - (uint16_t)currentState.waterTemperature) * 10;
        lcdSetTemperatureDecimal(tempDecimal);
        // If the weight output is a negative value lower than -0.8 you might want to tare again before extraction starts.
        if (currentState.shotWeight) lcdSetWeight(currentState.shotWeight > -0.8f ? currentState.shotWeight : -0.9f);
        /*LCD flow output*/
        lcdSetFlow( currentState.smoothedPumpFlow * 10.f);
        break;
      default:
        break; // don't push needless data on other pages
    }

  #ifdef DEBUG_ENABLED
    lcdShowDebug(readTempSensor(), getAdsError());
  #endif

    /*LCD timer and warmup*/
    if (brewActive) {
      lcdSetBrewTimer((millis() > brewingTimer) ? (int)((millis() - brewingTimer) / 1000) : 0);
      lcdBrewTimerStart(); // nextion timer start
      lcdWarmupStateStop(); // Flagging warmup notification on Nextion needs to stop (if enabled)
    } else {
      lcdBrewTimerStop(); // nextion timer stop
    }

    pageRefreshTimer = millis() + REFRESH_SCREEN_EVERY;
  }
}
//#############################################################################################
//###################################____SAVE_BUTTON____#######################################
//#############################################################################################
void tryEepromWrite(const eepromValues_t &eepromValues) {
  bool success = eepromWrite(eepromValues);
  watchdogReload(); // reload the watchdog timer on expensive operations
  if (success) {
    lcdShowPopup("Update successful!");
  } else {
    lcdShowPopup("Data out of range!");
  }
}

static bool refuseProfileChangeWhileBrewing(void) {
  if (!machineIsBrewing()) return false;
  lcdShowPopup("Not while brewing");
  return true;
}

void lcdSwitchActiveToStoredProfile(const eepromValues_t & storedSettings) {
  runningCfg.activeProfile = lcdGetSelectedProfile();
  ACTIVE_PROFILE(runningCfg) = storedSettings.profiles[runningCfg.activeProfile];
  stageScreens::reset();
  updateProfilerPhases();
  lcdUploadProfile(runningCfg);
}

// Save the desired temp values to EEPROM
void lcdSaveSettingsTrigger(void) {
  LOG_VERBOSE("Saving values to EEPROM");

  eepromValues_t eepromCurrentValues = eepromGetCurrentValues();
  lcdFetchPage(eepromCurrentValues, lcdCurrentPageId, runningCfg.activeProfile);
  tryEepromWrite(eepromCurrentValues);
}

void lcdSaveProfileTrigger(void) {
  LOG_VERBOSE("Saving profile to EEPROM");

  eepromValues_t eepromCurrentValues = eepromGetCurrentValues();
  lcdFetchCurrentProfile(eepromCurrentValues);
  eepromValues_t::profile_t &target = eepromCurrentValues.profiles[eepromCurrentValues.activeProfile];
  const eepromValues_t::profile_t &edited = runningCfg.profiles[eepromCurrentValues.activeProfile];
  target.stageCount = edited.stageCount;
  memcpy(target.stages, edited.stages, sizeof(target.stages));
  tryEepromWrite(eepromCurrentValues);
}

void lcdResetSettingsTrigger(void) {
  tryEepromWrite(eepromGetDefaultValues());
}

void lcdLoadDefaultProfileTrigger(void) {
  if (refuseProfileChangeWhileBrewing()) return;
  lcdSwitchActiveToStoredProfile(eepromGetDefaultValues());

  lcdShowPopup("Profile loaded!");
}

void lcdScalesTareTrigger(void) {
  LOG_VERBOSE("Tare scales");
  if (currentState.scalesPresent) currentState.tarePending = true;
}

void lcdHomeScreenScalesTrigger(void) {
  LOG_VERBOSE("Scales enabled or disabled");
  homeScreenScalesEnabled = lcdGetHomeScreenScalesEnabled();
}

void lcdBrewGraphScalesTareTrigger(void) {
  LOG_VERBOSE("Predictive scales tare action completed!");
  if (currentState.scalesPresent) {
    currentState.tarePending = true;
  }
  else {
    currentState.shotWeight = 0.f;
    predictiveWeight.setIsForceStarted(true);
  }
}

void lcdRefreshElementsTrigger(void) {
  // Used by the old PI/profile pages to toggle pressure/flow; stages handle that with their own triggers.
  lcdUploadProfile(runningCfg);
  pageValuesRefresh();
}

void lcdQuickProfileSwitch(void) {
  if (refuseProfileChangeWhileBrewing()) return;
  lcdSwitchActiveToStoredProfile(eepromGetCurrentValues());
  lcdShowPopup("Profile switched!");
}

//#############################################################################################
//###############################____PROFILING_CONTROL____#####################################
//#############################################################################################
static void updateProfilerPhases(void) {
  // Never rebuild mid-shot: the profiler is walking these phases. Entering BREWING rebuilds.
  if (!machineIsBrewing()) rebuildProfilerPhases();
}

static void rebuildProfilerPhases(void) {
  float shotTarget = -1.f;

  if (ACTIVE_PROFILE(runningCfg).stopOnWeightState) {
    shotTarget = (ACTIVE_PROFILE(runningCfg).shotStopOnCustomWeight < 1.f)
      ? ACTIVE_PROFILE(runningCfg).shotDose * ACTIVE_PROFILE(runningCfg).shotPreset
      : ACTIVE_PROFILE(runningCfg).shotStopOnCustomWeight;
  }

  // Global stop conditions: stopOnWeight from nextion, plus a time and water safety net
  // so a scales fault can never keep the pump running.
  profile.globalStopConditions = GlobalStopConditions{
    .time = MAX_SHOT_DURATION_MS,
    .weight = shotTarget,
    .waterPumped = MAX_SHOT_WATER_ML,
  };

  stageHints = stages::buildPhases(ACTIVE_PROFILE(runningCfg), runningCfg.basketPrefill, profile);
}

void onProfileReceived(Profile& newProfile) {
}

//#############################################################################################
//###################################____MACHINE STATE____#####################################
//#############################################################################################
// One explicit machine state (see functional/machine_state.h). Every loop: gather the inputs,
// pick the next state, run exit/entry actions on a change, then drive the outputs for the
// current state. Nothing in here blocks.
static unsigned long getTimeSinceInit(void);
static bool isBoilerFillPhase(unsigned long elapsedTime);
static bool isSwitchOn(void);
static void brewParamsReset(void);

namespace machine {
  MachineState state = MachineState::STARTUP_FILL;
  uint32_t stateSince = 0;
  bool brewArmed = false;           // brew switch seen off since the last brew/flush/descale
  bool previousBrewSwitch = false;
  unsigned long healthTimer = 0;    // next time to look for left-over pressure (LEGO / single board)

  // Startup boiler fill
  using FillStep = StartupFillStep;
  FillStep fillStep = FillStep::WAIT_FOR_HOME;
  uint32_t fillStepSince = 0;
  uint32_t fillRisingSince = 0;
  float fillBaseline = 0.f;

  constexpr uint32_t FILL_SETTLE_MS = 400;      // after the tare: wait for fresh readings
  constexpr uint32_t FILL_CONFIRM_MS = 500;     // weight must stay up this long (pump vibration)
  constexpr uint32_t FILL_TIMEOUT_MS = 20000;
  constexpr uint32_t FILL_NO_SCALES_MS = 4000;  // fixed fill time without scales
  constexpr uint8_t  FILL_PUMP_POWER = 35;
}

static BrewMode brewModeFromScreen(OPERATION_MODES mode) {
  switch (mode) {
    case OPERATION_MODES::OPMODE_manual: return BrewMode::MANUAL;
    case OPERATION_MODES::OPMODE_flush: return BrewMode::FLUSH;
    case OPERATION_MODES::OPMODE_descale: return BrewMode::DESCALE;
    case OPERATION_MODES::OPMODE_steam: return BrewMode::STEAM;
    default: return BrewMode::PROFILE;  // all the old preinfusion/profile variants run the stage profile
  }
}

static bool anySwitchOn(void) {
  return currentState.brewSwitchState || currentState.steamSwitchState || currentState.hotWaterSwitchState;
}

static bool machineIsBrewing(void) {
  return machine::state == MachineState::BREWING || machine::state == MachineState::MANUAL_BREWING;
}

static bool machineCanCalibrate(void) {
  return machine::state == MachineState::IDLE && !anySwitchOn()
    && brewModeFromScreen(selectedOperationalMode) == BrewMode::PROFILE;
}

static bool onBrewPage(void) {
  return lcdCurrentPageId == NextionPage::BrewGraph || lcdCurrentPageId == NextionPage::BrewManual
    || lcdCurrentPageId == NextionPage::GraphPreview;
}

static bool temperatureValid(void) {
  float t = currentState.temperature;
  return !isnan(t) && t > 0.f && t < 170.f;
}

static bool pressureReleaseSupported(void) {
#if defined LEGO_VALVE_RELAY || defined SINGLE_BOARD
  return true;
#else
  return false;
#endif
}

static bool pressureLeftOver(void) {
  return currentState.smoothedPressure >= SYS_PRESSURE_IDLE && currentState.temperature < 100.f;
}

//----------------------------------------------------------------------//
//------------------------- Startup boiler fill ------------------------//
//----------------------------------------------------------------------//
static void startupFillFinish(void) {
  setPumpOff();
  closeValve();
  machine::fillStep = machine::FillStep::DONE;
}

static void startupFillEnterStep(StartupFillStep step) {
  machine::fillStep = step;
  machine::fillStepSince = millis();
}

// Pumps a little water into the boiler after power-on (LEGO / single board), measured on the
// scales: done when the cup has gained STARTUP_FILL_WEIGHT for a moment, compared to a fresh
// reading taken after the tare. Without scales it fills for a fixed time.
static void startupFillStep(void) {
  using machine::FillStep;
  if (!pressureReleaseSupported() || currentState.temperature > BOILER_FILL_SKIP_TEMP) {
    startupFillFinish();
    return;
  }

  uint32_t inStep = millis() - machine::fillStepSince;
  switch (machine::fillStep) {
    case FillStep::WAIT_FOR_HOME:
      if (isSwitchOn()) {
        lcdShowPopup("Brew Switch ON!");
      } else if (isBoilerFillPhase(getTimeSinceInit())) {
        startupFillEnterStep(currentState.scalesPresent ? FillStep::TARE : FillStep::FILLING);
        if (!currentState.scalesPresent) {
          openValve();
          setPumpToRawValue(machine::FILL_PUMP_POWER);
        }
      }
      break;

    case FillStep::TARE:
      currentState.tarePending = true;
      startupFillEnterStep(FillStep::BASELINE);
      break;

    case FillStep::BASELINE:
      // The tare clears tarePending before the first reading after it: wait for fresh readings.
      if (!currentState.tarePending && inStep >= machine::FILL_SETTLE_MS) {
        machine::fillBaseline = currentState.weight;
        machine::fillRisingSince = 0;
        openValve();
        setPumpToRawValue(machine::FILL_PUMP_POWER);
        startupFillEnterStep(FillStep::FILLING);
      } else if (inStep > machine::FILL_TIMEOUT_MS) {
        startupFillFinish();  // scales never finished the tare: skip the fill
      }
      break;

    case FillStep::FILLING:
      lcdShowPopup("Filling boiler!");
      openValve();
      setPumpToRawValue(machine::FILL_PUMP_POWER);
      if (!currentState.scalesPresent) {
        if (inStep >= machine::FILL_NO_SCALES_MS) startupFillFinish();
        break;
      }
      if (currentState.weight - machine::fillBaseline > STARTUP_FILL_WEIGHT) {
        if (machine::fillRisingSince == 0) machine::fillRisingSince = millis();
        else if (millis() - machine::fillRisingSince >= machine::FILL_CONFIRM_MS) startupFillFinish();
      } else {
        machine::fillRisingSince = 0;
      }
      if (machine::fillStep != FillStep::DONE && inStep > machine::FILL_TIMEOUT_MS) {
        startupFillFinish();
        lcdShowPopup("Fill timed out: check water");
      }
      break;

    case FillStep::DONE:
      break;
  }
}

//----------------------------------------------------------------------//
//--------------------------- Inputs & actions -------------------------//
//----------------------------------------------------------------------//
static MachineInputs gatherMachineInputs(void) {
  MachineInputs in;
  in.brewSwitch = currentState.brewSwitchState;
  in.steamSwitch = currentState.steamSwitchState;
  in.hotWaterSwitch = currentState.hotWaterSwitchState;
  in.brewArmed = machine::brewArmed;
  in.mode = brewModeFromScreen(selectedOperationalMode);

  in.startupDone = machine::fillStep == machine::FillStep::DONE;
  in.temperatureValid = temperatureValid();
  in.steamForgotten = currentState.isSteamForgottenON;
  in.readyToBrew = currentState.waterLvl >= MIN_WATER_LVL;

  in.shotFinished = phaseProfiler.isFinished();
  in.onHomePage = lcdCurrentPageId == NextionPage::Home;
  in.cleaningWanted = currentState.temperature <= CLEANING_SKIP_TEMP;
  in.cleaningDone = millis() - machine::stateSince > CLEANING_DURATION;

  in.pressureReleaseDue = pressureReleaseSupported() && millis() >= machine::healthTimer && pressureLeftOver();
  in.pressureReleased = !pressureLeftOver();

  in.calibrationRunning = calibrationFlow::pumpRunActive();
  return in;
}

static void machineExit(MachineState state) {
  switch (state) {
    case MachineState::STARTUP_FILL:
    case MachineState::BREWING:
    case MachineState::MANUAL_BREWING:
    case MachineState::CLEANING:
    case MachineState::HOT_WATER:
      setPumpOff();
      closeValve();
      break;
    case MachineState::FLUSH:
      flushDeactivated();
      break;
    case MachineState::DESCALE:
      setPumpOff();
      closeValve();
      setSteamValveRelayOff();
      descaleReset();
      // Descale lowers the brew temperature; restore the saved one.
      ACTIVE_PROFILE(runningCfg).setpoint = eepromGetCurrentValues().profiles[runningCfg.activeProfile].setpoint;
      break;
    case MachineState::STEAM:
      setPumpOff();
      setSteamValveRelayOff();
      setSteamBoilerRelayOff();
      break;
    case MachineState::CALIBRATING:
      calibrationFlow::abortPumpRun("Stopped: a switch was turned on.\rTurn it off and tap Next to retry.");
      setPumpOff();
      closeValve();
      break;
    case MachineState::PRESSURE_RELEASE:
      closeValve();
      machine::healthTimer = millis() + HEALTHCHECK_EVERY;
      break;
    default:
      break;
  }
}

static void machineEnter(MachineState state) {
  switch (state) {
    case MachineState::BREWING:
    case MachineState::MANUAL_BREWING:
      lcdWakeUp();
      rebuildProfilerPhases();
      brewParamsReset();
      machine::brewArmed = false;
      break;
    case MachineState::FLUSH:
      flushReset();
      machine::brewArmed = false;
      break;
    case MachineState::DESCALE:
      descaleReset();
      machine::brewArmed = false;
      break;
    case MachineState::IDLE:
    case MachineState::SHOT_DONE:
    case MachineState::FAULT_TEMP:
    case MachineState::FAULT_STEAM:
      setPumpOff();
      closeValve();
      break;
    default:
      break;
  }
}

//----------------------------------------------------------------------//
//------------------------------ Outputs -------------------------------//
//----------------------------------------------------------------------//
static void runProfile(void) {
  uint32_t timeInShot = millis() - brewingTimer;
  bool shouldTare = phaseProfiler.updatePhase(timeInShot, currentState);
  CurrentPhase& currentPhase = phaseProfiler.getCurrentPhase();
  ShotSnapshot shotSnapshot = buildShotSnapshot(timeInShot, currentState, currentPhase);
  espCommsSendShotData(shotSnapshot, 100);

  if (shouldTare) {
    currentState.tarePending = true;
  }

  if (phaseProfiler.isFinished()) {
    setPumpOff();
    closeValve();
  } else if (currentPhase.getType() == PHASE_TYPE::PHASE_TYPE_PRESSURE) {
    openValve();
    setPumpPressure(currentPhase.getTarget(), currentPhase.getRestriction(), currentState);
  } else {
    openValve();
    setPumpFlow(currentPhase.getTarget(), currentPhase.getRestriction(), currentState);
  }
  justDoCoffee(runningCfg, currentState, true);
}

static void runManualFlow(void) {
  openValve();
  setPumpFlow(lcdGetManualFlowVol() / 10.f, 0.f, currentState);
  justDoCoffee(runningCfg, currentState, true);
}

// Countdown before venting left-over pressure, as before.
static void showPressureReleaseCountdown(void) {
  if (!pressureReleaseSupported() || onBrewPage() || !pressureLeftOver()) return;
  unsigned long now = millis();
  if (now >= machine::healthTimer - 3500ul && now <= machine::healthTimer - 500ul) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "Dropping beats in: %i", static_cast<int>((machine::healthTimer - now) / 1000));
    lcdShowPopup(tmp);
  }
}

static void runMachineState(MachineState state, BrewMode mode) {
  bool steamControl = false;

  switch (state) {
    case MachineState::STARTUP_FILL:
      startupFillStep();  // heaters stay off until the boiler has water
      break;

    case MachineState::IDLE:
      setPumpOff();
      closeValve();
      if (mode == BrewMode::STEAM) {
        steamCtrl(runningCfg, currentState);  // the steam page keeps steam temperature
        steamControl = true;
      } else {
        justDoCoffee(runningCfg, currentState, false);
      }
      if (currentState.brewSwitchState && machine::brewArmed && currentState.waterLvl < MIN_WATER_LVL) {
        lcdShowPopup("Fill the water tank!");
      }
      showPressureReleaseCountdown();
      break;

    case MachineState::BREWING:
      runProfile();
      break;

    case MachineState::MANUAL_BREWING:
      runManualFlow();
      break;

    case MachineState::SHOT_DONE:
    case MachineState::WAIT_SWITCH_OFF:
      setPumpOff();
      closeValve();
      justDoCoffee(runningCfg, currentState, false);
      if (state == MachineState::WAIT_SWITCH_OFF && currentState.brewSwitchState) lcdShowPopup("Switch brew to clean");
      break;

    case MachineState::CLEANING:
      lcdShowPopup("Cleaning showerhead!");
      openValve();
      setPumpToRawValue(35);
      justDoCoffee(runningCfg, currentState, false);
      break;

    case MachineState::STEAM:
      steamCtrl(runningCfg, currentState);
      steamControl = true;
      break;

    case MachineState::HOT_WATER:
      hotWaterMode(currentState);
      break;

    case MachineState::FLUSH:
      if (mode == BrewMode::STEAM) {  // group purge from the steam page
        flushActivated();
        steamCtrl(runningCfg, currentState);
        steamControl = true;
      } else {
        backFlush(currentState);
        setBoilerOff();
      }
      break;

    case MachineState::DESCALE:
      deScale(runningCfg, currentState);
      break;

    case MachineState::PRESSURE_RELEASE:
      setPumpOff();
      if (onBrewPage()) {
        // Don't interrupt the shot graph; just wait for the pressure to drop.
        justDoCoffee(runningCfg, currentState, false);
      } else {
        lcdShowPopup("Releasing pressure!");
        setBoilerOff();
        setSteamValveRelayOff();
        setSteamBoilerRelayOff();
        openValve();
      }
      break;

    case MachineState::CALIBRATING:
      calibrationFlow::runPump();
      justDoCoffee(runningCfg, currentState, false);
      break;

    case MachineState::FAULT_TEMP:
      setPumpOff();
      setBoilerOff();
      setSteamBoilerRelayOff();
      lcdShowPopup(currentState.steamSwitchState ? "COOLDOWN" : "TEMP READ ERROR");
      break;

    case MachineState::FAULT_STEAM:
      setPumpOff();
      setBoilerOff();
      setSteamBoilerRelayOff();
      lcdShowPopup("TURN STEAM OFF NOW!");
      if (!currentState.steamSwitchState) currentState.isSteamForgottenON = false;
      break;
  }

  // The steam-forgotten timer only runs while steam control is active.
  if (!steamControl) steamTime = millis();
}

static void machineUpdate(void) {
  // Brew switch edges: a fresh press is needed to start something; releasing it resets the shot data.
  bool brewSwitch = currentState.brewSwitchState;
  if (!brewSwitch) machine::brewArmed = true;
  if (machine::previousBrewSwitch && !brewSwitch) brewParamsReset();
  machine::previousBrewSwitch = brewSwitch;

  MachineInputs in = gatherMachineInputs();
  MachineState next = nextMachineState(machine::state, in);
  if (next != machine::state) {
    LOG_INFO("Machine: %s -> %s", machineStateName(machine::state), machineStateName(next));
    machineExit(machine::state);
    machine::state = next;
    machine::stateSince = millis();
    machineEnter(next);
  }

  // Look for left-over pressure only while idle with every switch off.
  if (machine::state != MachineState::IDLE || anySwitchOn()) {
    machine::healthTimer = millis() + HEALTHCHECK_EVERY;
  } else if (millis() >= machine::healthTimer && !pressureLeftOver()) {
    machine::healthTimer = millis() + HEALTHCHECK_EVERY;
  }

  brewActive = isBrewSession(machine::state);
  runMachineState(machine::state, in.mode);
}

static void brewParamsReset(void) {
  currentState.tarePending = true;
  currentState.shotWeight  = 0.f;
  currentState.pumpFlow    = 0.f;
  currentState.weight      = 0.f;
  currentState.waterPumped = 0.f;
  brewingTimer             = millis();
  flowTimer                = brewingTimer;

  weightMeasurements.clear();
  predictiveWeight.reset();
  phaseProfiler.reset();
}

// Function to track time since system has started
static unsigned long getTimeSinceInit(void) {
  static unsigned long startTime = millis();
  return millis() - startTime;
}

static bool isBoilerFillPhase(unsigned long elapsedTime) {
  return lcdCurrentPageId == NextionPage::Home && elapsedTime >= BOILER_FILL_START_TIME;
}

// Checks if Brew switch is ON
static bool isSwitchOn(void) {
  return currentState.brewSwitchState && lcdCurrentPageId == NextionPage::Home;
}

static void updateStartupTimer(void) {
  lcdSetUpTime(getTimeSinceInit() / 1000);
}

static void cpsInit(eepromValues_t &eepromValues) {
  int cps = getCPS();
  if (cps > 110) { // double 60 Hz
    eepromValues.powerLineFrequency = 60u;
  } else if (cps > 80) { // double 50 Hz
    eepromValues.powerLineFrequency = 50u;
  } else if (cps > 55) { // 60 Hz
    eepromValues.powerLineFrequency = 60u;
  } else if (cps > 0) { // 50 Hz
    eepromValues.powerLineFrequency = 50u;
  }
}

static void doLed(void) {
  if (runningCfg.ledDisco && brewActive) {
    switch(lcdCurrentPageId) {
      case NextionPage::BrewGraph:
      case NextionPage::BrewManual:
        led.setDisco(led.CLASSIC);
        break;
      case NextionPage::Flush:
        led.setDisco(led.STROBE);
        break;
      case NextionPage::Descale:
        led.setDisco(led.DESCALE);
        break;
      default:
        led.setColor(0, 0, 0);
        break;
    }
  } else {
    switch(lcdCurrentPageId) {
      case NextionPage::Led:
        static uint32_t timer = millis();
        if (millis() > timer) {
          timer = millis() + 100u;
          lcdFetchLed(runningCfg);
        }
      default: // intentionally fall through
        led.setColor(runningCfg.ledR, runningCfg.ledG, runningCfg.ledB);
    }
  }
}
