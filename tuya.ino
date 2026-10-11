// Tuya protocol transport and MCU-confirmed state. No direct GPIO relay control.
void trafficDiagnostic(String line) {
  diagnosticLog += String(millis()) + " " + line + "\n";
  if (diagnosticLog.length() > 2048) {
    int cut = diagnosticLog.indexOf('\n', diagnosticLog.length() - 1536);
    diagnosticLog.remove(0, cut >= 0 ? cut + 1 : 1000);
  }
}
void diagnostic(String line) {
  portal.logMessage(line);
  trafficDiagnostic(line);
}
void heatingEvent(String event) {
  heatingEventLog += String(millis()) + " " + event + "\n";
  while (heatingEventLog.length() > 1024) {
    int cut = heatingEventLog.indexOf('\n');
    heatingEventLog.remove(0, cut + 1);
  }
}
void logHeatingState() {
  int state = relayKnown ? (relayDirection ? 1 : 0) : -1;
  if (state == lastLoggedHeatingState) return;
  lastLoggedHeatingState = state;
  heatingEvent(String("MCU heating: ") + (state < 0 ? "unknown" : state ? "heating (DP36=0)" : "idle (DP36=1)"));
}
void commandError(String error) {
  ++bridgeStateRevision;
  lastCommandError = error;
  diagnostic("ERROR " + error);
}
bool mcuOnline() { return mcuSeen && uint32_t(millis() - lastMcuRx) < 45000; }
bool hasDp(uint8_t id, uint8_t type) { return id && dpSeen[id] && dpType[id] == type; }

void tuyaSend(uint8_t cmd, const uint8_t *payload = nullptr, size_t length = 0) {
  uint8_t header[] = {0x55, 0xAA, 0, cmd, uint8_t(length >> 8), uint8_t(length)};
  uint8_t sum = 0;
  for (uint8_t b : header) sum += b;
  for (size_t i = 0; i < length; ++i) sum += payload[i];
  Serial.write(header, sizeof(header));
  if (length) Serial.write(payload, length);
  Serial.write(sum);
  lastTx = millis();
  String message="Tuya TX cmd="+String(cmd,HEX)+" length="+String(length);
  if(cmd==6 && length>=4) {
    unsigned n=(unsigned(payload[2])<<8)|payload[3];
    message+=" DP "+String(payload[0])+" type="+String(payload[1]);
    if(n<=4 && n+4<=length && payload[1]!=0) {
      uint32_t value=0;for(unsigned i=0;i<n;++i)value=(value<<8)|payload[4+i];
      message+=" value="+String(int32_t(value));
    }
  }
  if(cmd!=0 && length) {
    message+=" hex=";
    for(size_t i=0;i<length && i<32;++i) {
      if(payload[i]<16)message+='0';message+=String(payload[i],HEX);
    }
  }
  if(cmd==0) trafficDiagnostic(message);
  else diagnostic(message);
}
void invalidateMcuState() {
  optimisticState.clear();
  ++bridgeStateRevision;
  mcuTimePending = true; mcuTimeSent = false;
  memset(dpSeen, 0, sizeof(dpSeen));
  scheduleKnown = false; scheduleDraftReady = false; scheduleDraftDirty = false;
  mode = -1; tempSelector = -1;
  currentTemperature = NAN;
  thermostatControl.cancel();
  relayKnown = false;
  retainConfirmedZeroPower=false;
  forcePortalStateRefresh=true;
  logHeatingState();
  if (writeCount) commandError("MCU connection lost; pending commands cancelled");
  writeCount = 0; writePending = false; forcePowerOff = false;
}
void tuyaBegin() {
  if (TUYA_STATUS_PIN >= 0) {
    digitalWrite(TUYA_STATUS_PIN, TUYA_STATUS_INVERTED ? HIGH : LOW);
    pinMode(TUYA_STATUS_PIN, OUTPUT);
  }
  invalidateMcuState();
  initStage = 0;
  reportedStatusPin = -1;
  lastWifiCode = 255;
  tuyaSend(0);
  initSent = heartbeatSent = millis();
}
uint8_t wifiCode() {
  if (WiFi.status() != WL_CONNECTED) return 2;
  return protocolVersion >= 3 && portal.mqttConnected() ? 4 : 3;
}
void sendTuyaTime(uint8_t command) {
  time_t now = time(nullptr);
  uint8_t data[8] = {};
  if (now > 1609459200) {
    struct tm value;
    if (command == 0x1C) { if(!configuredLocalTime(now,value)) return; }
    else if(!gmtime_r(&now,&value)) return;
    data[0] = 1; data[1] = value.tm_year - 100;
    data[2] = value.tm_mon + 1; data[3] = value.tm_mday;
    data[4] = value.tm_hour; data[5] = value.tm_min; data[6] = value.tm_sec;
    data[7] = value.tm_wday == 0 ? 7 : value.tm_wday;
  }
  tuyaSend(command,data,command == 0x1C ? 8 : 7);
  if (command == 0x1C && data[0]) {
    mcuTimePending = false; mcuTimeSent = true; lastMcuTimeSent = millis();
    char timestamp[32];
    snprintf(timestamp,sizeof(timestamp),"%04u-%02u-%02u %02u:%02u:%02u",2000u+data[1],unsigned(data[2]),unsigned(data[3]),unsigned(data[4]),unsigned(data[5]),unsigned(data[6]));
    diagnostic("Local time sent (configured timezone): "+String(timestamp)+"; MCU acceptance unconfirmed");
  }
}
bool scheduleWriteQueued() {
  for(unsigned i=0;i<writeCount;++i) if(writeIds[(writeHead+i)%8]==DP_SCHEDULE)return true;
  return false;
}
void syncScheduleDraft() {
  if(scheduleKnown && !scheduleWriteQueued() &&
      (!scheduleDraftDirty || memcmp(scheduleDraft.bytes,mcuSchedule.bytes,24)==0)) {
    scheduleDraft=mcuSchedule;scheduleDraftReady=true;scheduleDraftDirty=false;
  }
}
void deriveMcuState() {
  ++bridgeStateRevision;

  tempSelector = hasDp(DP_SENSOR,4) ? dpValue[DP_SENSOR] : -1;
  // A changed sensor source must not relabel an earlier temperature report.
  bool currentValid = hasDp(DP_CURRENT,2) && tempSelector >= 0 && tempSelector <= 2 &&
    int32_t(dpUpdated[DP_CURRENT] - dpUpdated[DP_SENSOR]) >= 0;
  float rawCurrent = currentValid ? dpValue[DP_CURRENT] * CURRENT_SCALE : NAN;
  currentTemperature = rawCurrent; // MCU already applies its calibration.
  mode = thermostatControl.reportedMode(hasDp(DP_POWER,1), dpValue[DP_POWER] != 0,
    hasDp(DP_OPERATION_MODE,4), dpValue[DP_OPERATION_MODE] == 1);
  bool previousRelayKnown=relayKnown, previousRelayDirection=relayDirection;
  relayKnown = hasDp(DP_RELAY_REPORT,RELAY_REPORT_TYPE) &&
    (dpValue[DP_RELAY_REPORT] == RELAY_ON_VALUE || dpValue[DP_RELAY_REPORT] == RELAY_OFF_VALUE);
  if (relayKnown) relayDirection = dpValue[DP_RELAY_REPORT] == RELAY_ON_VALUE;
  if(previousRelayKnown!=relayKnown || (relayKnown && previousRelayDirection!=relayDirection))
    forcePortalStateRefresh=true;
  logHeatingState();
}
void handleTuyaFrame(uint8_t command, uint8_t version, const uint8_t *p, size_t length) {
  updateEnergy(); // Account for the previous relay state before applying a report.
  mcuSeen = true; lastMcuRx = millis();
  String received="Tuya RX cmd="+String(command,HEX)+" length="+String(length);
  if(command==0 || command==7 || command==0x22) trafficDiagnostic(received);
  else diagnostic(received);
  switch (command) {
    case 0:
      if (!length) return;
      protocolVersion = version;
      if (p[0] == 0) {
        invalidateMcuState(); initStage = 0; reportedStatusPin = -1;
        lastWifiCode = 255;
      }
      if (initStage == 0) { initStage = 1; initSent = millis(); tuyaSend(1); }
      break;
    case 1:
      // Product reply can identify firmware; never publish credentials.
      if (initStage == 1) { initStage = 2; initSent = millis(); tuyaSend(2); }
      break;
    case 2:
      if (initStage != 2) break;
      if (length >= 2) {
        reportedStatusPin = p[0];
        diagnostic("MCU status pin=" + String(p[0]) + " reset pin=" + String(p[1]));
        // WBR3 GPIO numbers are NOT automatically applied to ESP8266.
        if (TUYA_STATUS_PIN < 0) diagnostic("Status GPIO requires PCB wiring verification");
        initStage = 4; tuyaSend(8);
      } else {
        initStage = 3; uint8_t code = wifiCode(); tuyaSend(3,&code,1);
      }
      initSent = millis();
      break;
    case 3:
      if (initStage == 3) { initStage = 4; initSent = millis(); tuyaSend(8); }
      break;
    case 4: case 5:
      tuyaSend(command);
      diagnostic("Pairing requested from panel; use ArdPortal to configure network");
      break;
    case 7: case 0x22: {
      if (!tuyaDpReportValid(p,length)) { diagnostic("Malformed DP report rejected"); break; }
      bool sensorChanged = false, currentIncluded = false, targetIncluded = false;
      uint32_t reportTime = millis();
      const uint8_t *cursor = p; size_t remaining = length;
      while (remaining) {
        uint8_t id = cursor[0], type = cursor[1];
        size_t n = (size_t(cursor[2]) << 8) | cursor[3];
        uint32_t value = 0;
        if (n <= 4) for (size_t i = 0; i < n; ++i) value = (value << 8) | cursor[4+i];
        if (id == DP_SENSOR && (!dpSeen[id] || dpValue[id] != int32_t(value))) sensorChanged = true;
        if (id == DP_POWER && (!hasDp(DP_POWER,1) || type!=dpType[id] || dpValue[id]!=int32_t(value)))
          forcePortalStateRefresh=true;
        if(id==DP_POWER && type==1 && value==0) retainConfirmedZeroPower=true;
        if(id==DP_RELAY_REPORT && type==RELAY_REPORT_TYPE &&
           (int32_t(value)==RELAY_ON_VALUE || int32_t(value)==RELAY_OFF_VALUE)) {
          if(retainConfirmedZeroPower) forcePortalStateRefresh=true;
          retainConfirmedZeroPower=false;
        }
        if (id == DP_CURRENT) currentIncluded = true;
        if (id == DP_TARGET && type == 2) targetIncluded = true;
        if (id == DP_SCHEDULE) {
          scheduleKnown = type == 0 && mcuSchedule.decode(cursor+4,n);
          if (!scheduleKnown) diagnostic("Unsupported/invalid DP38 schedule");
        }
        optimisticState.report(id);
        dpSeen[id] = true; dpType[id] = type; dpValue[id] = int32_t(value);
        if (id != DP_SENSOR || sensorChanged) dpUpdated[id] = reportTime;
        String detail = "DP " + String(id) + " type=" + String(type) + " length=" + String(n);
        if (type == 1 || type == 2 || type == 4 || type == 5) detail += " value=" + String(int32_t(value));
        else {
          detail += " hex=";
          for (size_t i = 0; i < n && i < 32; ++i) {
            if (cursor[4+i] < 16) detail += '0';
            detail += String(cursor[4+i],HEX);
          }
        }
        diagnostic("Tuya RX "+detail);
        if (writePending && id == writeIds[writeHead] && type == writeTypes[writeHead] &&
            (type == 0 ? (id == DP_SCHEDULE && n == 24 &&
              memcmp(cursor+4,writeSchedule[writeHead],24) == 0) : int32_t(value) == writeValues[writeHead])) {
          if (id == DP_POWER && value == 0 && offRequested) offRequested = false;
          writeRetries = 0;
          writePending = false; writeHead = (writeHead + 1) % 8; --writeCount;
        }
        cursor += n + 4; remaining -= n + 4;
      }
      if (sensorChanged && !currentIncluded) dpSeen[DP_CURRENT] = false;

      if (command == 0x22) { uint8_t ok = 1; tuyaSend(0x23,&ok,1); }
      if (initStage >= 4) initStage = 5;
      syncScheduleDraft();
      finishModeTransaction();
      if (targetIncluded && thermostatControl.observeTarget(dpValue[DP_TARGET] * TARGET_SCALE)) markBridgeConfigForSave();
      deriveMcuState();
      break;
    }
    case 0x1C: case 0x0C: sendTuyaTime(command); break;
    case 0x0E: { uint8_t response[2] = {0,0}; tuyaSend(command,response,2); break; }
    case 0x24: { uint8_t response = uint8_t(WiFi.RSSI()); tuyaSend(command,&response,1); break; }
    case 0x2B: { uint8_t response = wifiCode(); tuyaSend(command,&response,1); break; }
    default: break; // Unknown commands remain visible in diagnostics.
  }
}
String tuyaDiagnostics() {
  String result = "Initialization stage: " + String(initStage) + "\nReported WBR3 status pin: " + String(reportedStatusPin) + "\n";
  result += "ESP clock: " + String(time(nullptr) > 1609459200 ? "valid" : "waiting for NTP") + "\n";
  result += "Local time transmission: " + String(mcuTimeSent ? "sent; acceptance unconfirmed" : "not sent") + "\n";
  result += "Local time pending: " + String(mcuTimePending ? "yes" : "no") + "\n";
  result += "WiFi saved credentials: " + String(bridgeHasWifiCredentials() ? "yes" : "no") + "\n";
  result += "WiFi connected: " + String(WiFi.status()==WL_CONNECTED ? "yes" : "no") + "\n";
  result += "WiFi configuration AP: " + String(bridgeApActive() ? "active" : "inactive") + "\n";
  result += "WiFi icon requested: " + String(WiFi.status()!=WL_CONNECTED ? "off" : portal.mqttConnected() ? "steady" : "blink 1s on/1s off") + "\n";
  result += "MQTT connected: " + String(portal.mqttConnected() ? "yes" : "no") + "\n";
  result += "MQTT state topic prefix: stat/" + String(mqtt_hostname) + "/\n";
  result += "MQTT/HA discovery and state: managed by ArdPortal\n";

  result += "DP36 report age (ms): " + (hasDp(DP_RELAY_REPORT,RELAY_REPORT_TYPE) ? String(uint32_t(millis()-dpUpdated[DP_RELAY_REPORT])) : String("unknown")) + "\n";
  result += "Pending writes: " + String(writeCount) + "\nReported datapoints:\n";
  for (int id = 1; id < 256; ++id) if (dpSeen[id]) {
    result += "DP " + String(id) + " type=" + String(dpType[id]) + " value=" + String(dpValue[id]) + "\n";
  }
  char scheduleText[96]; mcuSchedule.format(scheduleText,sizeof(scheduleText));
  result += "Schedule DP38: " + String(scheduleKnown ? scheduleText : "unknown") + "\n";
  result += "Program mode DP102 (0=disabled,1=5+2,2=6+1,3=7): " + String(hasDp(DP_PROGRAM_MODE,4) ? dpValue[DP_PROGRAM_MODE] : -1) + "\n";
  result += "User target: " + String(thermostatControl.normalTarget()) + "\n";
  result += "Target upper limit: " + String(HEAT_TARGET_MAX) + "\n";
  result += "Effective MCU target: " + String(hasDp(DP_TARGET,2) ? dpValue[DP_TARGET] * TARGET_SCALE : NAN) + "\n";
  result += "Pending mode: " + String(thermostatControl.pendingMode()) + "\n";
  result += "MCU calibration (assumed degrees C): " + (hasDp(DP_CALIBRATION,2) ? String(dpValue[DP_CALIBRATION]) : String("unknown")) + "\n";
  result += "Relay DP36: " + String(relayKnown ? (relayDirection ? "heating" : "idle") : "unknown") + "\n";
  result += "\nHeating transitions / MQTT transmission (not broker acknowledgement):\n" + heatingEventLog;
  return result;
}
bool canWriteDp(uint8_t id, uint8_t type) {
  if (!mcuOnline() || initStage != 5) { commandError("MCU not ready"); return false; }
  if (!hasDp(id,type)) { commandError("Unsupported/unconfirmed datapoint " + String(id)); return false; }
  return true;
}
bool queueDp(uint8_t id, uint8_t type, int32_t value) {
  if (offRequested && !(id == DP_POWER && type == 1 && value == 0)) {
    commandError("off awaiting confirmation; command not queued"); return false;
  }
  if (!canWriteDp(id,type)) return false;
  if (writeCount >= 8) { commandError("Command queue full"); return false; }
  uint8_t at = (writeHead + writeCount) % 8;
  writeIds[at] = id; writeTypes[at] = type; writeValues[at] = value; ++writeCount;
  optimisticState.request(id,value);
  return true;
}
bool queueSchedule(const ThermostatSchedule &schedule) {
  if (offRequested) { commandError("off awaiting confirmation; schedule not queued"); return false; }
  if (!canWriteDp(DP_SCHEDULE,0) || !scheduleKnown) {
    commandError("Valid MCU schedule required before editing"); return false;
  }
  if (writeCount >= 8) { commandError("Command queue full"); return false; }
  uint8_t at=(writeHead+writeCount)%8;
  writeIds[at]=DP_SCHEDULE; writeTypes[at]=0; writeValues[at]=0;
  memcpy(writeSchedule[at],schedule.bytes,24); ++writeCount; return true;
}
void finishModeTransaction() {
  if (writeCount || writePending || offRequested) return;
  if (thermostatControl.complete(hasDp(DP_POWER,1), dpValue[DP_POWER] != 0,
      hasDp(DP_OPERATION_MODE,4), dpValue[DP_OPERATION_MODE] == 1,
      hasDp(DP_TARGET,2), dpValue[DP_TARGET] * TARGET_SCALE)) {
    markBridgeConfigForSave(); deriveMcuState();
  }
}
bool requestThermostatMode(int requested) {
  if (offRequested && requested != MODE_OFF) {
    commandError("off awaiting confirmation; mode unchanged"); return false;
  }
  if (!canWriteDp(DP_POWER,1)) return false;
  if (requested != MODE_OFF && !canWriteDp(DP_OPERATION_MODE,4)) return false;
  if (requested == MODE_HEAT && !canWriteDp(DP_TARGET,2)) return false;
  bool enableSchedule=requested==MODE_AUTO && hasDp(DP_PROGRAM_MODE,4) && dpValue[DP_PROGRAM_MODE]==0;
  if(enableSchedule && !canWriteDp(DP_PROGRAM_MODE,4)) {
    commandError("Cannot enable MCU weekly schedule"); return false;
  }
  // New mode supersedes queued commands. In particular off cannot be cancelled
  // by a timeout of a previous temperature/mode write.
  offRequested = requested == MODE_OFF;
  // Always transmit off: the cached DP1 may predate an in-flight power-on.
  forcePowerOff = offRequested;
  offRetryAt = millis(); writeRetries = 0;
  writeHead = 0; writeCount = 0; writePending = false;
  optimisticState.clear();
  thermostatControl.request(requested);
  if (requested != MODE_OFF) markBridgeConfigForSave();
  if (requested == MODE_OFF) queueDp(DP_POWER,1,0);
  else {
    // Enable a disabled weekly program before entering automatic operation.
    if(enableSchedule) queueDp(DP_PROGRAM_MODE,4,1);
    queueDp(DP_OPERATION_MODE,4,requested == MODE_HEAT ? 1 : 0);
    if (requested == MODE_HEAT) queueDp(DP_TARGET,2,int32_t(roundf(thermostatControl.requestedTarget()/TARGET_SCALE)));
    queueDp(DP_POWER,1,1);
  }
  deriveMcuState();
  return true;
}
void tuyaLoop() {
  uint32_t now = millis();
  if (now - rxByteTime > 300) tuyaParser.reset();
  // An unconnected/noisy RX must not monopolize the loop and starve the portal.
  // The parser retains partial frames for the next pass.
  for (unsigned count=0;count<256 && Serial.available();++count) {
    tuyaParser.feed(Serial.read()); rxByteTime=millis();
  }
  bool online = mcuOnline();
  if (wasMcuOnline && !online) { invalidateMcuState(); initStage = 0; }
  wasMcuOnline = online;
  if (online && initStage == 5 && mode == MODE_AUTO &&
      hasDp(DP_PROGRAM_MODE,4) && dpValue[DP_PROGRAM_MODE] == 0 &&
      !offRequested && !writeCount && !writePending &&
      thermostatControl.pendingMode() == ThermostatControl::Unknown) {
    requestThermostatMode(MODE_HEAT);
  }
  if (TUYA_STATUS_PIN >= 0) digitalWrite(TUYA_STATUS_PIN,
    connectionIndicatorOn(millis(),WiFi.status() == WL_CONNECTED,portal.mqttConnected()) != TUYA_STATUS_INVERTED);
  if (now - lastTx < 30) return;
  if (writePending) {
    if (now - writeSent > 3000) {
      if (offRequested && writeIds[writeHead] == DP_POWER && writeRetries < 2) {
        // Some MCUs do not report an unchanged off write. Query DP state while
        // waiting; a matching DP1 report is still required for confirmation.
        ++writeRetries; writeSent = now; tuyaSend(8);
      } else {
        optimisticState.clear();
        commandError("MCU did not confirm DP " + String(writeIds[writeHead]) +
          (offRequested ? "; off retained for retry" : "; remaining commands cancelled"));
        writePending = false; writeCount = 0; offRetryAt = now;
        if (!offRequested) thermostatControl.cancel();
        deriveMcuState();
      }
    }
    return;
  }
  if (offRequested && !writeCount && initStage == 5 && hasDp(DP_POWER,1) &&
      uint32_t(now-offRetryAt) >= 5000) requestThermostatMode(MODE_OFF);
  // Idempotent commands need no new report; use the existing confirmed value.
  while (writeCount && !(forcePowerOff && writeIds[writeHead] == DP_POWER) &&
         hasDp(writeIds[writeHead],writeTypes[writeHead]) &&
         (writeTypes[writeHead] == 0 ? (scheduleKnown &&
           memcmp(mcuSchedule.bytes,writeSchedule[writeHead],24)==0) :
           dpValue[writeIds[writeHead]] == writeValues[writeHead])) {
    writeHead = (writeHead + 1) % 8; --writeCount;
  }
  if (writeCount) {
    uint8_t id = writeIds[writeHead], type = writeTypes[writeHead];
    uint32_t value = uint32_t(writeValues[writeHead]);
    uint8_t data[28] = {id,type,0,uint8_t(type == 0 ? 24 : type == 2 ? 4 : 1),0,0,0,0};
    if (type == 0) memcpy(data+4,writeSchedule[writeHead],24);
    else if (type == 2) for (int i=0;i<4;++i) data[4+i] = value >> (24-i*8);
    else data[4] = value;
    tuyaSend(6,data,type == 0 ? 28 : type == 2 ? 8 : 5);
    if (id == DP_POWER) forcePowerOff = false;
    writePending = true; writeSent = now;
    return;
  }
  finishModeTransaction();
  // Time is best effort and never overtakes an off/DP transaction. Send only
  // after initialization and with a valid clock, even if MCU never asks.
  if (online && initStage == 5 && !offRequested && time(nullptr) > 1609459200 &&
      (mcuTimePending || !mcuTimeSent || uint32_t(now-lastMcuTimeSent) >= 3600000)) {
    sendTuyaTime(0x1C); return;
  }
  if (initStage < 5 && now - initSent > 2000) {
    initSent = now;
    if (initStage <= 2) tuyaSend(initStage);
    else if (initStage == 3) { uint8_t code = wifiCode(); tuyaSend(3,&code,1); }
    else tuyaSend(8);
    return;
  }
  if (now - heartbeatSent > 15000) { heartbeatSent = now; tuyaSend(0); return; }
  if (initStage >= 3 && reportedStatusPin < 0 && now - wifiSent > 1000) {
    wifiSent = now;
    uint8_t code = wifiCode();
    if (code != lastWifiCode) { lastWifiCode = code; tuyaSend(3,&code,1); return; }
  }
  if (initStage == 5 && now - querySent > 30000) { querySent = now; tuyaSend(8); }
}
