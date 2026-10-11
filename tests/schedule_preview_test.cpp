#include "../schedule_preview.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  SchedulePreview preview;
  assert(!preview.hasSaved());
  const ThermostatSchedule baseline=preview.saved();
  assert(baseline.valid(baseline.bytes,24));
  assert(!preview.save("06:00/20;05:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15"));
  assert(!preview.hasSaved() && !memcmp(baseline.bytes,preview.saved().bytes,24));
  assert(preview.save("06:30/21;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15"));
  assert(preview.hasSaved() && preview.saved().bytes[1]==30 && preview.saved().bytes[2]==21);
  const ThermostatSchedule saved=preview.saved();
  assert(!preview.save("06:30/99"));
  assert(!memcmp(saved.bytes,preview.saved().bytes,24));
  assert(memcmp(baseline.bytes,preview.saved().bytes,24));
  std::cout<<"Offline schedule preview validates saves and preserves the prior baseline on failure\n";
}
