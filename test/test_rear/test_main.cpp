#include <unity.h>
#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include <deque>
#include <memory>
#include <array>
#include "rr/radio_transport.hpp"
#include "message_catalog.hpp"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "spleen_bitmaps.hpp"
#define private public
#include "rear_app.hpp"
#undef private

namespace {
std::vector<rr::Packet> sent;
std::deque<rr::Packet> incoming;
bool radio_start_succeeds=true;
}
namespace rr {
bool RadioTransport::begin(const RadioConfig&) { ready_=radio_start_succeeds; return ready_; }
bool RadioTransport::send(const Packet& packet) { sent.push_back(packet); return true; }
bool RadioTransport::receive(Packet& packet) { if(incoming.empty()) return false; packet=incoming.front(); incoming.pop_front(); return true; }
}
extern "C" void setUp() {
  test_now_ms=1000; sent.clear(); incoming.clear(); preference_values.clear();
  preference_writes=0; preference_write_succeeds=true; preference_begin_succeeds=true;
  matrix_begin_succeeds=true; radio_start_succeeds=true;
}
extern "C" void tearDown() {}
namespace {
using namespace rr;
using namespace rr::rear;
Packet command(PacketType type,uint32_t sequence=1) {
  Packet p; p.type=type;p.sequence=sequence;p.catalog_revision=catalogRevision();p.preset_id=1;return p;
}
void test_boot_blank_and_settings_restore() {
  preference_values["matrix-pct"]=65;preference_values["matrix-flip"]=1;
  RearApp app;TEST_ASSERT_TRUE(app.begin());
  TEST_ASSERT_FALSE(app.currentState(test_now_ms).active);
  TEST_ASSERT_EQUAL_UINT8(65,app.currentState(test_now_ms).brightness_percent);
  TEST_ASSERT_TRUE(app.currentState(test_now_ms).flipped);
  TEST_ASSERT_EQUAL_UINT(2,sent.size());
  auto p=command(PacketType::Display);app.handlePacket(p,test_now_ms);
  RearApp reboot;TEST_ASSERT_TRUE(reboot.begin());TEST_ASSERT_FALSE(reboot.currentState(test_now_ms).active);
}
void test_catalog_requests_and_invalid_revision() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());sent.clear();
  app.handlePacket(command(PacketType::CatalogRequest,20),test_now_ms);
  TEST_ASSERT_EQUAL_UINT32(20,sent.back().sequence);
  TEST_ASSERT_EQUAL_UINT(presetCount(),sent.back().total_entries);
  for(uint8_t page=0;page<pageCountFor(presetCount());++page) {
    auto p=command(PacketType::CatalogPageRequest,30+page);p.page_index=page;
    app.handlePacket(p,test_now_ms);TEST_ASSERT_EQUAL_UINT8(page,sent.back().page_index);
    TEST_ASSERT_EQUAL_UINT16(summaryAt(page*kMessagesPerPage).id,sent.back().entries[0].id);
  }
  auto p=command(PacketType::CatalogPageRequest);p.catalog_revision++;
  app.handlePacket(p,test_now_ms);TEST_ASSERT_EQUAL_INT(static_cast<int>(AckResult::InvalidCatalogRevision),static_cast<int>(sent.back().ack_result));
}
void test_display_default_override_duplicate_and_expiry() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());auto p=command(PacketType::Display);
  app.handlePacket(p,test_now_ms);TEST_ASSERT_EQUAL_UINT32(findPreset(1)->default_duration_ms,app.currentState(test_now_ms).total_duration_ms);
  p.sequence=2;p.duration_override_ms=5000;app.handlePacket(p,test_now_ms);
  test_now_ms+=1000;app.handlePacket(p,test_now_ms);
  TEST_ASSERT_EQUAL_UINT32(4000,app.currentState(test_now_ms).remaining_ms);
  test_now_ms+=4000;app.loop();TEST_ASSERT_FALSE(app.currentState(test_now_ms).active);
  TEST_ASSERT_FALSE(sent.back().state.active);
}
void test_invalid_display_commands_do_not_replace_active_message() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());app.handlePacket(command(PacketType::Display),test_now_ms);
  auto p=command(PacketType::Display,2);p.catalog_revision++;app.handlePacket(p,test_now_ms);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AckResult::InvalidCatalogRevision),static_cast<int>(sent.back().ack_result));
  p=command(PacketType::Display,3);p.preset_id=65535;app.handlePacket(p,test_now_ms);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AckResult::InvalidPreset),static_cast<int>(sent.back().ack_result));
  p=command(PacketType::Display,4);p.duration_override_ms=123;app.handlePacket(p,test_now_ms);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AckResult::InvalidDuration),static_cast<int>(sent.back().ack_result));
  TEST_ASSERT_EQUAL_UINT16(1,app.currentState(test_now_ms).preset_id);
}
void test_clear_get_status_and_one_second_heartbeat() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());app.handlePacket(command(PacketType::Display),test_now_ms);
  app.handlePacket(command(PacketType::Clear,2),test_now_ms);TEST_ASSERT_FALSE(sent.back().state.active);
  app.handlePacket(command(PacketType::GetStatus,3),test_now_ms);TEST_ASSERT_EQUAL_UINT32(3,sent.back().sequence);
  sent.clear();test_now_ms+=999;app.loop();TEST_ASSERT_TRUE(sent.empty());
  ++test_now_ms;app.loop();TEST_ASSERT_EQUAL_UINT(1,sent.size());
}
void test_rear_settings_persist_retry_and_invalid_brightness() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());auto p=command(PacketType::SetBrightness);p.brightness_percent=70;
  app.handlePacket(p,test_now_ms);p=command(PacketType::SetFlip,2);p.flipped=true;app.handlePacket(p,test_now_ms);
  app.persistBrightnessIfDue(test_now_ms+749);TEST_ASSERT_EQUAL_UINT(0,preference_writes);
  preference_write_succeeds=false;app.persistBrightnessIfDue(test_now_ms+750);app.persistFlipIfDue(test_now_ms+750);
  TEST_ASSERT_TRUE(app.brightness_save_pending_);TEST_ASSERT_TRUE(app.flip_save_pending_);
  preference_write_succeeds=true;app.persistBrightnessIfDue(test_now_ms+5750);app.persistFlipIfDue(test_now_ms+5750);
  TEST_ASSERT_EQUAL_UINT8(70,preference_values["matrix-pct"]);TEST_ASSERT_EQUAL_UINT8(1,preference_values["matrix-flip"]);
  p=command(PacketType::SetBrightness,3);p.brightness_percent=7;app.handlePacket(p,test_now_ms);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AckResult::InvalidBrightness),static_cast<int>(sent.back().ack_result));
  TEST_ASSERT_EQUAL_UINT8(70,app.currentState(test_now_ms).brightness_percent);
}
void test_duplicate_setting_does_not_write_again() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());auto p=command(PacketType::SetBrightness);p.brightness_percent=35;
  app.handlePacket(p,test_now_ms);p=command(PacketType::SetFlip,2);p.flipped=false;app.handlePacket(p,test_now_ms);
  app.persistBrightnessIfDue(test_now_ms+750);app.persistFlipIfDue(test_now_ms+750);
  TEST_ASSERT_EQUAL_UINT(0,preference_writes);
}
void test_initialization_failures_stop_rear() {
  matrix_begin_succeeds=false;RearApp matrix_failed;TEST_ASSERT_FALSE(matrix_failed.begin());
  matrix_begin_succeeds=true;radio_start_succeeds=false;RearApp radio_failed;TEST_ASSERT_FALSE(radio_failed.begin());
}
}

// Renderer members are inspected only in this host suite.

namespace {
PresetDefinition preset(AnimationKind animation=AnimationKind::Static) {
  return {1,"Test",{{"HI"}},animation,{255,80,40},10000};
}
void test_renderer_static_fits_largest_font_and_idles() {
  MatrixRenderer r;TEST_ASSERT_TRUE(r.begin(35,false));auto p=preset();r.show(&p,1000);r.tick(1000);
  auto* driver=r.display_.get();TEST_ASSERT_EQUAL_STRING("HI",driver->text.c_str());
  TEST_ASSERT_EQUAL_PTR(matrix_font::kStrikes[0],driver->font);
  unsigned frames=driver->frames,measurements=driver->measurements;
  for(uint32_t now=1067;now<2000;now+=67) r.tick(now);
  TEST_ASSERT_EQUAL_UINT(frames,driver->frames);
  TEST_ASSERT_EQUAL_UINT(measurements,driver->measurements);
}
void test_renderer_flip_redraws_static_and_brightness_maps() {
  MatrixRenderer r;r.begin(35,false);auto p=preset();r.show(&p,1000);r.tick(1000);
  auto* driver=r.display_.get();unsigned before=driver->frames;
  r.setFlipped(true);r.tick(1067);TEST_ASSERT_EQUAL_UINT8(2,driver->rotation);
  TEST_ASSERT_EQUAL_UINT(before+1,driver->frames);r.setBrightness(70);
  TEST_ASSERT_EQUAL_UINT8(70*255/100,driver->brightness);
}
void test_renderer_multiple_screens_gap_loop_and_hold() {
  MatrixRenderer r;r.begin(35,false);auto p=preset();p.matrix_screens[1]="BYE";r.show(&p,1000);r.tick(1000);
  auto* d=r.display_.get();TEST_ASSERT_EQUAL_STRING("HI",d->text.c_str());
  r.tick(1750);TEST_ASSERT_TRUE(d->text.empty());
  r.tick(1850);TEST_ASSERT_EQUAL_STRING("BYE",d->text.c_str());
  unsigned before=d->frames;r.tick(1917);TEST_ASSERT_EQUAL_UINT(before,d->frames);
  r.tick(2700);TEST_ASSERT_EQUAL_STRING("HI",d->text.c_str());
}
void test_renderer_scrolls_long_text_and_reuses_layout() {
  MatrixRenderer r;r.begin(35,false);auto p=preset();p.matrix_screens[0]="THIS SCREEN IS MUCH TOO LONG TO FIT EVEN THE SMALLEST FONT";
  r.show(&p,1000);r.tick(1000);auto* d=r.display_.get();int before=d->x;unsigned measurements=d->measurements;
  r.tick(1067);TEST_ASSERT_TRUE(d->x<before);TEST_ASSERT_EQUAL_UINT(measurements,d->measurements);
}
void test_renderer_pulse_cycle_and_clear() {
  MatrixRenderer r;r.begin(35,false);auto p=preset(AnimationKind::Pulse);r.show(&p,1000);r.tick(1000);
  auto* d=r.display_.get();uint16_t low=d->color;r.tick(1800);TEST_ASSERT_NOT_EQUAL(low,d->color);
  p.animation=AnimationKind::ColorCycle;r.show(&p,2000);r.tick(2000);uint16_t start=d->color;
  r.tick(3020);TEST_ASSERT_NOT_EQUAL(start,d->color);
  r.clear();unsigned before=d->frames;r.tick(5000);TEST_ASSERT_EQUAL_UINT(before,d->frames);TEST_ASSERT_TRUE(d->text.empty());
}
void test_renderer_latin1_and_fallback() {
  MatrixRenderer r;r.begin(35,false);auto p=preset();p.matrix_screens[0]=u8"\u00F5\u20AC";r.show(&p,1000);r.tick(1000);
  const auto& text=r.display_->text;TEST_ASSERT_EQUAL_UINT8(0xF5,static_cast<uint8_t>(text[0]));TEST_ASSERT_EQUAL_CHAR('?',text[1]);
}
}
void test_expiration_sends_one_status_at_heartbeat_boundary() {
  RearApp app;TEST_ASSERT_TRUE(app.begin());auto p=command(PacketType::Display);
  p.duration_override_ms=5000;app.handlePacket(p,test_now_ms);sent.clear();
  test_now_ms+=5000;app.loop();
  TEST_ASSERT_EQUAL_UINT(1,sent.size());
  TEST_ASSERT_FALSE(sent.back().state.active);
}


void test_renderer_fits_smaller_font_and_four_screen_order() {
  MatrixRenderer r;r.begin(35,false);auto p=preset();p.matrix_screens={{"ABCDEFGHIJKL","B","C","D"}};
  r.show(&p,1000);r.tick(1000);
  TEST_ASSERT_EQUAL_PTR(matrix_font::kStrikes[2],r.display_->font);
  for(unsigned i=1;i<4;++i) { r.tick(1000+i*850);TEST_ASSERT_EQUAL_CHAR('A'+i,r.display_->text[0]); }
  r.tick(1000+4*850);TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKL",r.display_->text.c_str());
}
void test_expiry_and_retry_across_millis_wrap() {
  test_now_ms=UINT32_MAX-2000;RearApp app;TEST_ASSERT_TRUE(app.begin());
  auto p=command(PacketType::Display);p.duration_override_ms=5000;app.handlePacket(p,test_now_ms);
  test_now_ms+=3000;app.handlePacket(p,test_now_ms);TEST_ASSERT_EQUAL_UINT32(2000,sent.back().state.remaining_ms);
  test_now_ms+=2000;app.loop();TEST_ASSERT_FALSE(app.currentState(test_now_ms).active);
}


void test_invalid_preset_configuration_is_rejected() {
  PresetDefinition entries[2]={preset(),preset()};entries[1].id=2;
  TEST_ASSERT_TRUE(validateCatalog(entries,2));
  entries[1].id=1;TEST_ASSERT_FALSE(validateCatalog(entries,2));entries[1].id=2;
  entries[0].id=0;TEST_ASSERT_FALSE(validateCatalog(entries,2));entries[0].id=1;
  entries[0].label="";TEST_ASSERT_FALSE(validateCatalog(entries,2));
  entries[0].label="This label is longer than thirty one bytes";TEST_ASSERT_FALSE(validateCatalog(entries,2));
  entries[0].label=u8"\u00F5";TEST_ASSERT_FALSE(validateCatalog(entries,2));entries[0].label="Test";
  entries[0].matrix_screens={{""}};TEST_ASSERT_FALSE(validateCatalog(entries,2));
  entries[0].matrix_screens={{"A",nullptr,"C"}};TEST_ASSERT_FALSE(validateCatalog(entries,2));
  entries[0].matrix_screens={{"\xC0\xAF"}};TEST_ASSERT_FALSE(validateCatalog(entries,2));
  std::string too_long(513,'A');entries[0].matrix_screens={{too_long.c_str()}};TEST_ASSERT_FALSE(validateCatalog(entries,2));
  entries[0].matrix_screens={{"A"}};entries[0].animation=static_cast<AnimationKind>(255);TEST_ASSERT_FALSE(validateCatalog(entries,2));
  entries[0].animation=AnimationKind::Static;entries[0].default_duration_ms=0;TEST_ASSERT_FALSE(validateCatalog(entries,2));
  TEST_ASSERT_FALSE(validateCatalog(entries,65));TEST_ASSERT_FALSE(validateCatalog(nullptr,0));
}

int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_blank_and_settings_restore);
  RUN_TEST(test_catalog_requests_and_invalid_revision);
  RUN_TEST(test_display_default_override_duplicate_and_expiry);
  RUN_TEST(test_invalid_display_commands_do_not_replace_active_message);
  RUN_TEST(test_clear_get_status_and_one_second_heartbeat);
  RUN_TEST(test_rear_settings_persist_retry_and_invalid_brightness);
  RUN_TEST(test_duplicate_setting_does_not_write_again);
  RUN_TEST(test_initialization_failures_stop_rear);
  RUN_TEST(test_renderer_static_fits_largest_font_and_idles);
  RUN_TEST(test_renderer_flip_redraws_static_and_brightness_maps);
  RUN_TEST(test_renderer_multiple_screens_gap_loop_and_hold);
  RUN_TEST(test_renderer_scrolls_long_text_and_reuses_layout);
  RUN_TEST(test_renderer_pulse_cycle_and_clear);
  RUN_TEST(test_renderer_latin1_and_fallback);
  RUN_TEST(test_expiration_sends_one_status_at_heartbeat_boundary);
  RUN_TEST(test_renderer_fits_smaller_font_and_four_screen_order);
  RUN_TEST(test_expiry_and_retry_across_millis_wrap);
  RUN_TEST(test_invalid_preset_configuration_is_rejected);
  return UNITY_END();
}
