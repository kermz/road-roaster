#pragma once

#include <array>
#include <cstdint>

#include "rr/protocol.hpp"

namespace rr {

class CatalogStore {
 public:
  bool begin(uint32_t revision, uint16_t total_entries, uint8_t total_pages);
  bool applyPage(uint32_t revision, uint8_t page_index, uint8_t entry_count,
                 const CatalogEntrySummary* entries);

  void clear();
  bool complete() const;
  bool pageReceived(uint8_t page_index) const;
  uint8_t firstMissingPage() const;

  uint32_t revision() const { return revision_; }
  uint16_t size() const { return size_; }
  uint8_t pageCount() const { return page_count_; }
  const CatalogEntrySummary* entryAt(uint16_t index) const;
  const CatalogEntrySummary* findById(uint16_t id) const;

 private:
  static constexpr uint8_t kMaxPages =
      kMaxCatalogEntries / kMessagesPerPage;

  uint32_t revision_ = 0;
  uint16_t size_ = 0;
  uint8_t page_count_ = 0;
  bool initialized_ = false;
  std::array<CatalogEntrySummary, kMaxCatalogEntries> entries_{};
  std::array<bool, kMaxPages> received_pages_{};
};

}  // namespace rr
