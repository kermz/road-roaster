#include "rr/catalog_store.hpp"

#include <algorithm>
#include <cstring>

namespace rr {

bool CatalogStore::begin(uint32_t revision, uint16_t total_entries,
                         uint8_t total_pages) {
  if (total_entries > kMaxCatalogEntries ||
      total_pages != pageCountFor(total_entries)) {
    return false;
  }
  clear();
  revision_ = revision;
  size_ = total_entries;
  page_count_ = total_pages;
  initialized_ = true;
  return true;
}

bool CatalogStore::applyPage(uint32_t revision, uint8_t page_index,
                             uint8_t entry_count,
                             const CatalogEntrySummary* entries) {
  if (revision != revision_ || page_index >= page_count_ || entries == nullptr) {
    return false;
  }

  const uint16_t start = page_index * kMessagesPerPage;
  const uint8_t expected_count = static_cast<uint8_t>(
      std::min<uint16_t>(kMessagesPerPage, size_ - start));
  if (entry_count != expected_count) return false;

  for (uint8_t i = 0; i < entry_count; ++i) {
    const size_t label_length =
        strnlen(entries[i].label.data(), kMaxLabelBytes + 1);
    if (entries[i].id == 0 || entries[i].default_duration_ms == 0 ||
        label_length == 0 || label_length > kMaxLabelBytes) {
      return false;
    }
    for (uint8_t j = 0; j < i; ++j) {
      if (entries[j].id == entries[i].id) return false;
    }
    for (uint16_t existing = 0; existing < size_; ++existing) {
      const bool in_current_page = existing >= start &&
                                   existing < start + expected_count;
      if (!in_current_page && entries_[existing].id == entries[i].id) {
        return false;
      }
    }
  }

  for (uint8_t i = 0; i < entry_count; ++i) {
    entries_[start + i] = entries[i];
  }
  received_pages_[page_index] = true;
  return true;
}

void CatalogStore::clear() {
  revision_ = 0;
  size_ = 0;
  page_count_ = 0;
  initialized_ = false;
  entries_.fill({});
  received_pages_.fill(false);
}

bool CatalogStore::complete() const {
  if (!initialized_) return false;
  for (uint8_t page = 0; page < page_count_; ++page) {
    if (!received_pages_[page]) return false;
  }
  return true;
}

bool CatalogStore::pageReceived(uint8_t page_index) const {
  return page_index < page_count_ && received_pages_[page_index];
}

uint8_t CatalogStore::firstMissingPage() const {
  for (uint8_t page = 0; page < page_count_; ++page) {
    if (!received_pages_[page]) return page;
  }
  return page_count_;
}

const CatalogEntrySummary* CatalogStore::entryAt(uint16_t index) const {
  return index < size_ ? &entries_[index] : nullptr;
}

const CatalogEntrySummary* CatalogStore::findById(uint16_t id) const {
  for (uint16_t index = 0; index < size_; ++index) {
    if (entries_[index].id == id) return &entries_[index];
  }
  return nullptr;
}

}  // namespace rr
