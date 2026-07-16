#include <unity.h>

#include <array>
#include <cstring>

#include "rr/catalog_store.hpp"
#include "rr/protocol.hpp"
#include "rr/text_encoding.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

rr::CatalogEntrySummary entry(uint16_t id, const char* label,
                              uint32_t duration = 10000) {
  rr::CatalogEntrySummary value;
  value.id = id;
  value.default_duration_ms = duration;
  std::strncpy(value.label.data(), label, rr::kMaxLabelBytes);
  value.label[rr::kMaxLabelBytes] = '\0';
  return value;
}

std::array<rr::CatalogEntrySummary, 4> pageEntries(uint16_t first_id) {
  return {entry(first_id, "Message"), entry(first_id + 1, "Message"),
          entry(first_id + 2, "Message"), entry(first_id + 3, "Message")};
}

void test_catalog_page_round_trip() {
  rr::Packet packet;
  packet.type = rr::PacketType::CatalogPage;
  packet.sequence = 42;
  packet.catalog_revision = 0xAABBCCDD;
  packet.page_index = 2;
  packet.entry_count = 4;
  packet.entries = {entry(9, "Message 09"), entry(10, "Message 10"),
                    entry(11, "Message 11"), entry(12, "Message 12")};

  std::array<uint8_t, rr::kMaxEspNowPayload> bytes{};
  const size_t length = rr::encodePacket(packet, bytes.data(), bytes.size());
  TEST_ASSERT_GREATER_THAN(0, length);
  TEST_ASSERT_LESS_OR_EQUAL(rr::kMaxEspNowPayload, length);

  const rr::DecodeResult decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(rr::PacketType::CatalogPage),
                          static_cast<uint8_t>(decoded.packet.type));
  TEST_ASSERT_EQUAL_UINT32(packet.catalog_revision,
                           decoded.packet.catalog_revision);
  TEST_ASSERT_EQUAL_UINT8(4, decoded.packet.entry_count);
  TEST_ASSERT_EQUAL_STRING("Message 12", decoded.packet.entries[3].label.data());
}

void test_status_round_trip() {
  rr::Packet packet;
  packet.type = rr::PacketType::Status;
  packet.sequence = 99;
  packet.state = {77, true, 7, 30000, 14000, 35, true};

  std::array<uint8_t, rr::kMaxEspNowPayload> bytes{};
  const size_t length = rr::encodePacket(packet, bytes.data(), bytes.size());
  const rr::DecodeResult decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_TRUE(decoded.packet.state.active);
  TEST_ASSERT_EQUAL_UINT16(7, decoded.packet.state.preset_id);
  TEST_ASSERT_EQUAL_UINT32(14000, decoded.packet.state.remaining_ms);
  TEST_ASSERT_EQUAL_UINT8(35, decoded.packet.state.brightness_percent);
  TEST_ASSERT_TRUE(decoded.packet.state.flipped);
}

void test_malformed_packets_are_rejected() {
  const uint8_t too_short[] = {0x52, 0x52, 0x01};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(rr::DecodeError::TooShort),
      static_cast<uint8_t>(rr::decodePacket(too_short, sizeof(too_short)).error));

  rr::Packet packet;
  packet.type = rr::PacketType::Clear;
  std::array<uint8_t, rr::kMaxEspNowPayload> bytes{};
  const size_t length = rr::encodePacket(packet, bytes.data(), bytes.size());
  bytes[2] = 99;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(rr::DecodeError::UnsupportedVersion),
      static_cast<uint8_t>(rr::decodePacket(bytes.data(), length).error));
}

void test_duration_rules_and_wrap_safe_remaining_time() {
  TEST_ASSERT_TRUE(rr::isValidDurationOverride(0));
  TEST_ASSERT_TRUE(rr::isValidDurationOverride(5000));
  TEST_ASSERT_TRUE(rr::isValidDurationOverride(60000));
  TEST_ASSERT_FALSE(rr::isValidDurationOverride(7000));
  TEST_ASSERT_FALSE(rr::isValidDurationOverride(65000));
  TEST_ASSERT_EQUAL_UINT32(6000, rr::remainingDuration(5000, 1000, 10000));
  TEST_ASSERT_EQUAL_UINT32(0, rr::remainingDuration(11000, 1000, 10000));
  TEST_ASSERT_EQUAL_UINT32(16, rr::remainingDuration(4, 0xFFFFFFF0, 36));
}

void test_catalog_store_handles_three_and_partial_pages() {
  rr::CatalogStore store;
  TEST_ASSERT_TRUE(store.begin(123, 12, 3));
  for (uint8_t page = 0; page < 3; ++page) {
    std::array<rr::CatalogEntrySummary, 4> values{};
    for (uint8_t i = 0; i < 4; ++i) {
      values[i] = entry(static_cast<uint16_t>(page * 4 + i + 1), "Message");
    }
    TEST_ASSERT_TRUE(store.applyPage(123, page, 4, values.data()));
  }
  TEST_ASSERT_TRUE(store.complete());
  TEST_ASSERT_EQUAL_UINT16(12, store.size());

  TEST_ASSERT_TRUE(store.begin(456, 13, 4));
  std::array<rr::CatalogEntrySummary, 1> last = {entry(13, "Message 13")};
  TEST_ASSERT_TRUE(store.applyPage(456, 3, 1, last.data()));
  TEST_ASSERT_FALSE(store.complete());
}

void test_catalog_store_rejects_duplicates_and_boundaries() {
  rr::CatalogStore store;
  TEST_ASSERT_FALSE(store.begin(1, 65, 17));
  TEST_ASSERT_TRUE(store.begin(1, 4, 1));
  const std::array<rr::CatalogEntrySummary, 4> duplicate = {
      entry(1, "One"), entry(2, "Two"), entry(2, "Duplicate"), entry(4, "Four")};
  TEST_ASSERT_FALSE(store.applyPage(1, 0, 4, duplicate.data()));
}

void test_every_command_and_manifest_packet_round_trips() {
  std::array<uint8_t, rr::kMaxEspNowPayload> bytes{};

  rr::Packet manifest;
  manifest.type = rr::PacketType::CatalogManifest;
  manifest.sequence = 100;
  manifest.catalog_revision = 0x12345678;
  manifest.total_entries = 64;
  manifest.total_pages = 16;
  size_t length = rr::encodePacket(manifest, bytes.data(), bytes.size());
  rr::DecodeResult decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_EQUAL_UINT16(64, decoded.packet.total_entries);
  TEST_ASSERT_EQUAL_UINT8(16, decoded.packet.total_pages);

  rr::Packet display;
  display.type = rr::PacketType::Display;
  display.sequence = 101;
  display.catalog_revision = 0x12345678;
  display.preset_id = 37;
  display.duration_override_ms = 60000;
  length = rr::encodePacket(display, bytes.data(), bytes.size());
  decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_EQUAL_UINT32(display.catalog_revision,
                           decoded.packet.catalog_revision);
  TEST_ASSERT_EQUAL_UINT16(37, decoded.packet.preset_id);
  TEST_ASSERT_EQUAL_UINT32(60000, decoded.packet.duration_override_ms);

  rr::Packet ack;
  ack.type = rr::PacketType::Ack;
  ack.sequence = 101;
  ack.ack_result = rr::AckResult::Applied;
  ack.acknowledged_type = rr::PacketType::Display;
  ack.state = {0x12345678, true, 37, 60000, 59000, 35, true};
  length = rr::encodePacket(ack, bytes.data(), bytes.size());
  decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(rr::PacketType::Display),
                          static_cast<uint8_t>(decoded.packet.acknowledged_type));
  TEST_ASSERT_EQUAL_UINT32(60000, decoded.packet.state.total_duration_ms);

  for (rr::PacketType type : {rr::PacketType::Clear,
                              rr::PacketType::GetStatus}) {
    rr::Packet no_payload;
    no_payload.type = type;
    no_payload.sequence = 102;
    length = rr::encodePacket(no_payload, bytes.data(), bytes.size());
    decoded = rr::decodePacket(bytes.data(), length);
    TEST_ASSERT_TRUE(decoded);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type),
                            static_cast<uint8_t>(decoded.packet.type));
  }

  rr::Packet brightness;
  brightness.type = rr::PacketType::SetBrightness;
  brightness.sequence = 103;
  brightness.brightness_percent = 65;
  length = rr::encodePacket(brightness, bytes.data(), bytes.size());
  decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_EQUAL_UINT8(65, decoded.packet.brightness_percent);

  rr::Packet flip;
  flip.type = rr::PacketType::SetFlip;
  flip.sequence = 104;
  flip.flipped = true;
  length = rr::encodePacket(flip, bytes.data(), bytes.size());
  decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_TRUE(decoded.packet.flipped);

  bytes[10] = 2;
  decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(rr::DecodeError::InvalidPayload),
                          static_cast<uint8_t>(decoded.error));
}

void test_maximum_catalog_and_packet_size_are_supported() {
  rr::CatalogStore store;
  TEST_ASSERT_FALSE(store.complete());
  TEST_ASSERT_TRUE(store.begin(800, 64, 16));
  for (uint8_t page = 0; page < 16; ++page) {
    const auto values = pageEntries(static_cast<uint16_t>(page * 4 + 1));
    TEST_ASSERT_TRUE(store.applyPage(800, page, 4, values.data()));
  }
  TEST_ASSERT_TRUE(store.complete());
  TEST_ASSERT_EQUAL_UINT16(64, store.size());
  TEST_ASSERT_NOT_NULL(store.findById(64));

  rr::Packet full_page;
  full_page.type = rr::PacketType::CatalogPage;
  full_page.sequence = 9;
  full_page.catalog_revision = 800;
  full_page.page_index = 15;
  full_page.entry_count = 4;
  const char* long_label = "1234567890123456789012345678901";
  full_page.entries = {entry(61, long_label), entry(62, long_label),
                       entry(63, long_label), entry(64, long_label)};
  std::array<uint8_t, rr::kMaxEspNowPayload> bytes{};
  const size_t length =
      rr::encodePacket(full_page, bytes.data(), bytes.size());
  TEST_ASSERT_GREATER_THAN(0, length);
  TEST_ASSERT_LESS_OR_EQUAL(rr::kMaxEspNowPayload, length);
  TEST_ASSERT_TRUE(rr::decodePacket(bytes.data(), length));
}

void test_out_of_order_dropped_and_retried_pages_are_idempotent() {
  rr::CatalogStore store;
  TEST_ASSERT_TRUE(store.begin(900, 12, 3));
  const auto first = pageEntries(1);
  const auto second = pageEntries(5);
  const auto third = pageEntries(9);
  TEST_ASSERT_TRUE(store.applyPage(900, 2, 4, third.data()));
  TEST_ASSERT_EQUAL_UINT8(0, store.firstMissingPage());
  TEST_ASSERT_TRUE(store.applyPage(900, 0, 4, first.data()));
  TEST_ASSERT_TRUE(store.applyPage(900, 0, 4, first.data()));
  TEST_ASSERT_EQUAL_UINT8(1, store.firstMissingPage());
  TEST_ASSERT_FALSE(store.complete());
  TEST_ASSERT_TRUE(store.applyPage(900, 1, 4, second.data()));
  TEST_ASSERT_TRUE(store.complete());
}

void test_revision_changes_and_malformed_pages_are_rejected() {
  rr::CatalogStore store;
  TEST_ASSERT_TRUE(store.begin(10, 8, 2));
  auto first = pageEntries(1);
  TEST_ASSERT_FALSE(store.applyPage(11, 0, 4, first.data()));
  TEST_ASSERT_FALSE(store.applyPage(10, 0, 3, first.data()));

  auto malformed = first;
  malformed[1].default_duration_ms = 0;
  TEST_ASSERT_FALSE(store.applyPage(10, 0, 4, malformed.data()));
  malformed = first;
  malformed[1].label[0] = '\0';
  TEST_ASSERT_FALSE(store.applyPage(10, 0, 4, malformed.data()));
  TEST_ASSERT_TRUE(store.applyPage(10, 0, 4, first.data()));

  auto second = pageEntries(5);
  second[3] = entry(2, "Cross-page duplicate");
  TEST_ASSERT_FALSE(store.applyPage(10, 1, 4, second.data()));

  TEST_ASSERT_TRUE(store.begin(12, 4, 1));
  TEST_ASSERT_FALSE(store.complete());
  TEST_ASSERT_EQUAL_UINT32(12, store.revision());
  TEST_ASSERT_NULL(store.findById(1));
}

void test_retransmission_keeps_the_same_sequence_and_bytes() {
  rr::Packet command;
  command.type = rr::PacketType::Display;
  command.sequence = 0xCAFEBABE;
  command.catalog_revision = 900;
  command.preset_id = 7;
  command.duration_override_ms = 5000;
  std::array<uint8_t, rr::kMaxEspNowPayload> first{};
  std::array<uint8_t, rr::kMaxEspNowPayload> retry{};
  const size_t first_length =
      rr::encodePacket(command, first.data(), first.size());
  const size_t retry_length =
      rr::encodePacket(command, retry.data(), retry.size());
  TEST_ASSERT_EQUAL_UINT(first_length, retry_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.data(), retry.data(), first_length);
}

void test_page_and_duration_choice_wrapping() {
  TEST_ASSERT_EQUAL_UINT8(0, rr::wrappedIndex(2, 1, 3));
  TEST_ASSERT_EQUAL_UINT8(2, rr::wrappedIndex(0, -1, 3));
  TEST_ASSERT_EQUAL_UINT8(1, rr::wrappedIndex(0, 4, 3));
  TEST_ASSERT_EQUAL_UINT8(12, rr::wrappedIndex(0, -1, 13));
  TEST_ASSERT_EQUAL_UINT8(0, rr::wrappedIndex(4, 1, 0));
}

void test_effective_duration_and_duplicate_command_window() {
  TEST_ASSERT_EQUAL_UINT32(10000, rr::effectiveDuration(10000, 0));
  TEST_ASSERT_EQUAL_UINT32(5000, rr::effectiveDuration(10000, 5000));
  TEST_ASSERT_EQUAL_UINT32(60000, rr::effectiveDuration(10000, 60000));

  rr::ProcessedCommandWindow window;
  rr::AckResult result = rr::AckResult::MalformedRequest;
  TEST_ASSERT_FALSE(window.find(100, rr::PacketType::Display, result));
  window.remember(100, rr::PacketType::Display, rr::AckResult::Applied);
  TEST_ASSERT_TRUE(window.find(100, rr::PacketType::Display, result));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(rr::AckResult::Applied),
                          static_cast<uint8_t>(result));
  TEST_ASSERT_FALSE(window.find(101, rr::PacketType::Display, result));
  TEST_ASSERT_FALSE(window.find(100, rr::PacketType::Clear, result));

  // A new sequence selecting the same preset is not a duplicate invocation.
  window.remember(101, rr::PacketType::Display,
                  rr::AckResult::InvalidDuration);
  TEST_ASSERT_TRUE(window.find(101, rr::PacketType::Display, result));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(rr::AckResult::InvalidDuration),
                          static_cast<uint8_t>(result));
}

void test_request_timeout_and_catalog_sync_policy() {
  TEST_ASSERT_TRUE(rr::canStartCatalogSync(false));
  TEST_ASSERT_FALSE(rr::canStartCatalogSync(true));
  TEST_ASSERT_TRUE(rr::matchesCatalogRevision(77, 77));
  TEST_ASSERT_FALSE(rr::matchesCatalogRevision(76, 77));

  using ManifestAction = rr::CatalogManifestAction;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ManifestAction::AcceptExpected),
      static_cast<uint8_t>(rr::catalogManifestAction(true, true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ManifestAction::BeginSync),
      static_cast<uint8_t>(rr::catalogManifestAction(false, false, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ManifestAction::Ignore),
      static_cast<uint8_t>(rr::catalogManifestAction(false, false, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ManifestAction::Ignore),
      static_cast<uint8_t>(rr::catalogManifestAction(false, true, false)));

  using Action = rr::RequestTimeoutAction;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Action::Wait),
      static_cast<uint8_t>(
          rr::requestTimeoutAction(false, 1000, 0, 300, 0, 4)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Action::Wait),
      static_cast<uint8_t>(
          rr::requestTimeoutAction(true, 1299, 1000, 300, 1, 4)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Action::Retry),
      static_cast<uint8_t>(
          rr::requestTimeoutAction(true, 1300, 1000, 300, 1, 4)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Action::Fail),
      static_cast<uint8_t>(
          rr::requestTimeoutAction(true, 2200, 1900, 300, 4, 4)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Action::Retry),
      static_cast<uint8_t>(rr::requestTimeoutAction(
          true, 0x00000020, 0xFFFFFF00, 0x120, 2, 4)));
}

void test_request_coordinator_scenarios() {
  rr::RequestCoordinator coordinator;
  rr::Packet display;
  display.type = rr::PacketType::Display;
  display.sequence = 500;
  display.catalog_revision = 77;
  display.preset_id = 4;
  coordinator.begin(display, 1000);

  rr::Packet boot_manifest;
  boot_manifest.type = rr::PacketType::CatalogManifest;
  boot_manifest.sequence = 900;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(rr::CatalogManifestAction::Ignore),
      static_cast<uint8_t>(
          coordinator.manifestAction(boot_manifest, false, true)));
  TEST_ASSERT_TRUE(coordinator.active());
  TEST_ASSERT_TRUE(coordinator.matches(500, rr::PacketType::Display));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(rr::RequestTimeoutAction::Retry),
      static_cast<uint8_t>(coordinator.timeoutAction(1300, 300, 4)));
  coordinator.markRetried(1300);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(rr::RequestTimeoutAction::Wait),
      static_cast<uint8_t>(coordinator.timeoutAction(1599, 300, 4)));
  coordinator.clear();
  TEST_ASSERT_FALSE(coordinator.active());

  rr::Packet page_request;
  page_request.type = rr::PacketType::CatalogPageRequest;
  page_request.sequence = 501;
  page_request.page_index = 3;
  coordinator.begin(page_request, 2000);
  rr::Packet page_response;
  page_response.type = rr::PacketType::CatalogPage;
  page_response.sequence = 501;
  page_response.page_index = 3;
  TEST_ASSERT_TRUE(coordinator.matchesCatalogPage(page_response));
  page_response.page_index = 2;
  TEST_ASSERT_FALSE(coordinator.matchesCatalogPage(page_response));
}

void test_brightness_validation() {
  TEST_ASSERT_TRUE(rr::isValidBrightness(5));
  TEST_ASSERT_TRUE(rr::isValidBrightness(35));
  TEST_ASSERT_TRUE(rr::isValidBrightness(100));
  TEST_ASSERT_FALSE(rr::isValidBrightness(0));
  TEST_ASSERT_FALSE(rr::isValidBrightness(37));
  TEST_ASSERT_FALSE(rr::isValidBrightness(101));

  rr::Packet invalid;
  invalid.type = rr::PacketType::SetBrightness;
  invalid.brightness_percent = 37;
  std::array<uint8_t, rr::kMaxEspNowPayload> bytes{};
  const size_t length = rr::encodePacket(invalid, bytes.data(), bytes.size());
  TEST_ASSERT_GREATER_THAN(0, length);
  const rr::DecodeResult decoded = rr::decodePacket(bytes.data(), length);
  TEST_ASSERT_TRUE(decoded);
  TEST_ASSERT_EQUAL_UINT8(37, decoded.packet.brightness_percent);
}

void test_utf8_validation_and_latin1_encoding() {
  TEST_ASSERT_TRUE(rr::isValidUtf8("ASCII"));
  TEST_ASSERT_TRUE(rr::isValidUtf8(u8"õäöü ÕÄÖÜ"));
  TEST_ASSERT_TRUE(rr::isAscii("ASCII"));
  TEST_ASSERT_FALSE(rr::isAscii(u8"ö"));

  const char incomplete[] = {static_cast<char>(0xE2), '\0'};
  const char overlong[] = {static_cast<char>(0xC0),
                           static_cast<char>(0xAF), '\0'};
  TEST_ASSERT_FALSE(rr::isValidUtf8(incomplete));
  TEST_ASSERT_FALSE(rr::isValidUtf8(overlong));

  char encoded[16]{};
  TEST_ASSERT_EQUAL_UINT(4,
                         rr::encodeLatin1(u8"õäöü", encoded, sizeof(encoded)));
  const uint8_t expected[] = {0xF5, 0xE4, 0xF6, 0xFC};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,
                                reinterpret_cast<const uint8_t*>(encoded), 4);

  TEST_ASSERT_EQUAL_UINT(1,
                         rr::encodeLatin1(u8"€", encoded, sizeof(encoded)));
  TEST_ASSERT_EQUAL_CHAR('?', encoded[0]);
  TEST_ASSERT_EQUAL_UINT(1,
                         rr::encodeLatin1(incomplete, encoded, sizeof(encoded)));
  TEST_ASSERT_EQUAL_STRING("?", encoded);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_page_round_trip);
  RUN_TEST(test_status_round_trip);
  RUN_TEST(test_malformed_packets_are_rejected);
  RUN_TEST(test_duration_rules_and_wrap_safe_remaining_time);
  RUN_TEST(test_catalog_store_handles_three_and_partial_pages);
  RUN_TEST(test_catalog_store_rejects_duplicates_and_boundaries);
  RUN_TEST(test_every_command_and_manifest_packet_round_trips);
  RUN_TEST(test_maximum_catalog_and_packet_size_are_supported);
  RUN_TEST(test_out_of_order_dropped_and_retried_pages_are_idempotent);
  RUN_TEST(test_revision_changes_and_malformed_pages_are_rejected);
  RUN_TEST(test_retransmission_keeps_the_same_sequence_and_bytes);
  RUN_TEST(test_page_and_duration_choice_wrapping);
  RUN_TEST(test_effective_duration_and_duplicate_command_window);
  RUN_TEST(test_request_timeout_and_catalog_sync_policy);
  RUN_TEST(test_request_coordinator_scenarios);
  RUN_TEST(test_brightness_validation);
  RUN_TEST(test_utf8_validation_and_latin1_encoding);
  return UNITY_END();
}
