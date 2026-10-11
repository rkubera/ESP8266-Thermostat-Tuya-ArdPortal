#pragma once
#include <cmath>

// Heat = manual regulation; Auto = the factory MCU schedule.
class ThermostatControl {
 public:
  enum Mode { Unknown = -1, Off = 0, Heat = 1, Auto = 2 };
  ThermostatControl(float normal = 20, float maximum = 40): normal_(normal), maximum_(maximum) {}
  float normalTarget() const { return normal_; }
  int pendingMode() const { return pending_; }
  void restore(float normal) { normal_ = normal; pending_ = Unknown; }
  bool setNormalTarget(float value) {
    if (!std::isfinite(value) || value < 5 || value > maximum_) return false;
    normal_ = value; return true;
  }
  float requestedTarget() const { return normal_; }
  void request(int mode) { pending_ = mode; }
  void cancel() { pending_ = Unknown; }
  bool complete(bool powerKnown, bool power, bool manualKnown, bool manual,
                bool targetKnown, float target) {
    if (pending_ == Unknown || !powerKnown) return false;
    if (pending_ == Off) { if (power) return false; }
    else {
      if (!power || !manualKnown || manual != (pending_ == Heat)) return false;
      if (pending_ == Heat && (!targetKnown || std::fabs(target-normal_) > 0.001f)) return false;
    }
    pending_ = Unknown; return true;
  }
  bool observeTarget(float target) {
    if (pending_ != Unknown || !std::isfinite(target) || target < 5 || target > maximum_) return false;
    bool changed = std::fabs(normal_-target) > 0.001f;
    normal_ = target; return changed;
  }
  // A multi-DP mode change remains visible until it completes or is cancelled.
  int displayedMode(bool powerKnown, bool power, bool manualKnown, bool manual) const {
    return pending_ != Unknown ? pending_ : reportedMode(powerKnown,power,manualKnown,manual);
  }
  static const char* reportedAction(bool online, bool powerKnown, bool power,
                                    bool relayKnown, bool heating, bool retainZero=false) {
    if(!online) return "unknown";
    // Confirmed power-off supersedes the MCU's earlier heating report.
    if(powerKnown && !power) return "off";
    if(retainZero) return "idle";
    return relayKnown ? (heating ? "heating" : "idle") : "unknown";
  }
  int reportedMode(bool powerKnown, bool power, bool manualKnown, bool manual) const {
    if (!powerKnown) return Unknown;
    if (!power) return Off;
    if (!manualKnown) return Unknown;
    return manual ? Heat : Auto;
  }
 private:
  float normal_, maximum_;
  int pending_ = Unknown;
};
