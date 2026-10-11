#include "optimistic_state.h"
OptimisticState optimisticState;
#pragma once
// Shared runtime state; included once by the sketch and the host integration harness.
#define MODE_OFF 0
#define MODE_HEAT 1
#define MODE_AUTO 2
#define AIR 0
#define FLOOR 1

char mqtt_hostname[65] = "thermostat";
ArdPortal portal;
uint32_t bridgeStateRevision=0;
bool forcePortalStateRefresh=false;
ESP8266WebServer diagnosticsServer(8080);

// User target persists independently of the MCU's effective target.
ThermostatControl thermostatControl(20.0f, HEAT_TARGET_MAX);
EnergyMeter energyMeter;
float currentTemperature = NAN;
ThermostatSchedule mcuSchedule;
bool scheduleKnown = false;
ThermostatSchedule scheduleDraft;
bool scheduleDraftReady = false, scheduleDraftDirty = false;
int mode = -1, tempSelector = -1;
int powerConsumption = 100;
bool relayDirection = false, relayKnown = false;
bool retainConfirmedZeroPower=false;
String heatingEventLog;
int lastLoggedHeatingState = -2;
bool mcuTimePending = true, mcuTimeSent = false;
uint32_t lastMcuTimeSent = 0;
uint32_t lastMcuRx = 0;
bool mcuSeen = false;
String diagnosticLog;
String lastCommandError;

uint8_t dpType[256] = {};
bool dpSeen[256] = {};
int32_t dpValue[256] = {};
uint32_t dpUpdated[256] = {};
uint8_t protocolVersion = 0;
int initStage = 0; // heartbeat, product, configuration, WiFi ACK, DP, ready
uint32_t initSent = 0, heartbeatSent = 0, querySent = 0, rxByteTime = 0;
int reportedStatusPin = -1;
uint8_t lastWifiCode = 255;
uint32_t wifiSent = 0;
bool wasMcuOnline = false;
// Sequential, bounded write queue; MCU reports confirm every write.
uint8_t writeIds[8], writeTypes[8];
int32_t writeValues[8];
uint8_t writeSchedule[8][24];
uint8_t writeHead = 0, writeCount = 0;
bool writePending = false;
bool forcePowerOff = false;
bool offRequested = false;
uint32_t offRetryAt = 0;
uint8_t writeRetries = 0;
uint32_t writeSent = 0, lastTx = 0;

void handleTuyaFrame(uint8_t command, uint8_t version, const uint8_t *payload, size_t length);
TuyaParser tuyaParser(handleTuyaFrame);
