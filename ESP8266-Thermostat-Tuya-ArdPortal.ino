#include "portal_features.h"
#include <ArdPortal.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <coredecls.h>
#include <math.h>
#include "tuya_profile.h"
#include "connection_indicator.h"
#include "tuya_parser.h"
#include "thermostat_control.h"
#include "thermostat_schedule.h"
#include "schedule_preview.h"
#include "schedule_days.h"
#include "energy_meter.h"
#include "clock_timezone.h"
#include <bitset>
#include "bridge_state.h"
#include "portal_pages.h"
#include "portal_state_fields.h"

uint8_t portalPageMask=0;
uint32_t mqttConnections=0, mqttDisconnects=0, mqttChangedAt=0;
bool mqttWasConnected=false;
void observeMqttConnection() {
  bool connected=portal.mqttConnected();
  if(connected==mqttWasConnected) return;
  mqttWasConnected=connected; mqttChangedAt=millis();
  if(connected) ++mqttConnections; else ++mqttDisconnects;
}
uint32_t maxLoopUs=0, maxPortalUs=0, maxStateRefreshUs=0;

void setup() {
  // ArdPortal streams HTTP in small chunks. Disable Nagle for accepted sockets.
  WiFiClient::setDefaultNoDelay(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  Serial.begin(9600);
  delay(1000); // Retain the module settling delay; UART0 belongs to the MCU.
  tuyaBegin();
  setupPortal();
  diagnosticsServer.on("/",HTTP_GET,[]() {
    diagnosticsServer.send(200,"text/plain; charset=utf-8","ME81H bridge: /diagnostics\n");
  });
  diagnosticsServer.on("/ping",HTTP_GET,[]() {
    diagnosticsServer.send(200,"text/plain; charset=utf-8","OK\n");
  });
  diagnosticsServer.on("/diagnostics",HTTP_GET,[]() {
    String result="ME81H Tuya bridge\nUART 9600 8N1, TX GPIO1 / RX GPIO3\n";
    result+="Uptime seconds: "+String(millis()/1000)+"\n";
    result+="Reset reason: "+ESP.getResetReason()+"\n";
    result+="Free heap: "+String(ESP.getFreeHeap())+" bytes\n";
    result+="Largest heap block: "+String(ESP.getMaxFreeBlockSize())+" bytes\n";
    result+="Heap fragmentation: "+String(ESP.getHeapFragmentation())+"%\n";
    result+="MQTT successful connections: "+String(mqttConnections)+"\n";
    result+="MQTT observed disconnects: "+String(mqttDisconnects)+"\n";
    result+="MQTT last transition age (s): "+String(mqttChangedAt ? (millis()-mqttChangedAt)/1000 : 0)+"\n";
    result+="MQTT TLS enabled: "+String(portal.getPortalConfig().mqttTls ? "yes" : "no")+"\n";
    result+="WiFi RSSI: "+String(WiFi.RSSI())+" dBm\n";
    result+="Maximum loop time: "+String(maxLoopUs)+" us\n";
    result+="Maximum ArdPortal loop: "+String(maxPortalUs)+" us\n";
    result+="Maximum state refresh: "+String(maxStateRefreshUs)+" us\n";
    result+="Registered portal pages (expected 14): "+String(portalPageMask)+"\n";
    result+="Portal Climate mode: "+portal.getAppConfigValue("climate")["mode"].asString()+"\n";
    result+="Parser errors: "+String(tuyaParser.errors)+"\n";
    result+="Last command error: "+lastCommandError+"\n";
    result+=tuyaDiagnostics();
    result+="\nRecent traffic:\n"+diagnosticLog;
    diagnosticsServer.send(200,"text/plain; charset=utf-8",result);
  });
  diagnosticsServer.begin();
  settimeofday_cb([]() { mcuTimePending=true; });
}
void loop() {
  uint32_t loopStart=micros();
  tuyaLoop();
  energyLoop();
  uint32_t portalStart=micros();
  publishPriorityState();
  observeMqttConnection();
  portal.loop();
  servicePortalRegistration();
  observeMqttConnection();
  uint32_t portalTime=uint32_t(micros()-portalStart);
  if (portalTime>maxPortalUs) maxPortalUs=portalTime;
  tuyaLoop();
  scheduleAutoSaveLoop();
  previewScheduleAutoSaveLoop();
  portalPersistenceLoop();
  uint32_t stateStart=micros();
  updatePortalState();
  updateClockState();
  publishPriorityState();
  uint32_t stateTime=uint32_t(micros()-stateStart);
  if (stateTime>maxStateRefreshUs) maxStateRefreshUs=stateTime;
  diagnosticsServer.handleClient();
  yield();
  uint32_t loopTime=uint32_t(micros()-loopStart);
  if (loopTime>maxLoopUs) maxLoopUs=loopTime;
}
