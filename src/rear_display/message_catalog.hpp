#pragma once

#include "preset_definition.hpp"
#include "rr/protocol.hpp"

namespace rr::rear {

const PresetDefinition* presets();
size_t presetCount();
const PresetDefinition* findPreset(uint16_t id);
bool validateCatalog();
bool validateCatalog(const PresetDefinition* definitions, size_t count);
uint32_t catalogRevision();
CatalogEntrySummary summaryAt(size_t index);

}  // namespace rr::rear
