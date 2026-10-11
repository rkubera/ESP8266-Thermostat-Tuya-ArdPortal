#include "../schedule_days.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  auto days=scheduleDays("5+2");
  assert(!strcmp(days.primary,"Mon–Fri") && !strcmp(days.secondary,"Weekend (Sat–Sun)"));
  assert(days.primaryActive && days.secondaryActive);
  days=scheduleDays("6+1");
  assert(!strcmp(days.primary,"Mon–Sat") && !strcmp(days.secondary,"Sunday"));
  assert(days.primaryActive && days.secondaryActive);
  days=scheduleDays("7");
  assert(!strcmp(days.primary,"Every day (Mon–Sun)") && !strcmp(days.secondary,"Not used"));
  assert(days.primaryActive && !days.secondaryActive);
  for(const char* program:{"disabled","unknown"}) {
    days=scheduleDays(program); assert(!days.primaryActive && !days.secondaryActive);
  }
  std::cout<<"Schedule day names and point availability match 5+2, 6+1, daily and disabled programs\n";
}
