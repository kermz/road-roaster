#include <Arduino.h>

#include "rear_app.hpp"

namespace {
rr::rear::RearApp app;
bool initialized = false;
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("\nRoad Roaster rear display starting");
  initialized = app.begin();
}

void loop() {
  if (initialized) app.loop();
  delay(1);
}

