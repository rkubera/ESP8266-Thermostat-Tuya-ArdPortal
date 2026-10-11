// Local settings use ArdPortal's journaled App Config. No old settings import.
bool bridgeConfigPending=false;
void markBridgeConfigForSave() { bridgeConfigPending=true; }
void syncPortalIdentity(const ArdPortal::PortalConfig& config) {
  strlcpy(mqtt_hostname,config.deviceName.c_str(),sizeof(mqtt_hostname));
}
bool bridgeHasWifiCredentials() { return portal.getPortalConfig().ssid.length()>0; }
bool bridgeApActive() { return portal.apActive(); }
