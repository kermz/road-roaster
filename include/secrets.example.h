#pragma once

#include <array>
#include <cstdint>

// Copy this file to include/secrets.h, set kConfigured to true, replace both
// peer MAC addresses, and generate private 16-byte PMK and LMK values.
// include/secrets.h is intentionally ignored by git.
namespace rr::secrets {

inline constexpr bool kConfigured = false;
inline constexpr uint8_t kWifiChannel = 6;

inline constexpr std::array<uint8_t, 6> kControllerPeerMac = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

inline constexpr std::array<uint8_t, 6> kRearDisplayPeerMac = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

inline constexpr std::array<uint8_t, 16> kPrimaryMasterKey = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

inline constexpr std::array<uint8_t, 16> kLocalMasterKey = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

}  // namespace rr::secrets

