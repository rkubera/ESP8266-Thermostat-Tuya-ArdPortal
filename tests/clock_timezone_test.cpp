#include "../clock_timezone.h"
#include <cassert>
#include <cstdlib>
time_t utc(int year,int month,int day,int hour,int minute){
 tm value={};value.tm_year=year-1900;value.tm_mon=month-1;value.tm_mday=day;value.tm_hour=hour;value.tm_min=minute;return timegm(&value);
}
uint32_t fakeNow=0;
std::function<void(uint32_t)> onAdvance;
void advanceTime(uint32_t delta){fakeNow+=delta;}
int main(){
 setenv("TZ","UTC0",1);tzset();tm value;
 assert(configuredLocalTime(utc(2026,1,1,12,0),value)&&value.tm_hour==12);
 assert(configuredLocalTime(utc(2026,7,1,12,0),value)&&value.tm_hour==12);
 controllerTimezone.setRules(TimeChangeRule{"DST",Last,Sun,Mar,2,120},TimeChangeRule{"STD",Last,Sun,Oct,3,60});
 assert(configuredLocalTime(utc(2026,1,1,12,0),value)&&value.tm_hour==13);
 assert(configuredLocalTime(utc(2026,7,1,12,0),value)&&value.tm_hour==14);
 assert(configuredLocalTime(utc(2026,3,29,0,59),value)&&value.tm_hour==1&&value.tm_min==59);
 assert(configuredLocalTime(utc(2026,3,29,1,0),value)&&value.tm_hour==3);
 assert(configuredLocalTime(utc(2026,10,25,0,59),value)&&value.tm_hour==2&&value.tm_min==59);
 assert(configuredLocalTime(utc(2026,10,25,1,0),value)&&value.tm_hour==2&&value.tm_min==0);
 assert(configuredLocalTime(utc(2026,12,31,23,30),value)&&value.tm_year==127&&value.tm_mon==0&&value.tm_mday==1&&value.tm_hour==0);
 setenv("TZ","EST5",1);tzset();
 assert(configuredLocalTime(utc(2026,7,1,12,0),value)&&value.tm_hour==14);
 controllerTimezone.setRules(TimeChangeRule{"JST",Last,Sun,Mar,2,540},TimeChangeRule{"JST",Last,Sun,Oct,3,540});
 assert(configuredLocalTime(utc(2026,7,1,12,0),value)&&value.tm_hour==21);
 assert(configuredLocalTime(utc(2026,1,1,12,0),value)&&value.tm_hour==21);
 controllerTimezone.setRules(TimeChangeRule{"IST",Last,Sun,Mar,2,330},TimeChangeRule{"IST",Last,Sun,Oct,3,330});
 assert(configuredLocalTime(utc(2026,1,1,12,0),value)&&value.tm_hour==17&&value.tm_min==30);
}
