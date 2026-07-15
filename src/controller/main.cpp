#include <Arduino.h>

#include "controller_app.hpp"
#include "knob_board/knob_board.hpp"

extern "C" void initVariant() { knob_board::earlyBlankDisplay(); }

namespace {
rr::controller::ControllerApp app;
bool initialized = false;
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("\nRoad Roaster knob controller starting");
  initialized = app.begin();
}

void loop() {
  if (initialized) app.loop();
  delay(2);
}
