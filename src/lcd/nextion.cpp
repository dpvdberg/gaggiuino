/* 09:32 15/03/2023 - change triggering comment */
#include "lcd.h"
#include "pindef.h"
#include "log.h"
#include "functional/stages.h"
#include "peripherals/internal_watchdog.h"
#include <Arduino.h>

EasyNex myNex(USART_LCD);
volatile NextionPage lcdCurrentPageId;
volatile NextionPage lcdLastCurrentPageId;

// decode/encode bit packing.
// format is 000000sd rrrrrrrr gggggggg bbbbbbbb, where s = state, d = disco, r/g/b = colors
void lcdDecodeLedSettings(uint32_t code, bool &state, bool &disco, uint8_t &r, uint8_t &g, uint8_t &b) {
  state = (code & 0x02000000);
  disco = (code & 0x01000000);
  r     = (code & 0x00FF0000) >> 16;
  g     = (code & 0x0000FF00) >> 8;
  b     = (code & 0x000000FF);
}

uint32_t lcdEncodeLedSettings(bool state, bool disco, uint8_t r, uint8_t g, uint8_t b) {
  uint32_t code;
  code = state ? 0x01 : 0x00;
  code = (code << 1) | (disco ? 0x01 : 0x00);
  code = (code << 8) | (r & 0xFF);
  code = (code << 8) | (g & 0xFF);
  code = (code << 8) | (b & 0xFF);
  return code;
}

void lcdInit(void) {
  myNex.begin(115200);
  while (!lcdCheckSerialInit("\x88\xFF\xFF\xFF", 4)) {
    LOG_VERBOSE("Connecting to Nextion LCD...");
    delay(5);
  }
  myNex.writeStr("splash.build_version.txt", AUTO_VERSION);
  lcdCurrentPageId = static_cast<NextionPage>(myNex.currentPageId);
  lcdLastCurrentPageId = static_cast<NextionPage>(myNex.currentPageId);
}

bool lcdCheckSerialInit(const char* expectedOutput, size_t expectedLen) {
  size_t receivedLen = 0;

  while (receivedLen < expectedLen) {
    int receivedByte = myNex.readByte();
    if (receivedByte != -1) {
      if (receivedByte == expectedOutput[receivedLen]) {
        receivedLen++;
      } else {
        // Reset receivedLen if the received byte doesn't match
        receivedLen = 0;
      }
    }
  }

  // Serial output matches expected output
  return true;
}

void lcdListen(void) {
  myNex.NextionListen();
  lcdCurrentPageId = static_cast<NextionPage>(myNex.currentPageId);
}

void lcdWakeUp(void) {
  myNex.writeNum("sleep", 0);
}

void lcdUploadProfile(eepromValues_t &eepromCurrentValues) {
  // Highlight the active profile
  myNex.writeNum("pId", eepromCurrentValues.activeProfile + 1  /* 1-offset in nextion */);
  String buttonElemId = String("home.qPf") + (eepromCurrentValues.activeProfile + 1) + ".txt";
  myNex.writeStr(buttonElemId, ACTIVE_PROFILE(eepromCurrentValues).name);

  // Temp
  myNex.writeNum("sT.setPoint.val", ACTIVE_PROFILE(eepromCurrentValues).setpoint);

  // The screen still derives a brew mode from these old flags. With stages every brew
  // mode runs the stage profile, so pin them to "preinfusion + pressure profile".
  myNex.writeNum("piState", 1);
  myNex.writeNum("piFlowState", 0);
  myNex.writeNum("ppState", 1);
  myNex.writeNum("ppType", 0);

  // Dose settings
  myNex.writeNum("shotState", ACTIVE_PROFILE(eepromCurrentValues).stopOnWeightState);
  myNex.writeNum("dS.numDose.val", ACTIVE_PROFILE(eepromCurrentValues).shotDose * 10.f);
  myNex.writeNum("shotPreset", ACTIVE_PROFILE(eepromCurrentValues).shotPreset);
  myNex.writeNum("dS.numDoseForced.val", ACTIVE_PROFILE(eepromCurrentValues).shotStopOnCustomWeight * 10.f);
}

// This is never called again after boot
void lcdUploadCfg(eepromValues_t &eepromCurrentValues) {
  // Profile names for all buttons
  myNex.writeStr("home.qPf1.txt", eepromCurrentValues.profiles[0].name);
  myNex.writeStr("home.qPf2.txt", eepromCurrentValues.profiles[1].name);
  myNex.writeStr("home.qPf3.txt", eepromCurrentValues.profiles[2].name);
  myNex.writeStr("home.qPf4.txt", eepromCurrentValues.profiles[3].name);
  myNex.writeStr("home.qPf5.txt", eepromCurrentValues.profiles[4].name);

  // More brew settings
  myNex.writeNum("bckHome", eepromCurrentValues.homeOnShotFinish);
  myNex.writeNum("basketPrefill", eepromCurrentValues.basketPrefill);
  myNex.writeNum("deltaState", eepromCurrentValues.brewDeltaState);

  // System settings
  myNex.writeNum("sT.steamSetPoint.val", eepromCurrentValues.steamSetPoint);
  myNex.writeNum("sT.offSet.val", eepromCurrentValues.offsetTemp);
  myNex.writeNum("sT.hpwr.val", eepromCurrentValues.hpwr);
  myNex.writeNum("sT.mDiv.val", eepromCurrentValues.mainDivider);
  myNex.writeNum("sT.bDiv.val", eepromCurrentValues.brewDivider);

  myNex.writeNum("sP.n1.val", eepromCurrentValues.lcdSleep);
  myNex.writeNum("sP.lc1.val", eepromCurrentValues.scalesF1);
  myNex.writeNum("sP.lc2.val", eepromCurrentValues.scalesF2);
  myNex.writeNum("sP.pump_zero.val", eepromCurrentValues.pumpFlowAtZero * 10000.f);
  myNex.writeNum("warmupState", eepromCurrentValues.warmupState);

  // Led
  myNex.writeNum("ledNum",
    lcdEncodeLedSettings(
      eepromCurrentValues.ledState,
      eepromCurrentValues.ledDisco,
      eepromCurrentValues.ledR,
      eepromCurrentValues.ledG,
      eepromCurrentValues.ledB
    )
  );

  lcdUploadProfile(eepromCurrentValues);
}

void uploadPageCfg(eepromValues_t &eepromCurrentValues, SystemState &sys) {
  lcdUploadCfg(eepromCurrentValues);
}

void lcdFetchProfileName(eepromValues_t::profile_t &profile, uint8_t index /* 0-offset */) {
  String buttonElemId = String("home.qPf") + (index + 1) + ".txt";
  snprintf(profile.name, sizeof(profile.name), "%s", myNex.readStr(buttonElemId).c_str());
}

void lcdFetchDoseSettings(eepromValues_t::profile_t &profile) {
  // DOse settings
  profile.stopOnWeightState              = myNex.readNumber("shotState");
  profile.shotDose                       = myNex.readNumber("dS.numDose.val") / 10.f;
  profile.shotStopOnCustomWeight         = myNex.readNumber("dS.numDoseForced.val") / 10.f;
  profile.shotPreset                     = myNex.readNumber("shotPreset");
}

void lcdFetchTemp(eepromValues_t::profile_t &profile) {
  profile.setpoint       = myNex.readNumber("sT.setPoint.val");
}

/**
* Overwrites the entire profile in the index corresponding to
* the currently selected profile on the screen.
*/
void lcdFetchCurrentProfile(eepromValues_t & settings) {
  // Target save to the currently selected profile on screen (can be different from runningCfg on long press)
  settings.activeProfile = lcdGetSelectedProfile();
  eepromValues_t::profile_t *profile = &settings.profiles[settings.activeProfile];

  // Stages aren't stored on the screen; the caller copies them from the running config.
  lcdFetchProfileName(*profile, settings.activeProfile);
  lcdFetchDoseSettings(*profile);
  lcdFetchTemp(*profile);
}

void lcdFetchBrewSettings(eepromValues_t &settings) {
  // More brew settings
  settings.homeOnShotFinish               = myNex.readNumber("bckHome");
  settings.basketPrefill                  = myNex.readNumber("basketPrefill");
  settings.brewDeltaState                 = myNex.readNumber("deltaState");
}

void lcdFetchBoiler(eepromValues_t &settings) {
  settings.steamSetPoint                  = myNex.readNumber("sT.steamSetPoint.val");
  settings.offsetTemp                     = myNex.readNumber("sT.offSet.val");
  settings.hpwr                           = myNex.readNumber("sT.hpwr.val");
  settings.mainDivider                    = myNex.readNumber("sT.mDiv.val");
  settings.brewDivider                    = myNex.readNumber("sT.bDiv.val");
}

void lcdFetchSystem(eepromValues_t &settings) {
  // System settings
  settings.lcdSleep                       = myNex.readNumber("sP.n1.val"); // nextion sleep var
  settings.warmupState                    = myNex.readNumber("warmupState");
  settings.scalesF1                       = myNex.readNumber("sP.lc1.val");
  settings.scalesF2                       = myNex.readNumber("sP.lc2.val");
  settings.pumpFlowAtZero                 = myNex.readNumber("sP.pump_zero.val") / 10000.f;
}

void lcdFetchLed(eepromValues_t &settings) {
  // Led Settings
  uint32_t ledNum = myNex.readNumber("ledNum");
  lcdDecodeLedSettings(ledNum, settings.ledState, settings.ledDisco, settings.ledR, settings.ledG, settings.ledB);
}

void lcdFetchPage(eepromValues_t &settings, NextionPage page, int targetProfile) {
  switch (page) {
    case NextionPage::BrewMore:
      lcdFetchBrewSettings(settings);
      break;
    case NextionPage::SettingsBoiler:
      lcdFetchTemp(settings.profiles[targetProfile]);
      lcdFetchBoiler(settings);
      break;
    case NextionPage::SettingsSystem:
      lcdFetchSystem(settings);
      break;
    case NextionPage::ShotSettings:
      lcdFetchDoseSettings(settings.profiles[targetProfile]);
      break;
    case NextionPage::Led:
      lcdFetchLed(settings);
      break;
    default:
      break;
  }
}

uint8_t lcdGetSelectedProfile(void) {
  uint8_t pId = 0;
  int attempts = 2;
  do {
    if (attempts-- <= 0) {
      lcdShowPopup((String("getProfile rekt: ") + pId).c_str());
      return 0;
    }
    pId = myNex.readNumber("pId");
  } while (pId < 1 || pId > 5);
  return pId - 1; /* 1-offset in nextion */
}

int lcdGetManualFlowVol(void) {
  return myNex.readNumber("h0.val");
}

int lcdGetHomeScreenScalesEnabled(void) {
  return myNex.readNumber("scEn.val");
}

int lcdGetSelectedOperationalMode(void) {
  return  myNex.readNumber("modeSelect");
}

int lcdGetDescaleCycle(void) {
  return myNex.readNumber("j0.val");
}

void lcdSetDescaleCycle(int cycle) {
  myNex.writeNum("j0.val", cycle);
}

void lcdSetPressure(float val) {
  myNex.writeNum("pressure.val", val);
}

void lcdSetUpTime(float val) {
  myNex.writeNum("systemUpTime", val);
}

void lcdSetTemperature(uint16_t val) {
  myNex.writeNum("currentTemp", val);
}

void lcdSetTemperatureDecimal(uint16_t val) {
  myNex.writeNum("dE.val", val);
}

void lcdSetWeight(float val) {
  char tmp[6];
  int check = snprintf(tmp, sizeof(tmp), "%.1f", static_cast<double>(val));
  if (check > 0 && static_cast<unsigned int>(check) <= sizeof(tmp)) {
    strcat(tmp, "g");
    myNex.writeStr("weight.txt", tmp);
  }
}

void lcdSetFlow(int val) {
  myNex.writeNum("flow.val", val);
}

void lcdShowDebug(int val1, int val2) {
  myNex.writeNum("debug1",val1);
  myNex.writeNum("debug2",val2);
}

void lcdShowPopup(const char *msg) {
  static unsigned int timer;
  if(millis() > timer + 1150) {
    myNex.writeStr("popupMSG.t0.txt", msg);
    myNex.writeStr("page popupMSG");
    timer = millis();
  }
}

void lcdSetTankWaterLvl(uint16_t val) {
  myNex.writeNum("j0.val", val);
}
void lcdTargetState(int val) {
  myNex.writeNum("targetState", val);
}

void lcdBrewTimerStart(void) {
  myNex.writeNum("timerState", 1);
}

void lcdBrewTimerStop(void) {
  myNex.writeNum("timerState", 0);
}

void lcdSetBrewTimer(int seconds) {
  myNex.writeNum("activeBrewTime", seconds);
}

void lcdWarmupStateStop(void) {
  myNex.writeNum("warmupState", 0);
}

//##############################################################################################################################
//############################################________STAGE SCREENS________#####################################################
//##############################################################################################################################
namespace {
  constexpr uint16_t COLOR_RED = 64171;
  constexpr uint16_t COLOR_ROW_BORDER = 12678;
  constexpr uint8_t  STAGE_ROWS = 5;
  constexpr uint32_t NEXTION_READ_ERROR = 0xFFFFFFFF;

  const char *curveName(StageCurve curve) {
    switch (curve) {
      case StageCurve::EASE_IN: return "Ease in";
      case StageCurve::EASE_OUT: return "Ease out";
      case StageCurve::EASE_IN_OUT: return "Ease in-out";
      default: return "Linear";
    }
  }

  // Xfloat fields hold value * 10. Keeps `current` when the read fails (e.g. the field doesn't exist).
  float readTenths(const char *field, float current) {
    uint32_t raw = myNex.readNumber(field);
    return raw == NEXTION_READ_ERROR ? current : raw / 10.f;
  }

  void writeTenths(const char *field, float value) {
    myNex.writeNum(field, static_cast<uint32_t>(value * 10.f + 0.5f));
  }
}

void lcdGoToPage(const char *pageName) {
  myNex.writeStr(String("page ") + pageName);
}

void lcdShowStageList(const eepromValues_t::profile_t &profile, uint8_t firstRow, int selected, bool moveMode, const char *note) {
  char buf[48];
  int pages = (profile.stageCount + STAGE_ROWS - 1) / STAGE_ROWS;
  if (pages < 1) pages = 1;

  if (note != nullptr && note[0] != '\0') {
    snprintf(buf, sizeof(buf), "%s", note);
  } else {
    snprintf(buf, sizeof(buf), "%.16s  %d/%d", profile.name, firstRow / STAGE_ROWS + 1, pages);
  }
  myNex.writeStr("tProf.txt", buf);

  for (uint8_t row = 0; row < STAGE_ROWS; row++) {
    int idx = firstRow + row;
    char field[16];
    if (idx < profile.stageCount) {
      stages::formatSummary(profile.stages[idx], idx + 1, buf, sizeof(buf), idx < profile.stageCount - 1);
    } else {
      buf[0] = '\0';
    }
    snprintf(field, sizeof(field), "r%d.txt", row);
    myNex.writeStr(field, buf);
    snprintf(field, sizeof(field), "r%d.borderc", row);
    myNex.writeNum(field, idx == selected ? COLOR_RED : COLOR_ROW_BORDER);
  }
  myNex.writeNum("bMove.bco", moveMode ? COLOR_RED : 0);
}

void lcdShowStageEditor(const eepromValues_t::profile_t &profile, uint8_t index) {
  if (index >= profile.stageCount) return;
  const stage_t &stage = profile.stages[index];
  bool flow = stage.type == StageType::FLOW;

  // Global fields (readable/writable from any page)
  writeTenths("stageTarget.xStart.val", stage.start);
  writeTenths("stageTarget.xEnd.val", stage.end);
  writeTenths("stageTarget.xRamp.val", stage.rampTime);
  writeTenths("stageTarget.xLim.val", stage.limit);
  writeTenths("stageExit.xTime.val", stage.exitTime);
  writeTenths("stageExit.xPAbove.val", stage.exitPressureAbove);
  writeTenths("stageExit.xPBelow.val", stage.exitPressureBelow);
  writeTenths("stageExit.xWeight.val", stage.exitWeight);

  // Page-local components can only be written while their page is shown
  char title[16];
  snprintf(title, sizeof(title), "Stage %d/%d", index + 1, profile.stageCount);
  if (lcdCurrentPageId == NextionPage::StageTarget) {
    myNex.writeStr("b1.txt", title);
    myNex.writeStr("bType.txt", flow ? "Flow" : "Pressure");
    myNex.writeStr("bCurve.txt", curveName(stage.curve));
    myNex.writeStr("tStart.txt", flow ? "Start (ml/s)" : "Start (bar)");
    myNex.writeStr("tEnd.txt", flow ? "End (ml/s)" : "End (bar)");
    myNex.writeStr("tLim.txt", flow ? "Max bar" : "Max flow");
  } else if (lcdCurrentPageId == NextionPage::StageExit) {
    myNex.writeStr("b1.txt", title);
    myNex.writeStr("bTareS.txt", stage.tareAtStart ? "Start: yes" : "Start: no");
    myNex.writeStr("bTareE.txt", stage.tareAtEnd ? "End: yes" : "End: no");
  }
}

void lcdFetchStageEditor(stage_t &stage) {
  stage.start = readTenths("stageTarget.xStart.val", stage.start);
  stage.end = readTenths("stageTarget.xEnd.val", stage.end);
  stage.rampTime = readTenths("stageTarget.xRamp.val", stage.rampTime);
  stage.limit = readTenths("stageTarget.xLim.val", stage.limit);
  stage.exitTime = readTenths("stageExit.xTime.val", stage.exitTime);
  stage.exitPressureAbove = readTenths("stageExit.xPAbove.val", stage.exitPressureAbove);
  stage.exitPressureBelow = readTenths("stageExit.xPBelow.val", stage.exitPressureBelow);
  stage.exitWeight = readTenths("stageExit.xWeight.val", stage.exitWeight);
}

// Plots the stage profile on the graphPreview waveform (id 1):
// ch0 pressure (bar*10), ch1 flow (ml/s*10), ch2 a full-height spike at every stage change.
void lcdDrawStagePreview(const eepromValues_t::profile_t &profile) {
  constexpr int WIDTH = 320;
  stages::Preview preview(profile);
  if (preview.totalSeconds() <= 0.f) return;

  myNex.writeStr("cle 1,255");
  int previousStage = 0;
  char cmd[20];
  for (int x = 0; x < WIDTH; x++) {
    float t = (x + 0.5f) * preview.totalSeconds() / WIDTH;
    stages::PreviewSample sample = preview.sampleAt(t);
    int pressure = constrain(static_cast<int>(sample.pressure * 10.f + 0.5f), 0, 255);
    int flow = constrain(static_cast<int>(sample.flow * 10.f + 0.5f), 0, 255);
    int marker = (x > 0 && sample.stage != previousStage) ? 255 : 0;
    previousStage = sample.stage;

    snprintf(cmd, sizeof(cmd), "add 1,0,%d", pressure);
    myNex.writeStr(cmd);
    snprintf(cmd, sizeof(cmd), "add 1,1,%d", flow);
    myNex.writeStr(cmd);
    snprintf(cmd, sizeof(cmd), "add 1,2,%d", marker);
    myNex.writeStr(cmd);
    if (x % 64 == 0) watchdogReload();
  }
  myNex.writeStr("t2.txt", preview.hasEstimates() ? "Stages | ~est" : "Stages |");
}

//##############################################################################################################################
//############################################_____SCALES CALIBRATION_____######################################################
//##############################################################################################################################
void lcdShowCalibrationMessage(const char *message) {
  myNex.writeStr("tMsg.txt", message);
}

void lcdShowCalibrationLive(const char *text) {
  myNex.writeStr("tLive.txt", text);
}

void lcdGetCalibrationWeights(float &weightX, float &weightY) {
  weightX = readTenths("calibration.xCalX.val", 0.f);
  weightY = readTenths("calibration.xCalY.val", 0.f);
}

void lcdSetPumpZero(float pumpZero) {
  myNex.writeNum("sP.pump_zero.val", static_cast<uint32_t>(pumpZero * 10000.f + 0.5f));
}

void lcdSetScalesFactors(int factor1, int factor2) {
  myNex.writeNum("sP.lc1.val", factor1);
  myNex.writeNum("sP.lc2.val", factor2);
}

void trigger1(void) { lcdSaveSettingsTrigger(); }
void trigger2(void) { lcdScalesTareTrigger(); }
void trigger3(void) { lcdHomeScreenScalesTrigger(); }
void trigger4(void) { lcdBrewGraphScalesTareTrigger(); }
void trigger6(void) { lcdRefreshElementsTrigger(); }
void trigger7(void) { lcdQuickProfileSwitch(); }
void trigger8(void) { lcdSaveProfileTrigger(); }
void trigger9(void) { lcdResetSettingsTrigger(); }
void trigger10(void) { lcdLoadDefaultProfileTrigger(); }
// Stage list (stages page)
void trigger16(void) { lcdStageProfileSwitchTrigger(-1); }
void trigger17(void) { lcdStageProfileSwitchTrigger(+1); }
void trigger18(void) { lcdStageRowTrigger(0); }
void trigger19(void) { lcdStageRowTrigger(1); }
void trigger20(void) { lcdStageRowTrigger(2); }
void trigger21(void) { lcdStageRowTrigger(3); }
void trigger22(void) { lcdStageRowTrigger(4); }
void trigger23(void) { lcdStagePageTrigger(-1); }
void trigger24(void) { lcdStagePageTrigger(+1); }
void trigger25(void) { lcdStageAddTrigger(); }
void trigger26(void) { lcdStageDeleteTrigger(); }
void trigger27(void) { lcdStageSaveTrigger(); }
void trigger33(void) { lcdStageMoveModeTrigger(); }
// Stage editor (stageTarget / stageExit pages)
void trigger28(void) { lcdStageEditStepTrigger(-1); }
void trigger29(void) { lcdStageEditStepTrigger(+1); }
void trigger30(void) { lcdStageEditDoneTrigger(); }
void trigger31(void) { lcdStageTypeTrigger(); }
void trigger32(void) { lcdStageCurveTrigger(); }
void trigger35(void) { lcdStageTareTrigger(true); }
void trigger36(void) { lcdStageTareTrigger(false); }
// Scales calibration (calibration page)
void trigger37(void) { lcdCalibrationNextTrigger(); }
void trigger38(void) { lcdCalibrationRestartTrigger(); }
void trigger39(void) { lcdCalibrationSaveTrigger(); }
