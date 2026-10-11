#pragma once
#include <cstdint>
class OptimisticState {
  struct Entry {uint8_t id=0;int32_t value=0;bool active=false;} entries[8];
public:
  void clear(){for(auto& e:entries)e.active=false;}
  void report(uint8_t id){for(auto& e:entries)if(e.id==id)e.active=false;}
  void request(uint8_t id,int32_t value){
    for(auto& e:entries)if(e.active&&e.id==id){e.value=value;return;}
    for(auto& e:entries)if(!e.active){e.id=id;e.value=value;e.active=true;return;}
  }
  int32_t value(uint8_t id,int32_t confirmed)const{
    for(const auto& e:entries)if(e.active&&e.id==id)return e.value;
    return confirmed;
  }
};
