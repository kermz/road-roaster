#pragma once
#include <cstdint>

inline uint32_t test_now_ms = 1000;
inline uint32_t millis() { return test_now_ms; }
inline uint32_t esp_random() { return 100; }
struct TestSerial {
  void println(const char*) {}
  template<class... Args> void printf(const char*, Args...) {}
};
inline TestSerial Serial;
