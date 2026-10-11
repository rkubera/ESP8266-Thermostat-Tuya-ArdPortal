#pragma once
#include <time.h>
#include <Timezone.h>

// System/SNTP clock stays in UTC. Convert only for the portal and Tuya MCU.
static Timezone controllerTimezone(
  TimeChangeRule{"DST",Last,Sun,Jan,0,0},
  TimeChangeRule{"STD",Last,Sun,Jan,0,0});
inline bool configuredLocalTime(time_t utc,struct tm& local) {
  time_t converted=controllerTimezone.toLocal(utc);
  return gmtime_r(&converted,&local)!=nullptr;
}
