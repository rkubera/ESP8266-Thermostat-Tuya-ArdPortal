#include "../connection_indicator.h"
#include <cassert>
#include <iostream>
int main(){
 for(uint32_t t: {0u,999u,1000u,1999u,2000u,0xffffffffu}) {
   assert(!connectionIndicatorOn(t,false,false));
   assert(!connectionIndicatorOn(t,false,true));
   assert(connectionIndicatorOn(t,true,true));
 }
 assert(connectionIndicatorOn(0,true,false));
 assert(connectionIndicatorOn(999,true,false));
 assert(!connectionIndicatorOn(1000,true,false));
 assert(!connectionIndicatorOn(1999,true,false));
 assert(connectionIndicatorOn(2000,true,false));
 std::cout<<"WiFi/MQTT connection indicator tests passed\n";
}
