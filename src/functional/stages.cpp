#include "stages.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

namespace stages {

  //---------------------------------------------------------------------//
  //------------------------- Editing the list --------------------------//
  //---------------------------------------------------------------------//

  int insertAfter(profile_t& profile, int index, const stage_t& stage) {
    if (profile.stageCount >= MAX_STAGES) return -1;
    int at = (index < 0 || index >= profile.stageCount) ? profile.stageCount : index + 1;
    for (int i = profile.stageCount; i > at; i--) {
      profile.stages[i] = profile.stages[i - 1];
    }
    profile.stages[at] = stage;
    profile.stageCount++;
    return at;
  }

  bool remove(profile_t& profile, int index) {
    if (profile.stageCount <= 1 || index < 0 || index >= profile.stageCount) return false;
    for (int i = index; i < profile.stageCount - 1; i++) {
      profile.stages[i] = profile.stages[i + 1];
    }
    profile.stageCount--;
    return true;
  }

  int move(profile_t& profile, int index, int delta) {
    int to = index + delta;
    if (index < 0 || index >= profile.stageCount || to < 0 || to >= profile.stageCount) return index;
    stage_t tmp = profile.stages[index];
    profile.stages[index] = profile.stages[to];
    profile.stages[to] = tmp;
    return to;
  }

  //---------------------------------------------------------------------//
  //------------------------- Running a profile -------------------------//
  //---------------------------------------------------------------------//

  static TransitionCurve toTransitionCurve(StageCurve curve) {
    switch (curve) {
      case StageCurve::EASE_IN: return TransitionCurve::EASE_IN;
      case StageCurve::EASE_OUT: return TransitionCurve::EASE_OUT;
      case StageCurve::EASE_IN_OUT: return TransitionCurve::EASE_IN_OUT;
      default: return TransitionCurve::LINEAR;
    }
  }

  static float orOff(float value) {
    return value > 0.f ? value : -1.f;
  }

  bool isSoak(const stage_t& stage) {
    // Pump (almost) off: lets the puck soak.
    return stage.end <= 0.5f && stage.rampTime <= 0.f;
  }

  bool hasExit(const stage_t& stage) {
    return stage.exitTime > 0.f || stage.exitPressureAbove > 0.f || stage.exitPressureBelow > 0.f || stage.exitWeight > 0.f;
  }

  int firstStageWithoutExit(const profile_t& profile) {
    for (int i = 0; i < profile.stageCount - 1 && i < MAX_STAGES; i++) {
      if (!hasExit(profile.stages[i])) return i;
    }
    return -1;
  }

  Phase toPhase(const stage_t& stage) {
    Transition target = stage.rampTime > 0.f
      ? Transition(stage.start, stage.end, toTransitionCurve(stage.curve), static_cast<long>(stage.rampTime * 1000.f))
      : Transition(stage.end);

    return Phase{
      .type = stage.type == StageType::FLOW ? PHASE_TYPE::PHASE_TYPE_FLOW : PHASE_TYPE::PHASE_TYPE_PRESSURE,
      .target = target,
      .restriction = orOff(stage.limit),
      .stopConditions = PhaseStopConditions{
        .time = stage.exitTime > 0.f ? static_cast<long>(stage.exitTime * 1000.f) : -1L,
        .pressureAbove = orOff(stage.exitPressureAbove),
        .pressureBelow = orOff(stage.exitPressureBelow),
        .weight = orOff(stage.exitWeight),
      },
      .tareAtStart = stage.tareAtStart,
      .tareAtEnd = stage.tareAtEnd,
    };
  }

  PredictiveHints buildPhases(const profile_t& stagesProfile, bool basketPrefill, Profile& out) {
    PredictiveHints hints;
    out.clear();

    if (basketPrefill) {
      // Fill the basket at 7 ml/s until there's any pressure.
      out.addPhase(Phase{
        .type = PHASE_TYPE::PHASE_TYPE_FLOW,
        .target = Transition(7.f),
        .restriction = -1,
        .stopConditions = PhaseStopConditions{ .pressureAbove = 0.1f },
        .tareAtStart = false,
        .tareAtEnd = false,
      });
    }
    int offset = out.phaseCount();

    int count = stagesProfile.stageCount;
    if (count < 1 || count > MAX_STAGES) count = 0;

    float preinfusionPressure = 0.f;
    for (int i = 0; i < count; i++) {
      const stage_t& stage = stagesProfile.stages[i];
      out.addPhase(toPhase(stage));

      if (hints.soakPhaseIdx >= 0) continue;
      if (i > 0 && isSoak(stage)) {
        hints.soakPhaseIdx = offset + i;
        hints.preinfusionPressure = preinfusionPressure;
      } else if (stage.type == StageType::PRESSURE) {
        preinfusionPressure = fmaxf(preinfusionPressure, stage.end);
      } else {
        preinfusionPressure = fmaxf(preinfusionPressure, stage.exitPressureAbove > 0.f ? stage.exitPressureAbove : stage.limit);
      }
    }
    if (hints.preinfusionPressure <= 0.f) hints.soakPhaseIdx = -1;

    if (out.phaseCount() == 0) {
      out.addPhase(Phase{ .type = PHASE_TYPE::PHASE_TYPE_PRESSURE, .target = Transition(9.f), .restriction = -1 });
    }
    return hints;
  }

  //---------------------------------------------------------------------//
  //--------------------------- Screen helpers --------------------------//
  //---------------------------------------------------------------------//

  // "9" or "8.5"
  static void fmtNum(char* buf, size_t len, float value) {
    int tenths = static_cast<int>(value * 10.f + 0.5f);
    if (tenths % 10 == 0) snprintf(buf, len, "%d", tenths / 10);
    else snprintf(buf, len, "%d.%d", tenths / 10, tenths % 10);
  }

  static void append(char* buf, size_t len, const char* text) {
    size_t used = strlen(buf);
    if (used + 1 >= len) return;
    strncat(buf, text, len - used - 1);
  }

  void formatSummary(const stage_t& stage, int number, char* buf, size_t len, bool needsExit) {
    if (len == 0) return;
    char a[16], b[16], part[64];
    bool flow = stage.type == StageType::FLOW;
    const char* unit = flow ? "ml" : "b";

    snprintf(buf, len, "%d %c ", number, flow ? 'F' : 'P');

    fmtNum(b, sizeof(b), stage.end);
    if (stage.rampTime > 0.f) {
      fmtNum(a, sizeof(a), stage.rampTime);
      if (stage.start > 0.f) {
        char s[16];
        fmtNum(s, sizeof(s), stage.start);
        snprintf(part, sizeof(part), "%s>%s%s/%ss", s, b, unit, a);
      } else {
        snprintf(part, sizeof(part), ">%s%s/%ss", b, unit, a);
      }
    } else {
      snprintf(part, sizeof(part), "%s%s", b, unit);
    }
    append(buf, len, part);
    append(buf, len, " ");

    if (stage.exitTime > 0.f) { fmtNum(a, sizeof(a), stage.exitTime); snprintf(part, sizeof(part), " %ss", a); append(buf, len, part); }
    if (stage.exitPressureAbove > 0.f) { fmtNum(a, sizeof(a), stage.exitPressureAbove); snprintf(part, sizeof(part), " >%sb", a); append(buf, len, part); }
    if (stage.exitPressureBelow > 0.f) { fmtNum(a, sizeof(a), stage.exitPressureBelow); snprintf(part, sizeof(part), " <%sb", a); append(buf, len, part); }
    if (stage.exitWeight > 0.f) { fmtNum(a, sizeof(a), stage.exitWeight); snprintf(part, sizeof(part), " %sg", a); append(buf, len, part); }
    if (needsExit && !hasExit(stage)) append(buf, len, " no exit!");
  }

  float estimatedDuration(const stage_t& stage, bool isLast, bool* isEstimate) {
    if (isEstimate) *isEstimate = false;
    if (stage.exitTime > 0.f) return stage.exitTime;
    if (isEstimate) *isEstimate = true;
    // No time limit: guess how long the other exit conditions take.
    float guess = isLast ? 25.f : 8.f;
    return fmaxf(guess, stage.rampTime);
  }

  Preview::Preview(const profile_t& p) : profile(p) {
    int count = profile.stageCount;
    if (count < 1 || count > MAX_STAGES) count = 0;

    float t = 0.f;
    float lastPressure = 0.f, lastFlow = 0.f;
    for (int i = 0; i < count; i++) {
      const stage_t& stage = profile.stages[i];
      bool guessed = false;
      startTimes[i] = t;
      t += estimatedDuration(stage, i == count - 1, &guessed);
      estimated = estimated || guessed;

      bool flow = stage.type == StageType::FLOW;
      float previous = flow ? lastFlow : lastPressure;
      startValues[i] = stage.start > 0.f ? stage.start : previous;
      if (flow) lastFlow = stage.end; else lastPressure = stage.end;
    }
    for (int i = count; i <= MAX_STAGES; i++) startTimes[i] = t;
    total = t;
  }

  PreviewSample Preview::sampleAt(float t) const {
    PreviewSample sample = { 0.f, 0.f, 0 };
    int count = profile.stageCount;
    if (count < 1 || count > MAX_STAGES) return sample;

    int i = 0;
    while (i < count - 1 && t >= startTimes[i + 1]) i++;
    const stage_t& stage = profile.stages[i];
    float inStage = t - startTimes[i];

    float target = stage.end;
    if (stage.rampTime > 0.f && inStage < stage.rampTime) {
      target = mapRange(inStage, 0.f, stage.rampTime, startValues[i], stage.end, 1, toTransitionCurve(stage.curve));
    }

    if (stage.type == StageType::FLOW) {
      sample.flow = target;
      sample.pressure = stage.limit;
    } else {
      sample.pressure = target;
      sample.flow = stage.limit;
    }
    sample.stage = i;
    return sample;
  }
}
