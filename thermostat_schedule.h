#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// DP38: six primary-day and two secondary-day (hour, minute, Celsius) points.
struct ThermostatSchedule {
  uint8_t bytes[24] = {};
  bool valid(const uint8_t *p, size_t n) const {
    if (n != 24) return false;
    for (unsigned i=0;i<8;++i) {
      if (p[i*3]>23 || p[i*3+1]>59 || p[i*3+2]<5 || p[i*3+2]>40) return false;
      if (i!=0 && i!=6 && (p[i*3]*60+p[i*3+1] <= p[(i-1)*3]*60+p[(i-1)*3+1])) return false;
    }
    return true;
  }
  bool decode(const uint8_t *p,size_t n) {
    if (!valid(p,n)) return false;
    memcpy(bytes,p,24); return true;
  }
  bool parse(const char *text) {
    uint8_t candidate[24];
    for(unsigned i=0;i<8;++i) {
      // Fixed HH:MM/T[T], points separated by ';'. No implicit rounding.
      if (strlen(text)<7 || text[0]<'0'||text[0]>'9'||text[1]<'0'||text[1]>'9'||text[2]!=':'||
          text[3]<'0'||text[3]>'9'||text[4]<'0'||text[4]>'9'||text[5]!='/') return false;
      candidate[i*3]=(text[0]-'0')*10+text[1]-'0';
      candidate[i*3+1]=(text[3]-'0')*10+text[4]-'0';text+=6;
      if(*text<'0'||*text>'9')return false;
      unsigned temperature=*text++-'0';
      if(*text>='0'&&*text<='9')temperature=temperature*10+*text++-'0';
      candidate[i*3+2]=temperature;
      if(i<7) { if(*text++!=';')return false; }
      else if(*text) return false;
    }
    return decode(candidate,24);
  }
  void format(char *out,size_t size) const {
    size_t at=0;
    for(unsigned i=0;i<8 && at<size;++i) {
      int n=snprintf(out+at,size-at,"%s%02u:%02u/%u",i?";":"",bytes[i*3],bytes[i*3+1],bytes[i*3+2]);
      if(n<0 || size_t(n)>=size-at) return;
      at+=n;
    }
  }
};
