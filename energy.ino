static double lastSavedEnergy = 0;
bool energySavePending=false;
double energySaveValue=0;

bool storeEnergy() {
  if (!portal.portalAndAppConfigReady() || portal.portalAndAppConfigBusy()) return false;
  double value=energyMeter.total();
  if (!portal.setAppConfigValue("energy_total",value)) return false;
  energySaveValue=value; energySavePending=true;
  return true;
}
void updateEnergy() {
  int watts=estimatedLoadPower(mcuOnline() && initStage==5,hasDp(DP_POWER,1),
    dpValue[DP_POWER]!=0,relayKnown,relayDirection,powerConsumption,retainConfirmedZeroPower);
  energyMeter.update(millis(),watts>=0,watts>0,powerConsumption);
}
void energyLoop() {
  static uint32_t lastSaveAttempt = 0;
  updateEnergy();
  uint32_t now = millis();
  if (energyMeter.total() > lastSavedEnergy && uint32_t(now-lastSaveAttempt) >= 3600000) {
    if (storeEnergy()) lastSaveAttempt=now;
  }
}
