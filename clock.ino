extern bool portalValuesReady;
extern bool updatingPortalState;
bool clockStateDirty=true;
void configureTimezone() {
  TimeChangeRule rules[2]={};
  for(unsigned i=0;i<2;++i) {
    String prefix="tz_rule"+String(i+1)+"_";
    strcpy(rules[i].abbrev,i==0 ? "DST" : "STD");
    rules[i].week=portal.getAppConfigValue((prefix+"week").c_str()).asString().toInt();
    rules[i].dow=portal.getAppConfigValue((prefix+"day").c_str()).asString().toInt();
    rules[i].month=portal.getAppConfigValue((prefix+"month").c_str()).asString().toInt();
    rules[i].hour=portal.getAppConfigValue((prefix+"hour").c_str()).asDouble();
    rules[i].offset=lround(portal.getAppConfigValue((prefix+"offset").c_str()).asDouble()*60);
  }
  controllerTimezone.setRules(rules[0],rules[1]);
  clockStateDirty=true;
  mcuTimePending=true;
  forcePortalStateRefresh=true;
}
void applyLocalClock(const String& field,const String& text) {
  time_t now=time(nullptr);tm local={};
  if(now<=1609459200 || !configuredLocalTime(now,local)) {
    commandError("Wait for NTP before setting local date or time"); return;
  }
  unsigned a,b,c;int consumed=0;
  const char* format=field=="clock_date" ? "%4u-%2u-%2u%n" : "%2u:%2u:%2u%n";
  if(sscanf(text.c_str(),format,&a,&b,&c,&consumed)!=3 || consumed!=int(text.length())) {
    commandError("Invalid local date/time");return;
  }
  if(field=="clock_date") {local.tm_year=a-1900;local.tm_mon=b-1;local.tm_mday=c;}
  else {local.tm_hour=a;local.tm_min=b;local.tm_sec=c;}
  // TimeLib builds a timezone-independent epoch from local calendar components.
  tmElements_t parts={};parts.Year=local.tm_year+1900-1970;parts.Month=local.tm_mon+1;
  parts.Day=local.tm_mday;parts.Hour=local.tm_hour;parts.Minute=local.tm_min;parts.Second=local.tm_sec;
  time_t utc=controllerTimezone.toUTC(makeTime(parts));tm roundTrip={};
  if(!configuredLocalTime(utc,roundTrip) || roundTrip.tm_year!=local.tm_year ||
     roundTrip.tm_mon!=local.tm_mon || roundTrip.tm_mday!=local.tm_mday ||
     roundTrip.tm_hour!=local.tm_hour || roundTrip.tm_min!=local.tm_min || roundTrip.tm_sec!=local.tm_sec) {
    commandError("Invalid or nonexistent local date/time");return;
  }
  timeval current={utc,0};settimeofday(&current,nullptr);
  mcuTimePending=true;
}
void updateClockState() {
  if(!portalValuesReady) return;
  static time_t rendered=0;time_t now=time(nullptr);
  if(now<=1609459200 || (now==rendered && !clockStateDirty))return;
  clockStateDirty=false;rendered=now;tm local; if(!configuredLocalTime(now,local))return;
  TimeChangeRule* active=nullptr;controllerTimezone.toLocal(now,&active);
  char date[16],clock[12];strftime(date,sizeof(date),"%Y-%m-%d",&local);strftime(clock,sizeof(clock),"%H:%M:%S",&local);
  const char* days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  updatingPortalState=true;
  setReportedPortalValue("clock_date",date);setReportedPortalValue("clock_time",clock);
  setReportedPortalValue("clock_offset",String(active->offset/60.0,2));
  setReportedPortalValue("clock_weekday",days[local.tm_wday]);
  updatingPortalState=false;
}
