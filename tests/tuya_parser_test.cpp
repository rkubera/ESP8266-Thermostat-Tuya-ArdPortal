#include "../tuya_parser.h"
#include <cassert>
#include <vector>
#include <random>
#include <iostream>
static int calls;
static uint8_t command;
static std::vector<uint8_t> received;
static void handle(uint8_t c, uint8_t, const uint8_t *p, size_t n) {
  ++calls; command=c; received.assign(p,p+n);
}
static std::vector<uint8_t> frame(uint8_t cmd, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out={0x55,0xaa,3,cmd,uint8_t(payload.size()>>8),uint8_t(payload.size())};
  out.insert(out.end(),payload.begin(),payload.end());
  uint8_t sum=0; for(auto b:out) sum+=b; out.push_back(sum); return out;
}
static void feed(TuyaParser& parser,const std::vector<uint8_t>& bytes) { for(auto b:bytes) parser.feed(b); }
int main() {
  TuyaParser parser(handle);
  std::vector<uint8_t> dp={16,2,0,4,0,0,0,22,40,1,0,1,1,27,2,0,4,0xff,0xff,0xff,0xfe};
  auto packet=frame(7,dp);
  // Every fragmentation boundary, including an incomplete packet.
  for(size_t split=0;split<=packet.size();++split) {
    parser.reset(); calls=0;
    for(size_t i=0;i<split;++i) parser.feed(packet[i]);
    if(split<packet.size()) assert(calls==0);
    for(size_t i=split;i<packet.size();++i) parser.feed(packet[i]);
    assert(calls==1 && command==7 && received==dp);
  }
  assert(tuyaDpReportValid(dp.data(),dp.size()));
  assert(!tuyaDpReportValid(dp.data(),dp.size()-1));
  auto corrupt=packet; corrupt.back()^=1; calls=0;
  feed(parser,corrupt); assert(calls==0 && parser.errors==1);
  feed(parser,packet); assert(calls==1);
  calls=0;
  feed(parser,{0x00,0xff,0x55,0x55}); feed(parser,packet); assert(calls==1);
  calls=0;
  feed(parser,{0x55,0xaa,0,7,0xff,0xff}); feed(parser,packet); assert(calls==1 && parser.errors==2);
  calls=0; feed(parser,frame(0,{})); assert(calls==1 && received.empty());
  calls=0; feed(parser,frame(7,std::vector<uint8_t>(512,0))); assert(calls==1 && received.size()==512);
  calls=0; feed(parser,packet); feed(parser,packet); assert(calls==2);
  // An invalid bool or numeric type cannot pass the DP report validation.
  uint8_t badBool[]={40,1,0,1,2}; assert(!tuyaDpReportValid(badBool,sizeof badBool));
  uint8_t badInt[]={16,2,0,1,22}; assert(!tuyaDpReportValid(badInt,sizeof badInt));
  uint8_t badTail[]={1,1,0,1,1,16}; assert(!tuyaDpReportValid(badTail,sizeof badTail));
  uint8_t unknown[]={1,6,0,0}; assert(!tuyaDpReportValid(unknown,sizeof unknown));
  // Simulated partial-frame timeout/reset followed by recovery.
  parser.feed(0x55); parser.feed(0xaa); parser.reset(); calls=0;
  feed(parser,packet); assert(calls==1);
  // Malformed UART bytes must never exceed the fixed receive buffer.
  std::mt19937 random(81);
  for(int i=0;i<200000;++i) parser.feed(uint8_t(random()));
  parser.reset(); calls=0; feed(parser,packet); assert(calls==1);
  std::cout << "Tuya parser tests passed\n";
}
