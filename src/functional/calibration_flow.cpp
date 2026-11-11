#include "calibration_flow.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "scales_calibration.h"
#include "pump_calibration.h"
#include "../peripherals/scales.h"
#include "../peripherals/pump.h"
#include "../peripherals/peripherals.h"
#include "../peripherals/internal_watchdog.h"

namespace {
  calibrationFlow::Context ctx = { nullptr, nullptr, nullptr };
}

// Guided calibration of the two load cells with two known weights X and Y.
// Tray off: X and Y on one load cell, then on the other (each weighing loads one cell only,
// so each factor is measured on its own). Optional check with the tray back on: X+Y on the
// tray is added to the fit (counted twice) to correct both factors under real conditions.
//
// Pump zero calibration (needs working scales): portafilter off, cup on the tray. The pump
// primes until water clearly arrives, pauses, then runs a timed measurement. Both weighings
// happen with the pump off, so pump vibration doesn't affect them.
namespace calibration {
  enum class Step : uint8_t {
    TARE, ONE_X, ONE_Y, OTHER_X, OTHER_Y,  // tray off, per cell
    FITTED,                                // factors from the per-cell weighings
    TRAY_WEIGH,                            // tray back on and tared, X+Y on the tray
    CHECKED,                               // factors refined with the tray weighing
    SAVED,
    PUMP_READY, PUMP_PRIMING, PUMP_SETTLE_1, PUMP_MEASURING, PUMP_SETTLE_2, PUMP_RESULT, PUMP_SAVED
  };
  Step step = Step::TARE;
  scalesCalibration::Sample samples[6];  // 4 per-cell weighings + the tray weighing twice
  scalesCalibration::Result result;
  int firstCell = 0;                     // which cell (0/1) got the first weighings
  float trayBefore = 0.f;                // tray reading with the per-cell factors
  float trayAfter = 0.f;                 // tray reading with the refined factors
  uint32_t liveTimer = 0;

  // Pump zero
  uint32_t stepStarted = 0;
  uint32_t primedSince = 0;
  float restStartGrams = 0.f;            // cup weight before priming / before measuring (pump off)
  float measureStartGrams = 0.f;
  long measureStartStrokes = 0;
  long measuredStrokes = 0;
  float measuredGrams = 0.f;
  float pressureSum = 0.f;
  uint32_t pressureSamples = 0;
  float averagePressure = 0.f;
  float oldPumpZero = 0.f;
  float newPumpZero = -1.f;

  bool hasResult() {
    return result.ok && (step == Step::FITTED || step == Step::TRAY_WEIGH || step == Step::CHECKED);
  }

  bool isPumpStep() {
    return step >= Step::PUMP_READY;
  }

  // The pump run owns the pump and valve from priming until the last weighing.
  bool pumpRunActive() {
    return step >= Step::PUMP_PRIMING && step <= Step::PUMP_SETTLE_2;
  }
}

// Which cell carries the weight: 0 or 1, or -1 if it's spread over both.
static int loadedCell(const float raw[2], float f1, float f2) {
  float g1 = fabsf(raw[0] / f1), g2 = fabsf(raw[1] / f2);
  if (g1 + g2 <= 0.f) return -1;
  if (g1 >= 0.8f * (g1 + g2)) return 0;
  if (g2 >= 0.8f * (g1 + g2)) return 1;
  return -1;
}

// Cup weight with the current factors, averaged over 10 reads (call with the pump off).
static bool readRestGrams(float &grams) {
  float raw[2];
  watchdogReload();
  bool ok = scalesReadCells(raw, 10);
  watchdogReload();
  if (ok) grams = scalesCalibration::grams(raw[0], raw[1], ctx.cfg->scalesF1, ctx.cfg->scalesF2);
  return ok;
}

static void showCalibrationStep(void) {
  using calibration::Step;
  char msg[120];
  float weightX, weightY;
  lcdGetCalibrationWeights(weightX, weightY);
  const double x = static_cast<double>(weightX), y = static_cast<double>(weightY);
  const int f1 = static_cast<int>(lroundf(calibration::result.factor1));
  const int f2 = static_cast<int>(lroundf(calibration::result.factor2));

  switch (calibration::step) {
    case Step::TARE:
      snprintf(msg, sizeof(msg), "Scales: enter X and Y, take off the\rtray, cells empty, tap Next.\rPump zero instead: tap Save.");
      break;
    case Step::ONE_X:
      snprintf(msg, sizeof(msg), "2/5 Put X (%.1f g) right on one\rload cell, then tap Next.", x);
      break;
    case Step::ONE_Y:
      snprintf(msg, sizeof(msg), "3/5 Swap X for Y (%.1f g) on the\rsame load cell, then tap Next.", y);
      break;
    case Step::OTHER_X:
      snprintf(msg, sizeof(msg), "4/5 Put X (%.1f g) right on the\rother load cell, then tap Next.", x);
      break;
    case Step::OTHER_Y:
      snprintf(msg, sizeof(msg), "5/5 Swap X for Y (%.1f g) on that\rload cell, then tap Next.", y);
      break;
    case Step::FITTED:
      if (calibration::result.ok) {
        snprintf(msg, sizeof(msg), "Factors %d / %d, fit %.1f g.\rSave now, or put the tray back\r(empty) and tap Next to check.",
          f1, f2, static_cast<double>(calibration::result.maxErrorGrams));
      } else {
        snprintf(msg, sizeof(msg), "Couldn't work out the factors.\rCheck the weights are right on\reach cell and tap Restart.");
      }
      break;
    case Step::TRAY_WEIGH:
      snprintf(msg, sizeof(msg), "Check: put X and Y on the tray\r(%.1f g together), then tap Next.", x + y);
      break;
    case Step::CHECKED:
      snprintf(msg, sizeof(msg), "Tray read %.1f g, now %.1f g\r(should be %.1f g). Factors %d / %d.\rSave keeps them.",
        static_cast<double>(calibration::trayBefore), static_cast<double>(calibration::trayAfter), x + y, f1, f2);
      break;
    case Step::SAVED:
      snprintf(msg, sizeof(msg), "Saved. The scales now use the\rnew factors. Tap Next to also\rcalibrate the pump zero.");
      break;
    case Step::PUMP_READY:
      snprintf(msg, sizeof(msg), "Pump zero: portafilter off, tray on,\ra cup (250 ml+) under the group.\rTap Next; the pump runs ~30 s.");
      break;
    case Step::PUMP_PRIMING:
      snprintf(msg, sizeof(msg), "Filling the pump and group...\rwaiting for water in the cup.");
      break;
    case Step::PUMP_SETTLE_1:
      snprintf(msg, sizeof(msg), "Water is flowing. Pausing to\rweigh the cup...");
      break;
    case Step::PUMP_MEASURING:
      snprintf(msg, sizeof(msg), "Measuring the pump flow...\r(Restart stops the pump)");
      break;
    case Step::PUMP_SETTLE_2:
      snprintf(msg, sizeof(msg), "Pump stopped, weighing the cup...");
      break;
    case Step::PUMP_RESULT:
      if (pumpCalibration::inRange(calibration::newPumpZero)) {
        snprintf(msg, sizeof(msg), "Pump zero %.4f (was %.4f)\r%.3f ml per stroke at %.1f bar.\rSave keeps it, Restart redoes it.",
          static_cast<double>(calibration::newPumpZero), static_cast<double>(calibration::oldPumpZero),
          static_cast<double>(calibration::measuredGrams / calibration::measuredStrokes),
          static_cast<double>(calibration::averagePressure));
      } else {
        snprintf(msg, sizeof(msg), "Measured %.4f, outside 0.21-0.31.\rCheck the cup caught all the water\rand tap Restart.",
          static_cast<double>(calibration::newPumpZero));
      }
      break;
    case Step::PUMP_SAVED:
      snprintf(msg, sizeof(msg), "Pump zero saved: %.4f", static_cast<double>(calibration::newPumpZero));
      break;
  }
  lcdShowCalibrationMessage(msg);
}

static void calibrationStart(void) {
  calibration::step = calibration::Step::TARE;
  calibration::result = scalesCalibration::Result();
  if (!scalesHardwarePresent()) {
    lcdShowCalibrationMessage("No built-in scales found.\rCalibration needs the load cells.");
    return;
  }
  showCalibrationStep();
}

static void pumpCalibrationStop(const char *reason) {
  bool wasRunning = calibration::pumpRunActive();
  calibration::step = calibration::Step::PUMP_READY;
  if (wasRunning) {
    setPumpOff();
    closeValve();
  }
  if (reason != nullptr) lcdShowCalibrationMessage(reason);
  else showCalibrationStep();
}

static void pumpCalibrationEnter(uint32_t now) {
  calibration::stepStarted = now;
}

// Drives the pump run; the machine calls this every loop while it is in the CALIBRATING state.
static void pumpCalibrationUpdate(void) {
  using calibration::Step;
  if (!calibration::pumpRunActive()) return;

  uint32_t now = millis();
  uint32_t inStep = now - calibration::stepStarted;

  switch (calibration::step) {
    case Step::PUMP_PRIMING: {
      openValve();
      setPumpFullOn();
      bool water = ctx.state->weight - calibration::restStartGrams >= pumpCalibration::PRIME_GRAMS;
      if (!water) {
        calibration::primedSince = 0;
      } else if (calibration::primedSince == 0) {
        calibration::primedSince = now;
      } else if (now - calibration::primedSince >= pumpCalibration::PRIME_CONFIRM_MS) {
        setPumpOff();  // valve stays open so nothing is dumped into the tray
        calibration::step = Step::PUMP_SETTLE_1;
        pumpCalibrationEnter(now);
        showCalibrationStep();
        return;
      }
      if (inStep > pumpCalibration::PRIME_TIMEOUT_MS) {
        pumpCalibrationStop("No water reached the cup.\rCheck the tank and the cup position,\rthen tap Next to retry.");
      }
      break;
    }

    case Step::PUMP_SETTLE_1:
      setPumpOff();
      if (inStep >= pumpCalibration::SETTLE_MS) {
        if (!readRestGrams(calibration::measureStartGrams)) {
          pumpCalibrationStop("Scales not ready, tap Next to retry.");
          return;
        }
        calibration::measureStartStrokes = getPumpTotalClicks();
        calibration::pressureSum = 0.f;
        calibration::pressureSamples = 0;
        calibration::step = Step::PUMP_MEASURING;
        pumpCalibrationEnter(millis());
        showCalibrationStep();
      }
      break;

    case Step::PUMP_MEASURING:
      openValve();
      setPumpFullOn();
      calibration::pressureSum += ctx.state->smoothedPressure;
      calibration::pressureSamples++;
      if (inStep >= pumpCalibration::MEASURE_MS
        || ctx.state->weight - calibration::measureStartGrams >= pumpCalibration::MEASURE_MAX_GRAMS) {
        setPumpOff();
        calibration::measuredStrokes = getPumpTotalClicks() - calibration::measureStartStrokes;
        calibration::step = Step::PUMP_SETTLE_2;
        pumpCalibrationEnter(now);
        showCalibrationStep();
      }
      break;

    case Step::PUMP_SETTLE_2: {
      setPumpOff();
      if (inStep < pumpCalibration::SETTLE_MS) break;
      float endGrams;
      if (!readRestGrams(endGrams)) {
        pumpCalibrationStop("Scales not ready, tap Next to retry.");
        return;
      }
      closeValve();
      calibration::measuredGrams = endGrams - calibration::measureStartGrams;
      calibration::averagePressure = calibration::pressureSamples > 0
        ? fmaxf(0.f, calibration::pressureSum / calibration::pressureSamples) : 0.f;
      calibration::oldPumpZero = ctx.cfg->pumpFlowAtZero;
      calibration::newPumpZero = pumpCalibration::newPumpZero(
        calibration::oldPumpZero, calibration::measuredGrams, calibration::measuredStrokes,
        getPumpFlowPerClick(calibration::averagePressure), getPumpClickMultiplier());
      calibration::step = Step::PUMP_RESULT;
      showCalibrationStep();
      break;
    }

    default:
      break;
  }
}

static void pumpCalibrationBegin(void) {
  if (ctx.canRunPump == nullptr || !ctx.canRunPump()) {
    lcdShowCalibrationMessage("Can't run the pump now: leave\rflush/descale/manual/steam mode\rand turn all switches off.");
    return;
  }
  if (!readRestGrams(calibration::restStartGrams)) {
    lcdShowCalibrationMessage("Scales not ready, tap Next again.");
    return;
  }
  calibration::primedSince = 0;
  calibration::step = calibration::Step::PUMP_PRIMING;
  pumpCalibrationEnter(millis());
  showCalibrationStep();
}

// Live reading on the calibration page, called from lcdRefresh().
static void calibrationLiveUpdate(void) {
  if (!scalesHardwarePresent() || millis() < calibration::liveTimer) return;
  calibration::liveTimer = millis() + 500;
  char text[48];

  if (calibration::isPumpStep()) {
    // Don't block on the scales while the pump runs; use the regular weight reading.
    long strokes = calibration::step == calibration::Step::PUMP_MEASURING
      ? getPumpTotalClicks() - calibration::measureStartStrokes : 0;
    snprintf(text, sizeof(text), "Cup: %.1f g  %.1f bar  %ld strokes",
      static_cast<double>(ctx.state->weight), static_cast<double>(ctx.state->smoothedPressure), strokes);
    lcdShowCalibrationLive(text);
    return;
  }

  float raw[2];
  if (!scalesReadCells(raw, 1)) {
    lcdShowCalibrationLive("Scales not ready");
    return;
  }
  bool useNew = calibration::hasResult();
  float f1 = useNew ? calibration::result.factor1 : ctx.cfg->scalesF1;
  float f2 = useNew ? calibration::result.factor2 : ctx.cfg->scalesF2;
  snprintf(text, sizeof(text), "%s %.1f g  (1: %.1f  2: %.1f)", useNew ? "New:" : "Now:",
    static_cast<double>(scalesCalibration::grams(raw[0], raw[1], f1, f2)),
    static_cast<double>(raw[0] / f1), static_cast<double>(raw[1] / f2));
  lcdShowCalibrationLive(text);
}

void lcdCalibrationNextTrigger(void) {
  using calibration::Step;
  if (!scalesHardwarePresent()) return;

  if (calibration::step == Step::SAVED || calibration::step == Step::PUMP_SAVED) {
    calibration::step = Step::PUMP_READY;
    showCalibrationStep();
    return;
  }
  if (calibration::step == Step::PUMP_READY) {
    pumpCalibrationBegin();
    return;
  }
  if (calibration::isPumpStep()) return;  // running or showing a result

  float weightX, weightY;
  lcdGetCalibrationWeights(weightX, weightY);
  if (weightX <= 0.f || weightY <= 0.f) {
    lcdShowCalibrationMessage("Enter both weights (X and Y)\rin grams first.");
    return;
  }

  float raw[2];
  watchdogReload();
  switch (calibration::step) {
    case Step::TARE:
    case Step::FITTED:
      if (calibration::step == Step::FITTED && !calibration::result.ok) return;
      if (!scalesTare()) {
        lcdShowCalibrationMessage("Scales not ready, tap Next again.");
        return;
      }
      calibration::step = calibration::step == Step::TARE ? Step::ONE_X : Step::TRAY_WEIGH;
      break;

    case Step::ONE_X:
    case Step::ONE_Y:
    case Step::OTHER_X:
    case Step::OTHER_Y: {
      if (!scalesReadCells(raw, 10)) {
        lcdShowCalibrationMessage("Scales not ready, tap Next again.");
        return;
      }
      // The current factors are only used to see which cell is loaded.
      int cell = loadedCell(raw, ctx.cfg->scalesF1, ctx.cfg->scalesF2);
      bool expectFirst = calibration::step == Step::ONE_X || calibration::step == Step::ONE_Y;
      if (cell < 0) {
        lcdShowCalibrationMessage("The weight seems to rest on both\rcells. Put it right on one load\rcell and tap Next again.");
        return;
      }
      if (calibration::step == Step::ONE_X) calibration::firstCell = cell;
      if ((cell == calibration::firstCell) != expectFirst) {
        lcdShowCalibrationMessage(expectFirst
          ? "That's the other load cell. Use\rthe same cell as before, then\rtap Next again."
          : "That's the same load cell as\rbefore. Use the other one, then\rtap Next again.");
        return;
      }

      int i = static_cast<int>(calibration::step) - static_cast<int>(Step::ONE_X);
      bool isY = calibration::step == Step::ONE_Y || calibration::step == Step::OTHER_Y;
      calibration::samples[i] = { raw[0], raw[1], isY ? weightY : weightX };

      if (calibration::step == Step::OTHER_Y) {
        calibration::result = scalesCalibration::solve(calibration::samples, 4);
        calibration::step = Step::FITTED;
      } else {
        calibration::step = static_cast<Step>(static_cast<uint8_t>(calibration::step) + 1);
      }
      break;
    }

    case Step::TRAY_WEIGH: {
      if (!scalesReadCells(raw, 10)) {
        lcdShowCalibrationMessage("Scales not ready, tap Next again.");
        return;
      }
      float total = weightX + weightY;
      calibration::trayBefore = scalesCalibration::grams(raw[0], raw[1], calibration::result.factor1, calibration::result.factor2);
      // Count the tray weighing twice: it's the situation the scales are used in.
      calibration::samples[4] = { raw[0], raw[1], total };
      calibration::samples[5] = calibration::samples[4];
      scalesCalibration::Result refined = scalesCalibration::solve(calibration::samples, 6);
      if (refined.ok) calibration::result = refined;
      calibration::trayAfter = scalesCalibration::grams(raw[0], raw[1], calibration::result.factor1, calibration::result.factor2);
      calibration::step = Step::CHECKED;
      break;
    }

    default:
      return;  // CHECKED: use Save or Restart
  }
  watchdogReload();
  showCalibrationStep();
}

void lcdCalibrationRestartTrigger(void) {
  if (calibration::isPumpStep()) pumpCalibrationStop(nullptr);
  else calibrationStart();
}

static void savePumpZero(void) {
  float zero = calibration::newPumpZero;
  if (!pumpCalibration::inRange(zero)) {
    lcdShowCalibrationMessage("Can't save a value outside\r0.21-0.31. Tap Restart to measure\ragain.");
    return;
  }
  ctx.cfg->pumpFlowAtZero = zero;
  pumpInit(ctx.cfg->powerLineFrequency, zero);
  lcdSetPumpZero(zero);

  eepromValues_t eepromCurrentValues = eepromGetCurrentValues();
  eepromCurrentValues.pumpFlowAtZero = zero;
  bool saved = eepromWrite(eepromCurrentValues);
  watchdogReload();

  calibration::step = calibration::Step::PUMP_SAVED;
  if (saved) showCalibrationStep();
  else lcdShowCalibrationMessage("Couldn't save to memory.\rThe new pump zero applies until reboot.");
}

void lcdCalibrationSaveTrigger(void) {
  using calibration::Step;
  if (calibration::step == Step::TARE) {  // shortcut to the pump zero calibration
    calibration::step = Step::PUMP_READY;
    showCalibrationStep();
    return;
  }
  if (calibration::step == Step::PUMP_RESULT) {
    savePumpZero();
    return;
  }

  bool canSave = calibration::result.ok && (calibration::step == Step::FITTED || calibration::step == Step::CHECKED);
  if (!canSave) {
    lcdShowCalibrationMessage("Nothing to save yet:\rgo through the steps first.");
    return;
  }
  int f1 = static_cast<int>(lroundf(calibration::result.factor1));
  int f2 = static_cast<int>(lroundf(calibration::result.factor2));
  if (abs(f1) > 20000 || abs(f2) > 20000 || f1 == 0 || f2 == 0) {
    lcdShowCalibrationMessage("Factors out of range (max 20000).\rCheck the weights and Restart.");
    return;
  }

  ctx.cfg->scalesF1 = f1;
  ctx.cfg->scalesF2 = f2;
  scalesSetFactors(f1, f2);
  lcdSetScalesFactors(f1, f2);

  eepromValues_t eepromCurrentValues = eepromGetCurrentValues();
  eepromCurrentValues.scalesF1 = f1;
  eepromCurrentValues.scalesF2 = f2;
  bool saved = eepromWrite(eepromCurrentValues);
  watchdogReload();

  calibration::step = Step::SAVED;
  if (saved) showCalibrationStep();
  else lcdShowCalibrationMessage("Couldn't save to memory.\rThe new factors apply until reboot.");
}

//---------------------------------------------------------------------//
//----------------------------- Public API ----------------------------//
//---------------------------------------------------------------------//
namespace calibrationFlow {
  void init(const Context &context) {
    ctx = context;
  }

  void pageChanged(NextionPage from, NextionPage to) {
    if (ctx.cfg == nullptr) return;
    // Leaving the page (also for the keyboard) stops a pump run.
    if (from == NextionPage::Calibration && calibration::pumpRunActive()) {
      calibration::step = calibration::Step::PUMP_READY;
      setPumpOff();
      closeValve();
    }
    if (to == NextionPage::Calibration) {
      // Coming back from the keyboard (entering X or Y) keeps the current step.
      if (from == NextionPage::KeyboardNumeric) showCalibrationStep();
      else calibrationStart();
    }
  }

  void liveUpdate(void) {
    if (ctx.cfg != nullptr) calibrationLiveUpdate();
  }

  bool pumpRunActive(void) {
    return calibration::pumpRunActive();
  }

  void runPump(void) {
    if (ctx.cfg != nullptr) pumpCalibrationUpdate();
  }

  void abortPumpRun(const char *reason) {
    if (calibration::pumpRunActive()) pumpCalibrationStop(reason);
  }
}
