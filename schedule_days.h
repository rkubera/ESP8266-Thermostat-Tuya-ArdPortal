#pragma once
#include <cstring>
struct ScheduleDays {
  const char* primary;
  const char* secondary;
  bool primaryActive;
  bool secondaryActive;
};
inline ScheduleDays scheduleDays(const char* program) {
  if (!strcmp(program,"5+2")) return {"Mon–Fri","Weekend (Sat–Sun)",true,true};
  if (!strcmp(program,"6+1")) return {"Mon–Sat","Sunday",true,true};
  if (!strcmp(program,"7")) return {"Every day (Mon–Sun)","Not used",true,false};
  if (!strcmp(program,"disabled")) return {"Schedule disabled","Not used",false,false};
  return {"Schedule unavailable","Schedule unavailable",false,false};
}
