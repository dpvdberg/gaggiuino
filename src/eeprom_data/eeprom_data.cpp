#include "eeprom_data.h"
#include <FlashStorage_STM32.h>
#include "eeprom_metadata.h"
#include "default_profiles.h"
#include "legacy/eeprom_data_v4.h"
#include "legacy/eeprom_data_v5.h"
#include "legacy/eeprom_data_v6.h"
#include "legacy/eeprom_data_v7.h"
#include "legacy/eeprom_data_v8.h"
#include "legacy/eeprom_data_v9.h"
#include "legacy/eeprom_data_v10.h"
#include "legacy/eeprom_data_v11.h"
#include "legacy/eeprom_data_v12.h"
#include "../log.h"

namespace {

  struct eepromMetadata_t eepromMetadata;

  eepromValues_t getEepromDefaults(void) {
    eepromValues_t defaultData;

    // Profiles
    defaultData.activeProfile = 0;
    for (int i=0; i<MAX_PROFILES; i++) {
      snprintf(defaultData.profiles[i].name, PROFILE_NAME_LENGTH, "%s", defaultsProfile[i].name);
      defaultData.profiles[i].stageCount = defaultsProfile[i].stageCount;
      for (int s=0; s<MAX_STAGES; s++) {
        defaultData.profiles[i].stages[s] = defaultsProfile[i].stages[s];
      }
      defaultData.profiles[i].setpoint = defaultsProfile[i].setpoint;
      defaultData.profiles[i].stopOnWeightState = defaultsProfile[i].stopOnWeightState;
      defaultData.profiles[i].shotDose = defaultsProfile[i].shotDose;
      defaultData.profiles[i].shotStopOnCustomWeight = defaultsProfile[i].shotStopOnCustomWeight;
      defaultData.profiles[i].shotPreset = defaultsProfile[i].shotPreset;
    }
    // General brew settings
    defaultData.homeOnShotFinish = false;
    defaultData.brewDeltaState = true;
    defaultData.basketPrefill = false;
    // System settings
    defaultData.steamSetPoint = 155;
    defaultData.offsetTemp = 7;
    defaultData.hpwr = 550;
    defaultData.mainDivider = 5;
    defaultData.brewDivider = 3;
    defaultData.powerLineFrequency = 50;
    defaultData.lcdSleep = 16;
    defaultData.warmupState = false;
    defaultData.scalesF1 = 3920;
    defaultData.scalesF2 = 4210;
    defaultData.pumpFlowAtZero = 0.2225f;
    defaultData.ledState  = true;
    defaultData.ledDisco  = true;
    defaultData.ledR = 9;
    defaultData.ledG = 0;
    defaultData.ledB = 9;

    return defaultData;
  }

  // kind of annoying, but allows reusing macro without messing up type safety
  template <typename T>
  bool copy_t(T& target, T& source) {
    target = source;
    return true;
  }

  bool loadCurrentEepromData EEPROM_METADATA_LOADER(EEPROM_DATA_VERSION, eepromMetadata_t, copy_t);

}


bool eepromWrite(eepromValues_t eepromValuesNew) {
  const char *errMsg = "Data out of range";
  /* Check various profile array values */
  for (int i=0; i<MAX_PROFILES; i++) {
    const eepromValues_t::profile_t &profile = eepromValuesNew.profiles[i];
    if (profile.setpoint < 1 || profile.stageCount < 1 || profile.stageCount > MAX_STAGES) {
      LOG_ERROR(errMsg);
      return false;
    }
    for (int s=0; s<profile.stageCount; s++) {
      const stage_t &stage = profile.stages[s];
      if (stage.start < 0.f || stage.end < 0.f || stage.rampTime < 0.f || stage.limit < 0.f
        || stage.exitTime < 0.f || stage.exitPressureAbove < 0.f || stage.exitPressureBelow < 0.f
        || stage.exitWeight < 0.f)
      {
        LOG_ERROR(errMsg);
        return false;
      }
    }
  }
  /* Check various global values */
  if (eepromValuesNew.steamSetPoint < 1
  || eepromValuesNew.steamSetPoint > 165
  || eepromValuesNew.mainDivider < 1
  || eepromValuesNew.brewDivider < 1
  || eepromValuesNew.pumpFlowAtZero < 0.210f
  || eepromValuesNew.pumpFlowAtZero > 0.310f
  || eepromValuesNew.scalesF1 < -20000
  || eepromValuesNew.scalesF2 > 20000)
  {
    LOG_ERROR(errMsg);
    return false;
  }

  /* Saving the values struct + validation and versioning metadata */
  eepromMetadata.timestamp = millis();
  eepromMetadata.version = EEPROM_DATA_VERSION;
  eepromMetadata.values = eepromValuesNew;
  eepromMetadata.versionTimestampXOR = eepromMetadata.timestamp ^ eepromMetadata.version;
  EEPROM.put(0, eepromMetadata);

  return true;
}

void eepromInit(void) {
  // initialiaze defaults on memory
  eepromMetadata.values = getEepromDefaults();

  // read version
  uint16_t version;
  EEPROM.get(0, version);

  // load appropriate version (including current)
  bool readSuccess = false;

  if (version < EEPROM_DATA_VERSION && legacyEepromDataLoaders[version] != nullptr) {
    readSuccess = (*legacyEepromDataLoaders[version])(eepromMetadata.values);
  } else {
    readSuccess = loadCurrentEepromData(eepromMetadata.values);
  }

  if (!readSuccess) {
    LOG_ERROR("SECU_CHECK FAILED! Applying defaults! eepromMetadata.version=%d", version);
    eepromMetadata.values = getEepromDefaults();
  }

  if (!readSuccess || version != EEPROM_DATA_VERSION) {
    eepromWrite(eepromMetadata.values);
  }
}

struct eepromValues_t eepromGetCurrentValues(void) {
  return eepromMetadata.values;
}
struct eepromValues_t eepromGetDefaultValues(void) {
  return getEepromDefaults();
}
