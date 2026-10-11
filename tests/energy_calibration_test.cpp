#include "../energy_meter.h"
#include <cassert>
#include <iostream>
int main() {
  assert(estimatedLoadPower(true,true,false,false,false,1000)==0);
  assert(estimatedLoadPower(true,true,false,true,true,1000)==0);
  assert(estimatedLoadPower(true,true,true,false,false,1000)==-1);
  assert(estimatedLoadPower(true,true,true,true,true,1000)==1000);
  assert(estimatedLoadPower(true,true,true,true,false,1000)==0);
  assert(estimatedLoadPower(false,true,false,true,false,1000)==-1);
  assert(estimatedLoadPower(true,true,true,false,false,1000,true)==0);
  assert(estimatedLoadPower(true,true,true,true,true,1000,true)==0);
  assert(estimatedLoadPower(false,true,true,false,false,1000,true)==-1);

  EnergyMeter m;
  m.restore(3.5);
  m.update(0,false,false,1000);
  m.update(1000,true,true,1000);
  m.update(3601000,true,false,1000);
  assert(std::fabs(m.total()-4.5)<1e-9);
  m.update(7201000,true,false,1000); assert(std::fabs(m.total()-4.5)<1e-9);
  m.update(7202000,false,true,1000);
  m.update(10802000,true,true,1000); assert(std::fabs(m.total()-4.5)<1e-9);
  m.update(14402000,true,true,500); assert(std::fabs(m.total()-5.5)<1e-9);
  m.update(18002000,true,false,500); assert(std::fabs(m.total()-6.0)<1e-9);
  EnergyMeter restart; restart.restore(m.total());
  restart.update(900000,true,true,1000); assert(restart.total()==m.total());
  EnergyMeter wrap;
  wrap.update(0xfffffff0u,true,true,1000);
  wrap.update(0x10u,true,false,1000);
  assert(std::fabs(wrap.total()-32.0/3600000.0)<1e-12);
  std::cout << "Energy estimation tests passed\n";
}
