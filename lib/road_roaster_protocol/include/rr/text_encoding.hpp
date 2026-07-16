#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rr {

struct Utf8CodePoint {
  uint32_t value;
  size_t byte_count;
  bool valid;
};

inline bool isUtf8Continuation(uint8_t value) {
  return (value & 0xC0U) == 0x80U;
}

inline Utf8CodePoint decodeUtf8(const char* text, size_t length) {
  if (text == nullptr || length == 0) return {0, 0, false};

  const auto first = static_cast<uint8_t>(text[0]);
  if (first < 0x80U) return {first, 1, true};

  if (first >= 0xC2U && first <= 0xDFU && length >= 2) {
    const auto second = static_cast<uint8_t>(text[1]);
    if (isUtf8Continuation(second)) {
      return {static_cast<uint32_t>((first & 0x1FU) << 6) |
                  static_cast<uint32_t>(second & 0x3FU),
              2, true};
    }
  }

  if (first >= 0xE0U && first <= 0xEFU && length >= 3) {
    const auto second = static_cast<uint8_t>(text[1]);
    const auto third = static_cast<uint8_t>(text[2]);
    const bool valid_second =
        isUtf8Continuation(second) &&
        (first != 0xE0U || second >= 0xA0U) &&
        (first != 0xEDU || second <= 0x9FU);
    if (valid_second && isUtf8Continuation(third)) {
      return {static_cast<uint32_t>((first & 0x0FU) << 12) |
                  static_cast<uint32_t>((second & 0x3FU) << 6) |
                  static_cast<uint32_t>(third & 0x3FU),
              3, true};
    }
  }

  if (first >= 0xF0U && first <= 0xF4U && length >= 4) {
    const auto second = static_cast<uint8_t>(text[1]);
    const auto third = static_cast<uint8_t>(text[2]);
    const auto fourth = static_cast<uint8_t>(text[3]);
    const bool valid_second =
        isUtf8Continuation(second) &&
        (first != 0xF0U || second >= 0x90U) &&
        (first != 0xF4U || second <= 0x8FU);
    if (valid_second && isUtf8Continuation(third) &&
        isUtf8Continuation(fourth)) {
      return {static_cast<uint32_t>((first & 0x07U) << 18) |
                  static_cast<uint32_t>((second & 0x3FU) << 12) |
                  static_cast<uint32_t>((third & 0x3FU) << 6) |
                  static_cast<uint32_t>(fourth & 0x3FU),
              4, true};
    }
  }

  // Consume one byte so callers always make progress without stepping beyond
  // the known input length.
  return {'?', 1, false};
}

inline bool isValidUtf8(const char* text) {
  if (text == nullptr) return false;
  const size_t length = std::strlen(text);
  for (size_t offset = 0; offset < length;) {
    const Utf8CodePoint code_point = decodeUtf8(text + offset, length - offset);
    if (!code_point.valid) return false;
    offset += code_point.byte_count;
  }
  return true;
}

inline bool isAscii(const char* text) {
  if (text == nullptr) return false;
  while (*text != '\0') {
    if (static_cast<uint8_t>(*text++) >= 0x80U) return false;
  }
  return true;
}

inline size_t encodeLatin1(const char* utf8, char* output, size_t capacity) {
  if (output == nullptr || capacity == 0) return 0;
  output[0] = '\0';
  if (utf8 == nullptr) return 0;

  const size_t length = std::strlen(utf8);
  size_t offset = 0;
  size_t written = 0;
  while (offset < length && written + 1 < capacity) {
    const Utf8CodePoint code_point = decodeUtf8(utf8 + offset, length - offset);
    output[written++] = static_cast<char>(
        code_point.valid && code_point.value <= 0xFFU ? code_point.value : '?');
    offset += code_point.byte_count;
  }
  output[written] = '\0';
  return written;
}

}  // namespace rr
