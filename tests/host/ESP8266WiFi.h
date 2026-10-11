#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct WiFiMock {
  int wifiStatus=WL_CONNECTED;
  int status(){return wifiStatus;}
  int RSSI(){return -50;}
};
extern WiFiMock WiFi;
