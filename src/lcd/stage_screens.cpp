#include "stage_screens.h"
#include <stdio.h>
#include <string.h>
#include "../functional/stages.h"

namespace stageScreens {
  namespace {
    constexpr uint8_t ROWS = 5;

    Context ctx = { nullptr, nullptr, nullptr, nullptr };
    uint8_t firstRow = 0;
    int selected = -1;
    bool moveMode = false;
    bool deleteArmed = false;
    uint8_t editIndex = 0;

    eepromValues_t::profile_t &profile() {
      return ACTIVE_PROFILE((*ctx.cfg));
    }

    bool brewing() {
      return ctx.isBrewing != nullptr && ctx.isBrewing();
    }

    void changed() {
      if (ctx.stagesChanged != nullptr) ctx.stagesChanged();
    }

    void showList(const char *note = nullptr) {
      lcdShowStageList(profile(), firstRow, selected, moveMode, note);
    }

    // Shows "Not while brewing" on the list and returns true when an edit must be refused.
    bool refuseWhileBrewing() {
      if (!brewing()) return false;
      showList("Not while brewing");
      return true;
    }

    void showEditor() {
      if (editIndex >= profile().stageCount) editIndex = profile().stageCount - 1;
      lcdShowStageEditor(profile(), editIndex);
    }

    void keepVisible(int index) {
      if (index >= 0) firstRow = (index / ROWS) * ROWS;
    }

    // Copies the editor fields (global on the screen) into the edited stage. Ignored while
    // brewing: the editor then just shows the stored values again.
    void fetchEditor() {
      if (brewing() || editIndex >= profile().stageCount) return;
      lcdFetchStageEditor(profile().stages[editIndex]);
    }

    bool isEditorPage(NextionPage page) {
      return page == NextionPage::StageTarget || page == NextionPage::StageExit;
    }
  }

  void init(const Context &context) {
    ctx = context;
    reset();
  }

  void reset(void) {
    firstRow = 0;
    selected = -1;
    moveMode = false;
    deleteArmed = false;
    editIndex = 0;
  }

  void pageChanged(NextionPage from, NextionPage to) {
    if (ctx.cfg == nullptr) return;

    // The editor fields are global on the screen, so they're current whenever we come from an
    // editor page or from the keyboard opened on one. Don't read them when entering from
    // elsewhere: they'd still hold the previously edited stage.
    if (isEditorPage(from) || (from == NextionPage::KeyboardNumeric && isEditorPage(to))) {
      fetchEditor();
      changed();
    }

    if (isEditorPage(to)) {
      showEditor();
    } else if (to == NextionPage::Stages) {
      deleteArmed = false;
      showList();
    } else if (to == NextionPage::GraphPreview) {
      lcdDrawStagePreview(profile());
    }
  }
}

//---------------------------------------------------------------------//
//------------------------ Stage list triggers ------------------------//
//---------------------------------------------------------------------//
using namespace stageScreens;

void lcdStageProfileSwitchTrigger(int delta) {
  if (ctx.cfg == nullptr || refuseWhileBrewing()) return;
  ctx.cfg->activeProfile = (ctx.cfg->activeProfile + MAX_PROFILES + delta) % MAX_PROFILES;
  reset();
  lcdUploadProfile(*ctx.cfg);
  changed();
  showList();
}

void lcdStageRowTrigger(uint8_t row) {
  if (ctx.cfg == nullptr) return;
  deleteArmed = false;
  int index = firstRow + row;
  if (index >= profile().stageCount) {
    selected = -1;
  } else if (index == selected && !moveMode) {
    if (refuseWhileBrewing()) return;
    editIndex = index;
    lcdGoToPage("stageTarget");
    return;
  } else {
    selected = index;
  }
  showList();
}

void lcdStagePageTrigger(int delta) {
  if (ctx.cfg == nullptr) return;
  deleteArmed = false;

  if (moveMode && selected >= 0) {
    if (refuseWhileBrewing()) return;
    int moved = stages::move(profile(), selected, delta);
    if (moved != selected) {
      selected = moved;
      keepVisible(moved);
      changed();
    }
  } else {
    int lastPageFirstRow = ((profile().stageCount - 1) / ROWS) * ROWS;
    int first = firstRow + delta * ROWS;
    firstRow = constrain(first, 0, lastPageFirstRow);
  }
  showList();
}

void lcdStageAddTrigger(void) {
  if (ctx.cfg == nullptr || refuseWhileBrewing()) return;
  deleteArmed = false;
  if (profile().stageCount >= MAX_STAGES) {
    showList("Max 12 stages");
    return;
  }
  int after = selected >= 0 ? selected : profile().stageCount - 1;
  stage_t copy = profile().stages[after];
  int index = stages::insertAfter(profile(), after, copy);
  selected = index;
  keepVisible(index);
  changed();
  showList();
}

void lcdStageDeleteTrigger(void) {
  if (ctx.cfg == nullptr || refuseWhileBrewing()) return;
  if (selected < 0) {
    showList("Select a stage first");
    return;
  }
  if (profile().stageCount <= 1) {
    showList("Can't delete the last stage");
    return;
  }
  if (!deleteArmed) {
    deleteArmed = true;
    char note[32];
    snprintf(note, sizeof(note), "Delete again: stage %d", selected + 1);
    showList(note);
    return;
  }
  deleteArmed = false;
  stages::remove(profile(), selected);
  if (selected >= profile().stageCount) selected = profile().stageCount - 1;
  keepVisible(selected);
  changed();
  showList();
}

void lcdStageMoveModeTrigger(void) {
  if (ctx.cfg == nullptr) return;
  deleteArmed = false;
  moveMode = !moveMode;
  showList(moveMode && selected < 0 ? "Select a stage to move" : nullptr);
}

void lcdStageSaveTrigger(void) {
  if (ctx.cfg == nullptr) return;
  deleteArmed = false;
  int missing = stages::firstStageWithoutExit(profile());
  if (missing >= 0) {
    char note[32];
    snprintf(note, sizeof(note), "Stage %d needs an exit", missing + 1);
    selected = missing;
    keepVisible(missing);
    showList(note);
    return;
  }
  eepromValues_t values = eepromGetCurrentValues();
  eepromValues_t::profile_t &target = values.profiles[ctx.cfg->activeProfile];
  target.stageCount = profile().stageCount;
  memcpy(target.stages, profile().stages, sizeof(target.stages));
  values.activeProfile = ctx.cfg->activeProfile;
  if (ctx.save != nullptr) ctx.save(values);
}

//---------------------------------------------------------------------//
//----------------------- Stage editor triggers -----------------------//
//---------------------------------------------------------------------//
void lcdStageEditStepTrigger(int delta) {
  if (ctx.cfg == nullptr) return;
  fetchEditor();
  int next = editIndex + delta;
  if (next >= 0 && next < profile().stageCount) editIndex = next;
  changed();
  showEditor();
}

void lcdStageEditDoneTrigger(void) {
  if (ctx.cfg == nullptr) return;
  fetchEditor();
  changed();
  selected = editIndex;
  keepVisible(selected);
  lcdGoToPage("stages");
}

void lcdStageTypeTrigger(void) {
  if (ctx.cfg == nullptr) return;
  fetchEditor();
  if (!brewing()) {
    stage_t &stage = profile().stages[editIndex];
    stage.type = stage.type == StageType::FLOW ? StageType::PRESSURE : StageType::FLOW;
    changed();
  }
  showEditor();
}

void lcdStageCurveTrigger(void) {
  if (ctx.cfg == nullptr) return;
  fetchEditor();
  if (!brewing()) {
    stage_t &stage = profile().stages[editIndex];
    stage.curve = static_cast<StageCurve>((static_cast<uint8_t>(stage.curve) + 1) % 4);
    changed();
  }
  showEditor();
}

void lcdStageTareTrigger(bool atStart) {
  if (ctx.cfg == nullptr) return;
  fetchEditor();
  if (!brewing()) {
    stage_t &stage = profile().stages[editIndex];
    if (atStart) stage.tareAtStart = !stage.tareAtStart;
    else stage.tareAtEnd = !stage.tareAtEnd;
    changed();
  }
  showEditor();
}
