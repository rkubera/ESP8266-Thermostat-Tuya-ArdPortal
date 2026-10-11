#include "../thermostat_schedule.h"
#include <cassert>
#include <iostream>
int main(){
 ThermostatSchedule s;
 const char *valid="06:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15";
 assert(s.parse(valid));char text[96];s.format(text,sizeof(text));assert(!strcmp(text,valid));
 auto before=s;
 for(const char *bad:{"", "06:00/20", "06:00/20;", "24:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15",
 "06:00/20;05:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15",
 "06:00/20;08:60/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15",
 "06:00/20;08:00/15.5;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15",
 "06:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/15;",
 "06:00/20;08:00/15;11:30/15;12:30/15;17:00/22;22:00/15;08:00/22;23:00/99"}) {
 assert(!s.parse(bad));assert(!memcmp(s.bytes,before.bytes,24));
 }
 assert(!s.decode(before.bytes,23));
 std::cout<<"Schedule decoding, validation and round-trip tests passed\n";
}
