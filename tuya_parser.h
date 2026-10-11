#pragma once
#include <stdint.h>
#include <stddef.h>
// Fixed-size streaming parser: no heap allocations, bounded packet length.
class TuyaParser {
 public:
  typedef void (*Handler)(uint8_t, uint8_t, const uint8_t *, size_t);
  explicit TuyaParser(Handler handler) : handler_(handler) {}
  void reset() { used_ = 0; }
  uint32_t errors = 0;
  void feed(uint8_t b) {
    if (used_ == 0) { if (b == 0x55) data_[used_++] = b; return; }
    if (used_ == 1 && b != 0xAA) {
      used_ = b == 0x55 ? 1 : 0;
      return;
    }
    data_[used_++] = b;
    if (used_ < 6) return;
    size_t payload = (size_t(data_[4]) << 8) | data_[5];
    if (payload > sizeof(data_) - 7) { ++errors; reset(); return; }
    if (used_ < payload + 7) return;
    uint8_t sum = 0;
    for (size_t i = 0; i < payload + 6; ++i) sum += data_[i];
    if (sum == b) handler_(data_[3], data_[2], data_ + 6, payload);
    else ++errors;
    reset();
  }
 private:
  uint8_t data_[519] = {};
  size_t used_ = 0;
  Handler handler_;
};
// A malformed DP report must not partially update application state.
inline bool tuyaDpReportValid(const uint8_t *p, size_t length) {
  while (length) {
    if (length < 4) return false;
    size_t n = (size_t(p[2]) << 8) | p[3];
    if (n > length - 4) return false;
    if ((p[1] == 1 || p[1] == 4) && n != 1) return false;
    if (p[1] == 2 && n != 4) return false;
    if (p[1] == 5 && n != 1 && n != 2 && n != 4) return false;
    if (p[1] > 5) return false;
    if (p[1] == 1 && p[4] > 1) return false;
    p += n + 4;
    length -= n + 4;
  }
  return true;
}
