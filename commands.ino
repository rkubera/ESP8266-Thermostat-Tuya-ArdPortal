bool scheduleAutoSavePending=false;
uint32_t scheduleAutoSaveChangedAt=0;
const char *programModeName() {
  if (!hasDp(DP_PROGRAM_MODE,4)) return "unknown";
  switch(dpValue[DP_PROGRAM_MODE]) {
    case 0: return "disabled"; case 1: return "5+2";
    case 2: return "6+1"; case 3: return "7"; default: return "unknown";
  }
}

bool parseNumber(const String &text, float &value) {
  char *end;
  value = strtof(text.c_str(),&end);
  return end != text.c_str() && *end == '\0' && isfinite(value);
}
void handleBridgeCommand(const String& command, String value) {
  ++bridgeStateRevision;
  if (!value.length() || value.length()>32u) {
    commandError("Invalid command payload length"); return;
  }
  value.trim(); value.toLowerCase();
  float number;
  if (command.startsWith("SCHEDULE_POINT_")) {
    String index=command.substring(15);
    if(index.length()!=1 || index.c_str()[0]<'1' || index.c_str()[0]>'8' || !scheduleDraftReady || !mcuOnline() || !scheduleKnown) {
      commandError("Invalid/unavailable schedule point"); return;
    }
    int program=hasDp(DP_PROGRAM_MODE,4) ? dpValue[DP_PROGRAM_MODE] : -1;
    if (!(program == 1 || program == 2 || (program == 3 && index.c_str()[0] <= '6'))) return;
    // Point bounds are checked now; group chronology is checked on Save.
    if(value.length()<7 || value.length()>8) { commandError("Expected HH:MM/T, whole degrees 5..40"); return; }
    unsigned hour,minute,temp; int consumed=0;
    if(sscanf(value.c_str(),"%2u:%2u/%2u%n",&hour,&minute,&temp,&consumed)!=3 ||
       consumed!=int(value.length()) || value.c_str()[2]!=':' || value.c_str()[5]!='/' ||
       hour>23 || minute>59 || temp<5 || temp>40) {
      commandError("Expected HH:MM/T, whole degrees 5..40"); return;
    }
    for(unsigned j: {0u,1u,3u,4u,6u}) if(value.c_str()[j]<'0'||value.c_str()[j]>'9') {commandError("Invalid schedule point");return;}
    if(value.length()==8 && (value.c_str()[7]<'0'||value.c_str()[7]>'9')) {commandError("Invalid schedule point");return;}
    unsigned at=(index.c_str()[0]-'1')*3;
    scheduleDraft.bytes[at]=hour;scheduleDraft.bytes[at+1]=minute;scheduleDraft.bytes[at+2]=temp;
    scheduleDraftDirty=memcmp(scheduleDraft.bytes,mcuSchedule.bytes,24)!=0;
  } else if (command == "PROGRAM_MODE") {
    int requested=value=="disabled" ? 0 : value=="5+2" ? 1 : value=="6+1" ? 2 : value=="7" ? 3 : -1;
    if(requested<0) { commandError("Expected disabled, 5+2, 6+1 or 7"); return; }
    queueDp(DP_PROGRAM_MODE,4,requested);
  } else if (command == "MODE") {
    if (value == "heat") requestThermostatMode(MODE_HEAT);
    else if (value == "off") requestThermostatMode(MODE_OFF);
    else if (value == "auto") requestThermostatMode(MODE_AUTO);
    else commandError("Expected off, heat or auto");
  } else if (command == "TEMPERATURE") {
    if (!parseNumber(value,number) || number < targetLowerLimit() || number > targetUpperLimit() ||
        fabsf(number/TARGET_SCALE - roundf(number/TARGET_SCALE)) > 0.001f) {
      commandError("Invalid target temperature (profile limits/step required)"); return;
    }
    if (!canWriteDp(DP_TARGET,2)) return;
    thermostatControl.setNormalTarget(number);
    markBridgeConfigForSave();
    if (thermostatControl.pendingMode() == MODE_HEAT) requestThermostatMode(MODE_HEAT);
    else queueDp(DP_TARGET,2,int32_t(roundf(number/TARGET_SCALE)));
  } else if (command == "TEMPSELECTOR") {
    if (value == "air") queueDp(DP_SENSOR,4,0);
    else if (value == "floor") queueDp(DP_SENSOR,4,1);
    else if (value == "both") queueDp(DP_SENSOR,4,2);
    else commandError("Expected air, floor or both");
  } else if (command == "LOCK") {
    if (value == "on" || value == "1") queueDp(DP_LOCK,1,1);
    else if (value == "off" || value == "0") queueDp(DP_LOCK,1,0);
    else commandError("Expected ON or OFF");
  } else if (command == "TEMPERATURE_OFFSET") {
    if (!parseNumber(value,number) || number < -10 || number > 10 || number != roundf(number)) {
      commandError("Invalid MCU calibration (-10..10, whole degrees)"); return;
    }
    queueDp(DP_CALIBRATION,2,int32_t(number));
  } else if (command == "FROST") {
    if(value=="on" || value=="1") queueDp(DP_FROST,1,1);
    else if(value=="off" || value=="0") queueDp(DP_FROST,1,0);
    else commandError("Expected ON or OFF");
  } else if(command=="DIFFERENTIAL" || command=="LOWER_LIMIT" || command=="UPPER_LIMIT") {
    int minimum=command=="DIFFERENTIAL"?1:command=="LOWER_LIMIT"?5:30;
    int maximum=command=="DIFFERENTIAL"?5:command=="LOWER_LIMIT"?20:90;
    uint8_t id=command=="DIFFERENTIAL"?DP_DIFFERENTIAL:command=="LOWER_LIMIT"?DP_LOWER_LIMIT:DP_UPPER_LIMIT;
    if(!parseNumber(value,number) || number!=roundf(number) || number<minimum || number>maximum) {
      commandError("Invalid MCU setting range/step"); return;
    }
    queueDp(id,2,int32_t(number));
  } else if (command == "POWER_CONSUMPTION") {
    if (!parseNumber(value,number) || number < 100 || number > 5000 || number != roundf(number)) {
      commandError("Invalid rated power (100..5000 W)"); return;
    }
    updateEnergy(); // Split the interval at the wattage change.
    powerConsumption = int(number); markBridgeConfigForSave();
  } else commandError("Unknown bridge command: " + command);
  if(command.startsWith("SCHEDULE_POINT_") || command=="PROGRAM_MODE" || command=="MODE") {
    scheduleAutoSavePending=true; scheduleAutoSaveChangedAt=millis();
  }
}

bool saveScheduleDraft() {
  ++bridgeStateRevision;
  if (!scheduleDraftReady || !scheduleDraft.valid(scheduleDraft.bytes,24)) {
    commandError("Schedule times must increase within each group (6+2)"); return false;
  }
  if (scheduleWriteQueued()) { commandError("Schedule write awaiting confirmation"); return false; }
  return queueSchedule(scheduleDraft);
}
void scheduleAutoSaveLoop() {
  if(!scheduleAutoSavePending) return;
  // Offline preview must never become a delayed hardware command.
  if(!mcuOnline()) {scheduleAutoSavePending=false;return;}
  if(uint32_t(millis()-scheduleAutoSaveChangedAt)<500 || initStage!=5 ||
     !scheduleKnown || !scheduleDraftReady || scheduleWriteQueued() || offRequested || writeCount>=8) return;
  if(!scheduleDraft.valid(scheduleDraft.bytes,24)) {
    scheduleAutoSavePending=false;
    commandError("Schedule times must increase within each group (6+2)");return;
  }
  if(saveScheduleDraft()) scheduleAutoSavePending=false;
}
