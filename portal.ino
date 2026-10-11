unsigned portalRegistrationIndex=0;
bool portalRegistrationFailed=false, portalValuesReady=false;
static const char* const PORTAL_PAGE_DEFINITIONS[]={SETTINGS_PAGE,SCHEDULE_PAGE,CLOCK_PAGE};
static const uint8_t PORTAL_PAGE_BITS[]={2,4,8};
bool updatingPortalState=false;
std::bitset<96> priorityStateMask;
bool portalStateWriteFailed=false;
std::bitset<96> portalSeedRetryMask;
SchedulePreview schedulePreview;
ThermostatSchedule previewDraft=schedulePreview.saved();
bool previewDraftChanged=false;
uint32_t previewChangedAt=0;

void applyPortalControl(const String& fieldKey, const ArdJSON::JSONVar& value, ArdPortal::ChangeSource source) {
  if (updatingPortalState || source==ArdPortal::ChangeSource::Application || source==ArdPortal::ChangeSource::FactoryReset) return;
  if(source==ArdPortal::ChangeSource::Mqtt) {
    for(unsigned i=0;i<sizeof(PORTAL_STATE_FIELDS)/sizeof(PORTAL_STATE_FIELDS[0]);++i)
      if(fieldKey==PORTAL_STATE_FIELDS[i]) priorityStateMask.set(i);
    diagnostic("MQTT intent accepted at "+String(millis())+" ms: "+fieldKey+" = "+ArdJSON::JSON.stringify(value));
  }
  if(fieldKey.startsWith("tz_rule")) { configureTimezone(); return; }
  if(fieldKey=="clock_date" || fieldKey=="clock_time") { applyLocalClock(fieldKey,value.asString()); return; }
  String key=fieldKey;
  bool schedulePoint=key.indexOf("point_")>=0;
  String pointValue;
  if(schedulePoint) {
    unsigned index=key.substring(key.indexOf("point_")+6).toInt();
    if(index<1 || index>8) { commandError("Invalid schedule point"); return; }
    bool online=mcuOnline() && initStage==5;
    if(online && !scheduleDraftReady) { commandError("Schedule not yet received from MCU"); return; }
    ThermostatSchedule candidate=online ? scheduleDraft : previewDraft;
    uint8_t* point=candidate.bytes+(index-1)*3;
    if(key.endsWith("_temperature")) {
      double temperature=value.asDouble();
      if(!isfinite(temperature) || temperature<5 || temperature>40 || temperature!=floor(temperature)) {
        commandError("Expected whole degrees 5..40"); return;
      }
      point[2]=temperature;
    } else {
      String time=value.asString(); unsigned hour,minute,second=0; int consumed=0;
      int count=sscanf(time.c_str(),"%2u:%2u:%2u%n",&hour,&minute,&second,&consumed);
      if(count!=3 || consumed!=int(time.length()) || hour>23 || minute>59 || second!=0) {
        commandError("Expected HH:MM:00; MCU schedules use whole minutes"); return;
      }
      point[0]=hour; point[1]=minute;
    }
    char text[16];snprintf(text,sizeof(text),"%02u:%02u/%u",point[0],point[1],point[2]);
    pointValue=text;
    if(!online) previewDraft=candidate;
    key="point_"+String(index);
  }
  if (!mcuOnline() || initStage!=5) {
    // ArdPortal already holds the edited value. Keep it for local UI preview,
    // without queuing or persisting any intent to replay to the MCU.
    ++bridgeStateRevision;
    if (key.startsWith("point_") || key=="program" || key=="climate") { previewDraftChanged=true; previewChangedAt=millis(); }
    if (key=="rated_power") handleBridgeCommand("POWER_CONSUMPTION",String(value.asDouble(),0));
    return;
  }
  if (key=="climate") {
    String requestedMode=value["mode"].asString();
    int intendedMode=thermostatControl.pendingMode()!=ThermostatControl::Unknown ? thermostatControl.pendingMode() : mode;
    String confirmed=intendedMode==MODE_AUTO ? "auto" : intendedMode==MODE_HEAT ? "heat" : "off";
    // OFF always takes priority; do not queue a target first.
    if (requestedMode=="off") handleBridgeCommand("MODE","off");
    else {
      float target=value["temperature"].asDouble();
      if (fabsf(target-thermostatControl.normalTarget())>0.001f) handleBridgeCommand("TEMPERATURE",String(target,0));
      if (requestedMode!=confirmed) handleBridgeCommand("MODE",requestedMode);
    }
  } else if (key=="sensor") handleBridgeCommand("TEMPSELECTOR",value.asString());
  else if (key=="calibration") handleBridgeCommand("TEMPERATURE_OFFSET",String(value.asDouble(),0));
  else if (key=="panel_lock") handleBridgeCommand("LOCK",value.asBool() ? "ON" : "OFF");
  else if (key=="frost") handleBridgeCommand("FROST",value.asBool()?"ON":"OFF");
  else if (key=="differential") handleBridgeCommand("DIFFERENTIAL",String(value.asDouble(),0));
  else if (key=="lower_limit") handleBridgeCommand("LOWER_LIMIT",String(value.asDouble(),0));
  else if (key=="upper_limit") handleBridgeCommand("UPPER_LIMIT",String(value.asDouble(),0));
  else if (key=="rated_power") handleBridgeCommand("POWER_CONSUMPTION",String(value.asDouble(),0));
  else if (key=="program") handleBridgeCommand("PROGRAM_MODE",value.asString());
  else if (key.startsWith("point_")) handleBridgeCommand("SCHEDULE_POINT_"+key.substring(6),pointValue);
  if(source==ArdPortal::ChangeSource::Mqtt) portal.queueAppConfigStatePublish(fieldKey.c_str());
}
void publishPriorityState() {
  if(priorityStateMask.none() || !portal.mqttConnected()) return;
  for(unsigned i=0;i<sizeof(PORTAL_STATE_FIELDS)/sizeof(PORTAL_STATE_FIELDS[0]);++i) {
    if(!priorityStateMask.test(i))continue;
    const char* key=PORTAL_STATE_FIELDS[i];
    ArdJSON::JSONVar value=portal.getAppConfigValue(key);
    String payload=value.type()==ArdJSON::JSONVar::Type::String ? value.asString() : ArdJSON::JSON.stringify(value);
    if(String(key)=="panel_lock" || String(key)=="frost") payload=value.asBool()?"ON":"OFF";
    String topic="stat/"+String(mqtt_hostname)+"/"+key;
    if(portal.publish(topic.c_str(),payload.c_str(),true)) priorityStateMask.reset(i);
    return;
  }
}
void previewScheduleAutoSaveLoop() {
  if (!previewDraftChanged || uint32_t(millis()-previewChangedAt)<500) return;
  if (mcuOnline() && initStage==5) { previewDraftChanged=false; return; }
  String points;
  for(unsigned i=1;i<=8;++i) {
    if(i>1) points+=';';
    const uint8_t* p=previewDraft.bytes+(i-1)*3;
    char text[16];snprintf(text,sizeof(text),"%02u:%02u/%u",p[0],p[1],p[2]);
    points+=text;
  }
  bool ok=schedulePreview.save(points.c_str());
  previewDraftChanged=false;
  if(!ok) commandError("Invalid schedule preview: check increasing times and temperatures");
  ++bridgeStateRevision;
}
bool seedPortalField(const char* key) {
  ArdJSON::JSONVar value=portal.getAppConfigValue(key);
  updatingPortalState=true;
  bool ok=value.isValid() && !value.isUndefined() && portal.setAppConfigStateValue(key,value,false);
  updatingPortalState=false;
  return ok;
}
void seedPortalState() {
  portalSeedRetryMask.reset();
  for (unsigned i=0;i<sizeof(PORTAL_STATE_FIELDS)/sizeof(PORTAL_STATE_FIELDS[0]);++i)
    if (!seedPortalField(PORTAL_STATE_FIELDS[i])) portalSeedRetryMask.set(i);
}
void retryPortalSeed() {
  // Retry just one failed field per refresh; never repeat the whole form.
  static unsigned cursor=0;
  const unsigned count=sizeof(PORTAL_STATE_FIELDS)/sizeof(PORTAL_STATE_FIELDS[0]);
  for(unsigned n=0;n<count;++n) {
    unsigned i=cursor; cursor=(cursor+1)%count;
    if(!portalSeedRetryMask.test(i)) continue;
    if(seedPortalField(PORTAL_STATE_FIELDS[i])) portalSeedRetryMask.reset(i);
    return;
  }
}
void servicePortalRegistration() {
  if(portalRegistrationFailed || portal.appConfigPageRegistrationBusy()) return;
  if(portalRegistrationIndex<sizeof(PORTAL_PAGE_DEFINITIONS)/sizeof(PORTAL_PAGE_DEFINITIONS[0])) {
    if(!portal.startAppConfigPageRegistration(FPSTR(PORTAL_PAGE_DEFINITIONS[portalRegistrationIndex]))) {
      portalRegistrationFailed=true;
      diagnostic("ArdPortal registration failed: "+portal.appConfigRegistrationError().reason);
    }
    return;
  }
  if(!portalValuesReady && portal.portalAndAppConfigReady()) {
    seedPortalState();
    configureTimezone();
    portalValuesReady=true;
    ++bridgeStateRevision;
    forcePortalStateRefresh=true;
  }
}
void setupPortal() {
  portal.onAppConfigPageRegistrationFinished([](bool success) {
    if(success) {
      portalPageMask|=PORTAL_PAGE_BITS[portalRegistrationIndex];
      ++portalRegistrationIndex;
    } else {
      portalRegistrationFailed=true;
      diagnostic("ArdPortal registration failed: "+portal.appConfigRegistrationError().reason);
    }
  });
  portal.onBeforeRestart([](ArdPortal::RestartReason reason) {
    // Do not repopulate storage immediately after an explicit format.
    if(reason==ArdPortal::RestartReason::StorageFormat) return;
    updateEnergy(); // Include heating time up to the restart callback.
    if(!storeEnergy()) diagnostic("Unable to queue restart energy save");
    // ArdPortal flushes callback writes and waits for completion before restart.
  });
  portal.onAppConfigValueChanged(applyPortalControl);
  portal.onBeforeFactoryReset([]() {
    // MCU reset is best effort; unsupported/offline MCU must not block ESP reset.
    if(!requestMcuFactoryReset()) diagnostic("ESP factory reset continues; MCU reset not sent");
  });
  portal.onPortalConfigChanged([](const ArdPortal::PortalConfig& config,ArdPortal::ChangeSource) { syncPortalIdentity(config); });
  portal.onPortalAndAppConfigSaved([](bool ok,const String& error) {
    if (energySavePending) {
      if (ok) lastSavedEnergy=energySaveValue;
      energySavePending=false;
    }
    if (!ok) { bridgeConfigPending=true; diagnostic("ArdFS: "+error); }
  });
  portal.onPortalAndAppConfigReady([](bool loaded) {
    ++bridgeStateRevision;
    if (loaded) {
      double energy=portal.getAppConfigValue("energy_total").asDouble();
      energyMeter.restore(energy); lastSavedEnergy=energyMeter.total();
      int watts=portal.getAppConfigValue("bridge_settings")["rated_power"].asDouble();
      if (watts>=100 && watts<=5000) powerConsumption=watts;
      float target=portal.getAppConfigValue("bridge_settings")["normal_target"].asDouble();
      if (isfinite(target) && target>=5 && target<=HEAT_TARGET_MAX) thermostatControl.restore(target);
    }
    syncPortalIdentity(portal.getPortalConfig());
    bridgeConfigPending=true;
    // Page registration finishes asynchronously in portal.loop().
  });
  ArdPortal::Options options;
  options.deviceName=mqtt_hostname;
  options.tcpTimeoutMs=100;
  options.retryMs=5000;
  if (!portal.begin(options)) diagnostic("ArdPortal begin failed");
  else servicePortalRegistration();
}
void portalPersistenceLoop() {
  if (!portal.portalAndAppConfigReady() || portal.portalAndAppConfigBusy()) return;
  if (bridgeConfigPending) {
    ArdJSON::JSONVar settings=ArdJSON::JSONVar::object();
    settings["rated_power"]=powerConsumption;
    settings["normal_target"]=thermostatControl.normalTarget();
    if (portal.setAppConfigValue("bridge_settings",settings)) bridgeConfigPending=false;
  }
}
void setReportedPortalValue(const char* key,const ArdJSON::JSONVar& value) {
  // RAM state is available without reparsing a page. Avoid schema validation
  // and copying the state tree when the reported value has not changed.
  if (ArdJSON::JSON.stringify(portal.getAppConfigValue(key))==ArdJSON::JSON.stringify(value)) return;
  if (!portal.setAppConfigStateValue(key,value)) { portalStateWriteFailed=true; return; }
  if(String(key)=="power") {
    portal.queueAppConfigStatePublish("power");
    for(unsigned i=0;i<sizeof(PORTAL_STATE_FIELDS)/sizeof(PORTAL_STATE_FIELDS[0]);++i)
      if(String(PORTAL_STATE_FIELDS[i])=="power") priorityStateMask.set(i);
  }
}
void syncScheduleControls(const String& program,const ThermostatSchedule& schedule) {
  const char* prefix=program=="6+1" ? "monsat_point_" : program=="7" ? "daily_point_" : "point_";
  unsigned count=program=="7" ? 6 : 8;
  for(unsigned i=0;i<count;++i) {
    String key=String(program=="6+1" && i>=6 ? "sunday_point_" : prefix)+String(i+1);
    const uint8_t* point=schedule.bytes+i*3;
    char time[12];snprintf(time,sizeof(time),"%02u:%02u:00",point[0],point[1]);
    setReportedPortalValue(key.c_str(),time);
    setReportedPortalValue((key+"_temperature").c_str(),int(point[2]));
  }
}
void updatePortalState() {
  if (!portalValuesReady || !portal.portalAndAppConfigReady()) return;
  // State setters validate dynamic schemas even when values are unchanged.
  // Keep HTTP/OTA responsive with or without a connected MCU.
  static bool firstRefresh=true;
  static uint32_t lastRefresh=0;
  uint32_t now=millis();
  if (!forcePortalStateRefresh && !firstRefresh && uint32_t(now-lastRefresh)<250) return;
  bool forcedRefresh=forcePortalStateRefresh;
  forcePortalStateRefresh=false;
  firstRefresh=false; lastRefresh=now;
  if (portalSeedRetryMask.any()) retryPortalSeed();
  bool online=mcuOnline() && initStage==5;
  bool fresh=online && hasDp(DP_CURRENT,2) && uint32_t(millis()-dpUpdated[DP_CURRENT])<90000 && isfinite(currentTemperature);
  static bool stateRendered=false, renderedOnline=false, renderedFresh=false;
  static uint32_t renderedRevision=0;
  static double renderedEnergy=0;
  // Energy increases every loop while heating. Sampling it avoids rerunning
  // schema getters/setters for the entire UI every 250 ms just for this sensor.
  static uint32_t energyReportedAt=0;
  static double reportedEnergy=0;
  static bool energyReported=false;
  if(!energyReported || uint32_t(now-energyReportedAt)>=30000) {
    reportedEnergy=energyMeter.total(); energyReportedAt=now; energyReported=true;
  }
  double energy=reportedEnergy;
  if (!forcedRefresh && stateRendered && renderedRevision==bridgeStateRevision &&
      renderedOnline==online && renderedFresh==fresh && renderedEnergy==energy) return;
  stateRendered=true; renderedRevision=bridgeStateRevision;
  renderedOnline=online; renderedFresh=fresh; renderedEnergy=energy;
  updatingPortalState=true;
  portalStateWriteFailed=false;
  if (!online) {
    ArdJSON::JSONVar climate=portal.getAppConfigValue("climate");
    climate["current_temperature"]=nullptr;
    climate["action"]="unknown";
    String program=portal.getAppConfigValue("program").asString();

    setReportedPortalValue("climate",climate);


    setReportedPortalValue("mcu_fault","unknown");
    setReportedPortalValue("power","unknown");
    setReportedPortalValue("energy",String(energy,2));

    syncScheduleControls(program,previewDraft);

    stateRendered=!portalStateWriteFailed;
    updatingPortalState=false;
    return;
  }
  ArdJSON::JSONVar climate=ArdJSON::JSONVar::object();
  climate["temperature"]=online && hasDp(DP_TARGET,2) ? optimisticState.value(DP_TARGET,dpValue[DP_TARGET])*TARGET_SCALE : thermostatControl.normalTarget();
  if (fresh) climate["current_temperature"]=currentTemperature;
  else climate["current_temperature"]=nullptr;
  // Intermediate MCU reports must not undo an in-progress multi-DP mode change.
  int displayedMode=thermostatControl.displayedMode(hasDp(DP_POWER,1),optimisticState.value(DP_POWER,dpValue[DP_POWER])!=0,
    hasDp(DP_OPERATION_MODE,4),optimisticState.value(DP_OPERATION_MODE,dpValue[DP_OPERATION_MODE])==1);
  climate["mode"]=displayedMode==MODE_AUTO ? "auto" : displayedMode==MODE_HEAT ? "heat" : "off";
  climate["action"]=ThermostatControl::reportedAction(online,hasDp(DP_POWER,1),
    dpValue[DP_POWER]!=0,relayKnown,relayDirection,retainConfirmedZeroPower);

  int displayedProgram=optimisticState.value(DP_PROGRAM_MODE,hasDp(DP_PROGRAM_MODE,4)?dpValue[DP_PROGRAM_MODE]:-1);
  String program=displayedProgram==1?"5+2":displayedProgram==2?"6+1":displayedProgram==3?"7":"unknown";

  setReportedPortalValue("climate",climate);
  if (online && tempSelector>=0) setReportedPortalValue("sensor",optimisticState.value(DP_SENSOR,tempSelector)==0 ? "air" : optimisticState.value(DP_SENSOR,tempSelector)==1 ? "floor" : "both");
  if (online && hasDp(DP_CALIBRATION,2)) setReportedPortalValue("calibration",optimisticState.value(DP_CALIBRATION,dpValue[DP_CALIBRATION]));
  if (online && hasDp(DP_LOCK,1)) setReportedPortalValue("panel_lock",optimisticState.value(DP_LOCK,dpValue[DP_LOCK])!=0);
  if(hasDp(DP_FROST,1)) setReportedPortalValue("frost",optimisticState.value(DP_FROST,dpValue[DP_FROST])!=0);
  if(hasDp(DP_DIFFERENTIAL,2)) setReportedPortalValue("differential",optimisticState.value(DP_DIFFERENTIAL,dpValue[DP_DIFFERENTIAL]));
  if(hasDp(DP_LOWER_LIMIT,2)) setReportedPortalValue("lower_limit",optimisticState.value(DP_LOWER_LIMIT,dpValue[DP_LOWER_LIMIT]));
  if(hasDp(DP_UPPER_LIMIT,2)) setReportedPortalValue("upper_limit",optimisticState.value(DP_UPPER_LIMIT,dpValue[DP_UPPER_LIMIT]));
  // Fault bit meanings are not verified; never label guessed bits as sensor faults.
  setReportedPortalValue("mcu_fault",hasDp(DP_FAULT,5) ? (dpValue[DP_FAULT]==0 ? String("OK") : String("Fault bitmap: ")+String(uint32_t(dpValue[DP_FAULT]),HEX)) : String("unknown"));
  setReportedPortalValue("rated_power",powerConsumption);
  int watts=estimatedLoadPower(online,hasDp(DP_POWER,1),dpValue[DP_POWER]!=0,
    relayKnown,relayDirection,powerConsumption,retainConfirmedZeroPower);
  setReportedPortalValue("power",watts>=0 ? String(watts) : "unknown");
  // The accumulated total remains known even when the current relay state is not.
  setReportedPortalValue("energy",String(energy,2));

  if (program!="unknown") setReportedPortalValue("program",program);

  if(scheduleDraftReady) syncScheduleControls(program,scheduleDraft);

  stateRendered=!portalStateWriteFailed;
  updatingPortalState=false;
}
