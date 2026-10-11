#include "../thermostat_control.h"
#include <cassert>
#include <iostream>
#include <cstring>
using Control = ThermostatControl;
int main() {
  // A stale heating report must not override confirmed MCU power-off.
  assert(std::strcmp(Control::reportedAction(true,true,false,true,true),"off")==0);
  assert(std::strcmp(Control::reportedAction(true,true,false,false,false),"off")==0);
  assert(std::strcmp(Control::reportedAction(true,true,true,true,true),"heating")==0);
  assert(std::strcmp(Control::reportedAction(true,true,true,true,false),"idle")==0);
  assert(std::strcmp(Control::reportedAction(true,true,true,false,false),"unknown")==0);
  assert(std::strcmp(Control::reportedAction(false,true,false,true,true),"unknown")==0);
  assert(std::strcmp(Control::reportedAction(true,true,true,true,true,true),"idle")==0);
  Control c(22,40);
  c.request(Control::Heat);
  assert(c.requestedTarget()==22);
  assert(!c.complete(true,true,true,false,true,22));
  assert(!c.complete(true,true,true,true,true,21));
  assert(c.complete(true,true,true,true,true,22));
  assert(c.reportedMode(true,true,true,true)==Control::Heat);
  c.request(Control::Auto);
  assert(c.reportedMode(true,true,true,true)==Control::Heat);
  assert(c.reportedMode(true,true,false,false)==Control::Unknown);
  assert(!c.complete(true,true,true,true,true,22));
  assert(c.displayedMode(true,true,true,true)==Control::Auto);
  assert(c.displayedMode(true,false,true,true)==Control::Auto);
  c.cancel();assert(c.displayedMode(true,true,true,true)==Control::Heat);
  c.request(Control::Auto);
  // Schedule confirmation does not depend on its current target.
  assert(c.complete(true,true,true,false,false,0));
  assert(c.reportedMode(true,true,true,false)==Control::Auto);
  assert(c.observeTarget(19)); assert(c.normalTarget()==19);
  c.request(Control::Heat); c.setNormalTarget(25);
  assert(!c.observeTarget(18));
  assert(!c.complete(true,true,true,true,true,19));
  assert(c.complete(true,true,true,true,true,25));
  c.request(Control::Off);
  assert(c.reportedMode(true,true,true,true)==Control::Heat);
  assert(!c.complete(true,true,true,true,true,25));
  assert(c.complete(true,false,false,false,false,0));
  assert(c.reportedMode(true,false,false,false)==Control::Off);
  c.restore(21); assert(c.pendingMode()==Control::Unknown);
  assert(c.reportedMode(true,true,true,false)==Control::Auto);
  assert(c.reportedMode(true,true,true,true)==Control::Heat);
  assert(c.reportedMode(false,false,true,true)==Control::Unknown);
  assert(!c.setNormalTarget(41)); assert(!c.setNormalTarget(4)); assert(!c.setNormalTarget(NAN));
  std::cout << "Thermostat manual/schedule mode transition tests passed\n";
}
