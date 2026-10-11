#include "../optimistic_state.h"
#include <cassert>
int main(){
 OptimisticState s;
 s.request(16,25);assert(s.value(16,20)==25);
 s.request(16,26);assert(s.value(16,20)==26);
 s.report(24);assert(s.value(16,20)==26);
 s.report(16);assert(s.value(16,23)==23);
 s.request(2,0);s.request(1,1);s.report(1);
 assert(s.value(2,1)==0 && s.value(1,0)==0);
 s.clear();assert(s.value(2,1)==1);
}
