#pragma once

#include <cstdint>

#include "preset_definition.hpp"
#include "rr/protocol.hpp"

// Edit this file to customize visible text and colors, then rebuild/reflash the
// affected board. Keep preset IDs stable: controller duration preferences use ID.
// Controller copy needs glyphs supported by the enabled LVGL fonts. Catalog
// labels must be ASCII, 1..31 bytes; rear screens are UTF-8, 1..512 bytes each.
namespace rr::presentation {

namespace theme {
inline constexpr uint32_t kBackground = 0x050708;
inline constexpr uint32_t kSurface = 0x111619;
inline constexpr uint32_t kSurfacePressed = 0x19231F;
inline constexpr uint32_t kDivider = 0x283236;
inline constexpr uint32_t kTextPrimary = 0xF7FAF8;
inline constexpr uint32_t kTextMuted = 0x7E8985;
inline constexpr uint32_t kAccent = 0x59E391;
inline constexpr uint32_t kWarning = 0xF2AD49;
inline constexpr uint32_t kError = 0xFF5C5C;
inline constexpr uint32_t kModalSurface = 0x0D1315;
inline constexpr uint32_t kPrimaryAction = 0x17603A;
inline constexpr uint32_t kPrimaryActionPressed = 0x20794A;
inline constexpr uint32_t kDestructivePressed = 0x361719;
}  // namespace theme

namespace text {
inline constexpr char kBrand[] = "ROAD ROASTER";
inline constexpr char kClear[] = "Clear";
inline constexpr char kDurationTitle[] = "DURATION";
inline constexpr char kCancel[] = "Cancel";
inline constexpr char kSave[] = "Save";
inline constexpr char kDisplayTitle[] = "DISPLAY";
inline constexpr char kController[] = "CONTROLLER";
inline constexpr char kRearDisplay[] = "REAR DISPLAY";
inline constexpr char kFlip[] = "FLIP";
inline constexpr char kBack[] = "Back";
inline constexpr char kSyncing[] = "Syncing messages";
inline constexpr char kSetupRequired[] = "Radio setup required";
inline constexpr char kRadioFailure[] = "Radio initialization failed";
inline constexpr char kRearUnavailable[] = "Rear unavailable";
inline constexpr char kSending[] = "Sending";
inline constexpr char kCommandFailed[] = "Command failed";
inline constexpr char kUnknown[] = "Unknown";
inline constexpr char kDefaultDuration[] = "Default";
inline constexpr char kSaveFailed[] = "Save failed - retry";
inline constexpr char kBatteryUnavailable[] = "--%";
inline constexpr char kValueUnavailable[] = "--";
inline constexpr char kPageUnavailable[] = "- / -";
inline constexpr char kPercentSuffix[] = "%";
inline constexpr char kSecondsSuffix[] = "s";
inline constexpr char kPageSeparator[] = " / ";
inline constexpr char kStatusSeparator[] = " \xE2\x80\xA2 ";  // LVGL bullet glyph.
}  // namespace text

namespace matrix {
using rear::AnimationKind;
using rear::PresetDefinition;
using rear::RgbColor;

// ColorCycle uses these stops instead of a preset's RGB. Static/Pulse use RGB.
inline constexpr RgbColor kCycleGreen{0, 255, 0};
inline constexpr RgbColor kCycleRed{255, 0, 0};
inline constexpr RgbColor kCycleBlue{0, 0, 255};

// One to four contiguous screens per preset; each string is one screen.
inline constexpr PresetDefinition kPresets[] = {
    {1, "Sell it", {{"SELL IT"}}, AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
    {2, "Thanks", {{"Thanks", "Aitäh"}}, AnimationKind::Static, {255, 0, 0},
     kDefaultDurationMs},
    {3, "Sorry", {{"Sorry", "Vabandust"}}, AnimationKind::Static,
     {50, 220, 255},
     kDefaultDurationMs},
    {4, "Wanna race?", {{"You seem fast", "prove it"}},
     AnimationKind::Static, {255, 255, 255},
     kDefaultDurationMs},
    {5, "Koer oled?", {{"KOER OLED?", "HOIA PIKIVAHET"}},
     AnimationKind::Static, {100, 255, 120},
     kDefaultDurationMs},
    {6, "Maantee tont", {{"REASTU KAUGEMAL", "TONT"}},
     AnimationKind::Static, {255, 80, 80},
     kDefaultDurationMs},
    {7, "Reguleeri tulesi", {{"Reguleeri", "esitulesi!"}},
     AnimationKind::Static, {255, 255, 255},
     kDefaultDurationMs},
    {8, "Turvaline mooduda", {{"<- Sõida mööda!"}},
     AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
    {9, "Nice car", {{"Nice car", "Äge auto"}}, AnimationKind::Static,
     {255, 220, 40},
     kDefaultDurationMs},
    {10, "Message 10", {{"MESSAGE 10"}}, AnimationKind::Pulse,
     {80, 180, 255},
     kDefaultDurationMs},
    {11, "Message 11", {{"MESSAGE 11"}}, AnimationKind::Static,
     {255, 120, 40},
     kDefaultDurationMs},
    {12, "Message 12", {{"MESSAGE 12"}}, AnimationKind::ColorCycle,
     {255, 255, 255},
     kDefaultDurationMs},
};
}  // namespace matrix

}  // namespace rr::presentation
