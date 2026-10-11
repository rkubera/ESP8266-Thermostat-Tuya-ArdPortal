#pragma once
#include <stdint.h>
#include <cmath>
// Only confirmed MCU reports determine estimated load; -1 means unknown.
inline int estimatedLoadPower(bool online, bool powerKnown, bool powerOn,
                              bool relayKnown, bool heating, unsigned watts, bool retainZero=false) {
  if(!online) return -1;
  if(powerKnown && !powerOn) return 0;
  // Keep the last confirmed zero until the MCU supplies a new relay report.
  if(retainZero) return 0;
  return relayKnown ? (heating ? int(watts) : 0) : -1;
}
// Estimate energy using configured watts and reported relay state, not a meter.
class EnergyMeter {
 public:
  void restore(double kwh) { total_ = std::isfinite(kwh) && kwh >= 0 ? kwh : 0; }
  void update(uint32_t now, bool known, bool heating, unsigned watts) {
    // Never bridge an interval with unknown state or backfill restart downtime.
    if (started_ && known_ && known && heating_)
      total_ += double(uint32_t(now - last_)) * watts_ / 3600000000.0;
    started_ = true; known_ = known; heating_ = heating; last_ = now; watts_ = watts;
  }
  double total() const { return total_; }
 private:
  double total_ = 0;
  uint32_t last_ = 0;
  unsigned watts_ = 0;
  bool started_ = false, known_ = false, heating_ = false;
};
