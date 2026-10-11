#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <deque>
#include <vector>
#include <functional>
#include <algorithm>
using byte=uint8_t;
using boolean=bool;
#define HEX 16
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define FPSTR(p) (reinterpret_cast<const char*>(p))
class __FlashStringHelper;
#define memcpy_P memcpy
#define strcmp_P strcmp
#define strlen_P strlen
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_byte_near(p) (*(const uint8_t*)(p))
extern uint32_t fakeNow;
extern std::function<void(uint32_t)> onAdvance;
void advanceTime(uint32_t delta);
inline unsigned long millis() { return fakeNow; }
inline void yield() { advanceTime(1); }
inline int &lastDigitalPin(){static int value=-1;return value;}
inline int &lastDigitalValue(){static int value=-1;return value;}
inline void digitalWrite(int pin,int value) {lastDigitalPin()=pin;lastDigitalValue()=value;}
#define OUTPUT 1
inline int &lastPinMode(){static int value=-1;return value;}
inline void pinMode(int pin,int mode){lastDigitalPin()=pin;lastPinMode()=mode;}
class String {
 public:
  String()=default;
  String(char*s):s_(s?s:""){}
  String(const char*s):s_(s?s:""){}
  String(const __FlashStringHelper* p):s_(reinterpret_cast<const char*>(p)){}
  String(const std::string&s):s_(s){}
  String(char c):s_(1,c){}
  template<class T> String(T n):s_(std::to_string(n)){}
  template<class T> String(T n,int format) {
    std::ostringstream out;
    if(format==HEX) out<<std::hex<<uint64_t(n);
    else out<<std::fixed<<std::setprecision(format)<<double(n);
    s_=out.str();
  }
  bool reserve(size_t n){s_.reserve(n);return true;}
  char operator[](size_t i)const{return s_[i];}
  bool concat(char c){s_+=c;return true;}
  bool concat(const char* p,size_t n){s_.append(p,n);return true;}
  const char*c_str()const{return s_.c_str();}
  size_t length()const{return s_.size();}
  String substring(size_t start)const{return s_.substr(std::min(start,s_.size()));}
  String substring(size_t start,size_t end)const{return s_.substr(start,end-start);}
  bool startsWith(const String&s)const{return s_.find(s.s_)==0;}
  int indexOf(char c,size_t start=0)const{auto i=s_.find(c,start);return i==std::string::npos?-1:int(i);}
  int indexOf(const String &value,size_t start=0)const{auto i=s_.find(value.s_,start);return i==std::string::npos?-1:int(i);}
  void replace(const String& from,const String& to){if(!from.length())return;size_t pos=0;while((pos=s_.find(from.s_,pos))!=std::string::npos){s_.replace(pos,from.length(),to.s_);pos+=to.length();}}
  void remove(size_t start,size_t length=std::string::npos){s_.erase(start,length);}
  void trim(){auto a=s_.find_first_not_of(" \r\n\t"); auto b=s_.find_last_not_of(" \r\n\t");s_=a==std::string::npos?"":s_.substr(a,b-a+1);}
  void toLowerCase(){std::transform(s_.begin(),s_.end(),s_.begin(),[](char c){return std::tolower(c);});}
  String&operator+=(const String&s){s_+=s.s_;return *this;}
  friend String operator+(String a,const String&b){return a+=b;}
  friend bool operator==(const String&a,const String&b){return a.s_==b.s_;}
  friend bool operator!=(const String&a,const String&b){return !(a==b);}
 private:std::string s_;
};
struct SerialMock {
  void flush() {}
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  size_t maxBuffered=0;
  int available(){return rx.size();}
  int read(){auto b=rx.front();rx.pop_front();return b;}
  size_t write(uint8_t b){tx.push_back(b);return 1;}
  size_t write(const uint8_t*p,size_t n){tx.insert(tx.end(),p,p+n);return n;}
  void receive(const std::vector<uint8_t>&v){rx.insert(rx.end(),v.begin(),v.end());maxBuffered=std::max(maxBuffered,rx.size());}
};
extern SerialMock Serial;
