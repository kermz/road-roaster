#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <map>

inline std::string last_preference_key;
inline uint8_t last_preference_value = 0;
inline bool preference_write_succeeds = true;
inline bool preference_begin_succeeds = true;
inline unsigned preference_writes = 0;
inline std::map<std::string, uint8_t> preference_values;

class Preferences {
 public:
  bool begin(const char*, bool) { return preference_begin_succeeds; }
  uint8_t getUChar(const char* key, uint8_t fallback) {
    auto it = preference_values.find(key);
    return it == preference_values.end() ? fallback : it->second;
  }
  bool getBool(const char* key, bool fallback) { return getUChar(key, fallback) != 0; }
  size_t putUChar(const char* key, uint8_t value) {
    ++preference_writes;
    if (!preference_write_succeeds) return 0;
    last_preference_key = key;
    last_preference_value = value;
    preference_values[key] = value;
    return 1;
  }
  size_t putBool(const char* key, bool value) { return putUChar(key, value); }
};
