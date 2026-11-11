#ifndef STAGES_H
#define STAGES_H

#include <stddef.h>
#include "../eeprom_data/eeprom_data.h"
#include "profiling_phases.h"

/**
 * Stage based brew profiles: editing helpers, conversion to profiler phases,
 * and data for the screen (row summaries and the preview graph).
 * No screen or hardware code in here, so it can be unit tested.
 */
namespace stages {
  using profile_t = eepromValues_t::profile_t;

  // ---- Editing the stage list ----

  // Inserts a stage after `index` (or at the end when index < 0). Returns the new index, -1 if full.
  int insertAfter(profile_t& profile, int index, const stage_t& stage);
  // Removes a stage. The last remaining stage can't be removed. Returns false when nothing changed.
  bool remove(profile_t& profile, int index);
  // Moves a stage one place up (delta -1) or down (+1). Returns the new index, or `index` if it can't move.
  int move(profile_t& profile, int index, int delta);

  // ---- Running a profile ----

  // Hints for the predictive scales, derived from the stages.
  struct PredictiveHints {
    int   soakPhaseIdx = -1;          // phase index of the first soak stage (pump ~off), -1 if none
    float preinfusionPressure = 0.f;  // highest pressure the stages before the soak aim for
  };

  bool isSoak(const stage_t& stage);
  // A stage ends on time, pressure or weight. Only the last stage may run without an exit
  // (it runs until the shot's target weight or the safety stop).
  bool hasExit(const stage_t& stage);
  // Index of the first stage (except the last) without an exit, or -1 if all are fine.
  int firstStageWithoutExit(const profile_t& profile);
  Phase toPhase(const stage_t& stage);
  // Replaces profile.phases with the stages (plus an optional basket prefill phase in front).
  PredictiveHints buildPhases(const profile_t& stagesProfile, bool basketPrefill, Profile& out);

  // ---- Screen helpers ----

  // One-line summary for the stage list, e.g. "3 P 0>9b/3s  3s". needsExit adds "no exit!" when it has none.
  void formatSummary(const stage_t& stage, int number, char* buf, size_t len, bool needsExit = false);

  // Preview: estimated stage durations (seconds) and sampled targets.
  float estimatedDuration(const stage_t& stage, bool isLast, bool* isEstimate = nullptr);

  struct PreviewSample {
    float pressure;  // bar (target or limit), 0 if none
    float flow;      // ml/s (target or limit), 0 if none
    int   stage;     // index of the stage this sample belongs to
  };

  class Preview {
  public:
    explicit Preview(const profile_t& profile);
    float totalSeconds() const { return total; }
    bool  hasEstimates() const { return estimated; }
    // t in seconds from shot start.
    PreviewSample sampleAt(float t) const;

  private:
    const profile_t& profile;
    float startTimes[MAX_STAGES + 1];
    float startValues[MAX_STAGES];  // resolved start values (a stage starting "from current")
    float total = 0.f;
    bool  estimated = false;
  };
}

#endif
