#pragma once
#include "thermostat_schedule.h"

// Local UI preview never changes the MCU schedule or its acknowledged queue.
class SchedulePreview {
 public:
  SchedulePreview() {
    saved_.parse("06:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15");
  }
  bool save(const char* text) {
    ThermostatSchedule candidate;
    if (!candidate.parse(text)) return false;
    saved_=candidate;
    hasSaved_=true;
    return true;
  }
  const ThermostatSchedule& saved() const { return saved_; }
  bool hasSaved() const { return hasSaved_; }
 private:
  ThermostatSchedule saved_;
  bool hasSaved_=false;
};
