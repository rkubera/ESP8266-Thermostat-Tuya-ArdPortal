#include "host/ESP8266WiFi.h"
#include "../tuya_profile.h"
#include "../connection_indicator.h"
#include "../tuya_parser.h"
#include "../thermostat_control.h"
#include "../thermostat_schedule.h"
#include "../energy_meter.h"
#include "../clock_timezone.h"
#include <cassert>
#include <iostream>
uint32_t fakeNow=100;
std::function<void(uint32_t)> onAdvance;
void advanceTime(uint32_t delta){fakeNow+=delta;if(onAdvance)onAdvance(delta);}
SerialMock Serial;
WiFiMock WiFi;
struct ESP8266WebServer {ESP8266WebServer(int){}};
struct ESPMock {uint32_t getChipId(){return 81;}} ESP;
struct ArdPortal {
 bool mqttOnline=false;
 bool mqttConnected()const{return mqttOnline;}
 std::vector<String> messages;
 void log(const String& message){messages.push_back(message);}
 void logMessage(const String& message){log(message);}
};
#include "../bridge_state.h"
int configWrites=0;
void markBridgeConfigForSave(){++configWrites;}
bool mcuOnline();
bool bridgeHasWifiCredentials();
bool bridgeApActive();
void updateEnergy(){energyMeter.update(millis(),mcuOnline()&&initStage==5&&relayKnown,relayDirection,powerConsumption);}
void energyLoop(){updateEnergy();}
void finishModeTransaction();
void tuyaLoop();
time_t fakeEpoch = 0;
time_t bridgeTestTime(time_t *out) { if(out) *out=fakeEpoch; return fakeEpoch; }
#define time bridgeTestTime
#include "../tuya.ino"
#undef time
#include "../commands.ino"
bool bridgeHasWifiCredentials(){return true;}
bool bridgeApActive(){return false;}
std::vector<uint8_t> frame(uint8_t cmd,const std::vector<uint8_t>&payload){
  std::vector<uint8_t> out={0x55,0xaa,3,cmd,uint8_t(payload.size()>>8),uint8_t(payload.size())};
  out.insert(out.end(),payload.begin(),payload.end());uint8_t sum=0;for(auto b:out)sum+=b;out.push_back(sum);return out;
}
void receive(uint8_t cmd,const std::vector<uint8_t>&p){Serial.receive(frame(cmd,p));tuyaLoop();}
std::vector<uint8_t> dp(uint8_t id,uint8_t type,int32_t value){
  if(type==2)return{id,type,0,4,uint8_t(uint32_t(value)>>24),uint8_t(uint32_t(value)>>16),uint8_t(uint32_t(value)>>8),uint8_t(value)};
  return{id,type,0,1,uint8_t(value)};
}
void report(uint8_t id,uint8_t type,int32_t value){receive(7,dp(id,type,value));}
void tick(uint32_t n=40){advanceTime(n);tuyaLoop();}
void command(const char*name,const char*value){
  if (String(name)=="SCHEDULE_SAVE") saveScheduleDraft();
  else if (String(name)=="SCHEDULE_RELOAD") { if(mcuOnline() && scheduleKnown && !scheduleWriteQueued()) {scheduleDraft=mcuSchedule;scheduleDraftReady=true;scheduleDraftDirty=false;} }
  else if (String(name)=="SCHEDULE") { ThermostatSchedule schedule; if(schedule.parse(value)) queueSchedule(schedule); }
  else handleBridgeCommand(name,value);
}
void initialize(bool power=true){
  receive(0,{0});assert(initStage==1);
  receive(1,{'{','}'});assert(initStage==2);
  receive(2,{14,0});assert(initStage==4);
  std::vector<uint8_t> all;
  for(auto p:{dp(1,1,power),dp(2,4,1),dp(16,2,22),dp(43,4,0),dp(24,2,205),dp(36,4,1),dp(40,1,0),dp(27,2,0)})all.insert(all.end(),p.begin(),p.end());
  receive(7,all);assert(initStage==5);assert(mcuOnline());
}
std::vector<std::vector<uint8_t>> sentWrites(){
  std::vector<std::vector<uint8_t>> result;
  for(size_t i=0;i+7<=Serial.tx.size();){assert(Serial.tx[i]==0x55&&Serial.tx[i+1]==0xaa);size_t n=(size_t(Serial.tx[i+4])<<8)|Serial.tx[i+5];assert(i+n+7<=Serial.tx.size());if(Serial.tx[i+3]==6)result.emplace_back(Serial.tx.begin()+i+6,Serial.tx.begin()+i+6+n);i+=n+7;}
  return result;
}
int main(){
  controllerTimezone.setRules(TimeChangeRule{"DST",Last,Sun,Mar,2,120},TimeChangeRule{"STD",Last,Sun,Oct,3,60});
  tuyaBegin();assert(lastDigitalPin()==14 && lastPinMode()==OUTPUT && lastDigitalValue()==HIGH);
  // Disconnected RX noise is bounded per pass; valid MCU traffic still recovers.
  Serial.receive(std::vector<uint8_t>(4096,0xff));
  tuyaLoop(); assert(Serial.rx.size()==4096-256);
  while (!Serial.rx.empty()) tuyaLoop();
  assert(!mcuOnline());
  initialize();assert(mode==MODE_HEAT&&currentTemperature==20.5f);
  // Integer MCU calibration: signed UART payload, no local correction or optimistic state.
  assert(sentWrites().empty());
  command("TEMPERATURE_OFFSET","-2");tick();
  assert(sentWrites()==std::vector<std::vector<uint8_t>>({dp(27,2,-2)}));
  assert(dpValue[27]==0&&currentTemperature==20.5f);
  report(27,2,-2);assert(!writeCount&&dpValue[27]==-2&&currentTemperature==20.5f);
  report(24,2,185);assert(currentTemperature==18.5f); // Already corrected MCU reading.
  command("TEMPERATURE_OFFSET","0.5");assert(!writeCount);
  command("TEMPERATURE_OFFSET","11");assert(!writeCount);
  Serial.tx.clear();
  // Normal queue confirmation and timeout.
  command("TEMPERATURE","23");tick();assert(writePending&&writeIds[writeHead]==16);
  report(16,2,23);assert(!writeCount&&!writePending);
  command("LOCK","ON");tick();advanceTime(3100);tuyaLoop();assert(!writeCount);assert(lastCommandError.length()>0);
  // Off supersedes an in-flight temperature command and a queued lock command.
  command("TEMPERATURE","24");tick();command("LOCK","ON");assert(writeCount==2);
  command("MODE","off");assert(offRequested&&writeCount==1&&!writePending);
  Serial.tx.clear();tick();assert(sentWrites()==std::vector<std::vector<uint8_t>>({dp(1,1,0)}));
  report(16,2,24);assert(offRequested&&writePending); // Late old acknowledgement.
  command("MODE","heat");assert(writeCount==1); // Cannot overtake pending off.
  report(1,1,0);assert(!offRequested&&mode==MODE_OFF);
  // In-flight ON with an old OFF cache: new OFF must still be transmitted.
  command("MODE","heat");tick(); // manual and target equal cache; ON is sent.
  assert(writePending&&writeIds[writeHead]==1&&dpValue[1]==0);
  command("MODE","off");Serial.tx.clear();tick();assert(sentWrites()==std::vector<std::vector<uint8_t>>({dp(1,1,0)}));
  report(1,1,1);assert(offRequested);report(1,1,0);assert(!offRequested);
  // Missing OFF ACK: two state queries, then retry, not silent cancellation.
  command("MODE","off");tick();
  for(int i=0;i<3;++i){advanceTime(3100);tuyaLoop();}
  assert(offRequested&&!writeCount);tick(5001);assert(writePending&&writeCount==1);
  report(1,1,0);assert(!offRequested);
  // Restart cancels ordinary writes but retains an outstanding OFF intent.
  command("MODE","off");tick();receive(0,{0});
  assert(offRequested&&initStage==1&&!writeCount&&!relayKnown&&std::isnan(currentTemperature));
  initialize(true);tick(5001);assert(writePending&&writeIds[writeHead]==1);report(1,1,0);assert(!offRequested);
  // MCU silence invalidates measurements; recovery does not replay ordinary ON.
  command("LOCK","ON");tick();advanceTime(46000);tuyaLoop();
  assert(!mcuOnline()&&!writeCount&&!relayKnown&&mode==-1);
  initialize(true);assert(!writeCount);
  // Manual heat and schedule auto use the real MQTT/queue/UART flow.
  command("TEMPERATURE","22");tick();report(16,2,22);
  command("MODE","heat");tick();assert(mode==MODE_HEAT&&!writeCount&&dpValue[16]==22);
  command("TEMPERATURE","25");tick();report(16,2,25);assert(thermostatControl.normalTarget()==25&&dpValue[16]==25);
  report(102,4,0);
  lastCommandError="";
  forcePortalStateRefresh=false;
  command("MODE","auto");
  assert(writeCount && mode==MODE_HEAT && lastCommandError.length()==0);
  assert(thermostatControl.pendingMode()==MODE_AUTO);
  assert(!forcePortalStateRefresh);
  // An unsolicited old mode report during DP102 must not roll back the UI.
  report(2,4,1);report(1,1,1);
  assert(thermostatControl.displayedMode(true,true,true,true)==MODE_AUTO);
  tick();assert(writeIds[writeHead]==102 && writeValues[writeHead]==1);
  report(102,4,1);
  assert(thermostatControl.displayedMode(true,true,true,true)==MODE_AUTO);
  tick();assert(writeIds[writeHead]==2 && writeValues[writeHead]==0);
  report(2,4,0);tick();assert(mode==MODE_AUTO&&!writeCount&&dpValue[16]==25);
  report(16,2,19);assert(mode==MODE_AUTO&&thermostatControl.normalTarget()==19);
  command("PROGRAM_MODE","disabled");tick();report(102,4,0);
  tick();assert(writeIds[writeHead]==2&&writeValues[writeHead]==1);
  report(2,4,1);tick();assert(mode==MODE_HEAT&&!writeCount);
  // Power-on preserves the last confirmed zero until a new relay report arrives.
  report(1,1,0);assert(retainConfirmedZeroPower);
  report(1,1,1);assert(retainConfirmedZeroPower);
  assert(estimatedLoadPower(true,true,true,relayKnown,relayDirection,100,retainConfirmedZeroPower)==0);
  // Heating history survives repeated target reports.
  forcePortalStateRefresh=false;
  report(36,4,0);assert(forcePortalStateRefresh && relayKnown && relayDirection && !retainConfirmedZeroPower);
  forcePortalStateRefresh=false;
  report(36,4,0);assert(!forcePortalStateRefresh); // unchanged report needs no priority refresh
  report(36,4,1);assert(forcePortalStateRefresh && relayKnown && !relayDirection);
  assert(heatingEventLog.indexOf("MCU heating: heating (DP36=0)")>=0);
  assert(heatingEventLog.indexOf("MCU heating: idle (DP36=1)")>=0);
  String events=heatingEventLog;
  for(int i=0;i<80;++i) report(16,2,25);
  assert(heatingEventLog==events);
  // Full raw schedule transport, confirmation, rejection and off priority.
  std::vector<uint8_t> scheduleBytes={6,0,20,8,0,15,11,30,15,12,30,15,17,0,22,22,0,15,8,0,22,23,0,15};
  auto scheduleReport=[&](std::vector<uint8_t> bytes){std::vector<uint8_t> payload={38,0,0,uint8_t(bytes.size())};payload.insert(payload.end(),bytes.begin(),bytes.end());receive(7,payload);};
  scheduleReport(scheduleBytes);report(102,4,1);assert(scheduleKnown);
  command("SCHEDULE","06:00/21;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15");
  Serial.tx.clear();tick();assert(writePending&&writeIds[writeHead]==38&&mcuSchedule.bytes[2]==20);
  auto expected=scheduleBytes;expected[2]=21;
  std::vector<uint8_t> expectedWrite={38,0,0,24};expectedWrite.insert(expectedWrite.end(),expected.begin(),expected.end());
  assert(sentWrites()==std::vector<std::vector<uint8_t>>({expectedWrite}));
  scheduleReport(scheduleBytes);assert(writePending); // Old report cannot confirm raw write.
  scheduleReport(expected);assert(!writePending&&!writeCount&&mcuSchedule.bytes[2]==21);
  command("PROGRAM_MODE","6+1");tick();report(102,4,2);assert(!writeCount);
  command("SCHEDULE","06:00/99;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15");assert(!writeCount);
  command("SCHEDULE","06:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15");tick();assert(writePending);
  command("MODE","off");assert(writeCount==1&&writeIds[writeHead]==1);tick();report(1,1,0);
  // Malformed raw format invalidates only schedule and cannot be written.
  scheduleReport({6,0,20});assert(!scheduleKnown);
  command("SCHEDULE","06:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15");assert(!writeCount);
  scheduleReport(expected);
  // Native discovery editor: drafts only write on Save and preserve newer edits.
  assert(scheduleDraftReady);
  command("SCHEDULE_POINT_1","07:00/23");assert(scheduleDraftDirty&&!writeCount&&scheduleDraft.bytes[2]==23);
  command("SCHEDULE_POINT_2","06:00/15");command("SCHEDULE_SAVE","PRESS");assert(!writeCount); // Out of order.
  command("SCHEDULE_POINT_2","08:00/15");command("SCHEDULE_SAVE","PRESS");tick();assert(writePending&&writeIds[writeHead]==38);
  auto savedDraft=std::vector<uint8_t>(scheduleDraft.bytes,scheduleDraft.bytes+24);
  command("SCHEDULE_POINT_1","07:00/24");scheduleReport(savedDraft);
  assert(!writeCount&&scheduleDraftDirty&&scheduleDraft.bytes[2]==24&&mcuSchedule.bytes[2]==23);
  command("SCHEDULE_RELOAD","PRESS");assert(!scheduleDraftDirty&&scheduleDraft.bytes[2]==23);
  command("SCHEDULE_POINT_1","07:00/25");command("SCHEDULE_SAVE","PRESS");tick();
  savedDraft.assign(scheduleDraft.bytes,scheduleDraft.bytes+24);scheduleReport(savedDraft);
  assert(!writeCount&&!scheduleDraftDirty&&mcuSchedule.bytes[2]==25);
  command("SCHEDULE_POINT_9","07:00/20");command("SCHEDULE_POINT_1","07:00/2.5");assert(!writeCount&&!scheduleDraftDirty);
  // Automatic save debounces edits, rejects invalid chronology, and queues DP38.
  command("SCHEDULE_POINT_1","07:00/26");
  advanceTime(499);scheduleAutoSaveLoop();assert(!writeCount&&scheduleAutoSavePending);
  advanceTime(1);scheduleAutoSaveLoop();assert(scheduleWriteQueued()&&!scheduleAutoSavePending);
  tick();savedDraft.assign(scheduleDraft.bytes,scheduleDraft.bytes+24);scheduleReport(savedDraft);
  assert(!writeCount&&!scheduleDraftDirty&&mcuSchedule.bytes[2]==26);
  command("SCHEDULE_POINT_2","06:00/15");
  advanceTime(500);scheduleAutoSaveLoop();assert(!writeCount&&!scheduleAutoSavePending);
  command("SCHEDULE_POINT_2","08:00/15");
  advanceTime(500);scheduleAutoSaveLoop();assert(scheduleWriteQueued());
  tick();savedDraft.assign(scheduleDraft.bytes,scheduleDraft.bytes+24);scheduleReport(savedDraft);
  // Unsolicited local time: no NTP means no automatic transmission.
  Serial.tx.clear();tick();assert(!mcuTimeSent);
  setenv("TZ","UTC0",1);tzset(); // ArdPortal runs SNTP in UTC.
  auto times=[](){std::vector<std::vector<uint8_t>> result;for(size_t i=0;i+7<=Serial.tx.size();){size_t n=(size_t(Serial.tx[i+4])<<8)|Serial.tx[i+5];if(Serial.tx[i+3]==0x1c)result.emplace_back(Serial.tx.begin()+i+6,Serial.tx.begin()+i+6+n);i+=n+7;}return result;};
  fakeEpoch=1767268800; // 2026-01-01 12:00 UTC -> Warsaw 13:00, Thursday.
  Serial.tx.clear();tick();assert(times()==std::vector<std::vector<uint8_t>>({{1,26,1,1,13,0,0,4}}));
  assert(mcuTimeSent&&!mcuTimePending);tick();assert(times().size()==1);
  // NTP update and summer time; no double transmission when MCU requests it.
  fakeEpoch=1782907200; // 2026-07-01 12:00 UTC -> Warsaw 14:00, Wednesday.
  mcuTimePending=true;Serial.tx.clear();tick();assert(times()==std::vector<std::vector<uint8_t>>({{1,26,7,1,14,0,0,3}}));
  Serial.tx.clear();receive(0x1c,{});tick();assert(times().size()==1);
  // Restart marks time pending; only send after initialization again.
  receive(0,{0});assert(!mcuTimeSent&&mcuTimePending);Serial.tx.clear();tick();assert(times().empty());
  initialize();Serial.tx.clear();tick();assert(times().size()==1);
  // An outstanding off request has priority over a new clock synchronization.
  mcuTimePending=true;command("MODE","off");Serial.tx.clear();tick();assert(times().empty());
  report(1,1,0);tick();assert(times().size()==1);
  // Periodic resend; UART frame/checksum validated by the existing frame walker.
  Serial.tx.clear();lastMcuTimeSent=millis()-3600000u;tick();assert(times().size()==1);
  // Actual active-low GPIO status: off, blink without broker, steady with broker.
  WiFi.wifiStatus=0;tuyaLoop();assert(lastDigitalPin()==14&&lastDigitalValue()==HIGH);
  WiFi.wifiStatus=WL_CONNECTED;portal.mqttOnline=false;
  fakeNow=(fakeNow/2000+1)*2000;tuyaLoop();assert(lastDigitalValue()==LOW);
  fakeNow+=1000;tuyaLoop();assert(lastDigitalValue()==HIGH);
  portal.mqttOnline=true;
  tuyaLoop();assert(lastDigitalValue()==LOW);
  portal.messages.clear();tuyaSend(0);
  assert(portal.messages.empty());
  uint8_t setting[]={40,1,0,1,1};tuyaSend(6,setting,sizeof(setting));
  assert(portal.messages.back().indexOf("DP 40 type=1 value=1")>=0);
  report(24,2,235);
  assert(portal.messages.back().indexOf("Tuya RX DP 24")>=0);
  // Advanced settings require a matching report; invalid values never reach UART.
  Serial.tx.clear();command("FROST","ON");assert(!writeCount);
  for(auto p:{dp(10,1,0),dp(101,2,1),dp(19,2,40),dp(26,2,10),dp(45,5,0)}) receive(7,p);
  for(auto item:std::vector<std::pair<const char*,uint8_t>>{{"FROST",10},{"DIFFERENTIAL",101},{"LOWER_LIMIT",26},{"UPPER_LIMIT",19}}) {
    const char* value=item.second==10?"ON":item.second==101?"2":item.second==26?"12":"50";
    int expected=item.second==10?1:item.second==101?2:item.second==26?12:50;
    Serial.tx.clear();command(item.first,value);tick();
    assert(sentWrites()==std::vector<std::vector<uint8_t>>({dp(item.second,item.second==10?1:2,expected)}));
    report(item.second,item.second==10?1:2,expected);assert(!writeCount);
  }
  command("DIFFERENTIAL","0");command("DIFFERENTIAL","1.5");command("LOWER_LIMIT","21");command("UPPER_LIMIT","91");
  command("TEMPERATURE","11");command("TEMPERATURE","51");assert(!writeCount);
  command("TEMPERATURE","45");tick();report(16,2,45);assert(!writeCount);
  assert(!requestMcuFactoryReset()); // Our original MCU logs do not report DP39.
  report(39,1,0);Serial.tx.clear();assert(requestMcuFactoryReset());
  assert(sentWrites()==std::vector<std::vector<uint8_t>>({dp(39,1,1)}));
  assert(!writeCount && initStage==0 && !hasDp(39,1));
  tick(4000);assert(sentWrites().size()==1); // Reset cannot be retried.
  std::cout<<"Bridge integration tests passed: UART, off priority, timeout, MCU restart, schedule, clock and connection indicator\n";
}
