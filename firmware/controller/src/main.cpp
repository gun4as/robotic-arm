// =============================================================================
//  Robot rokas kontrolieris — RAMPS 1.4 (Mega2560) / BTT SKR V1.4 Turbo (LPC1769)
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "hal.h"
#include "stepper.h"
#include "motion.h"
#include "gripper.h"
#include "tmc2209.h"
#include "protocol.h"
#include "endstops.h"
#include "storage.h"
#include "runner.h"
#include "sdcard.h"
#include "ui.h"

#ifdef STATUS_LED_PIN
static FastPin statusLed;
static uint32_t lastBlink = 0;
#endif

void setup() {
  hal::init();
  protocol::init();
#ifdef STATUS_LED_PIN
  statusLed.beginOutput(STATUS_LED_PIN, false);
#endif
  stepper::init();
  motion::init();
  endstops::init();
  ui::setEnabled(LCD_DEFAULT_ON);        // noklusējums; saglabātie iestatījumi to var mainīt
  sdcard::setEnabled(SD_DEFAULT_ON);
  const bool loaded = storage::load();   // pirms gripper/tmc init - tie izmanto iestatījumus
  gripper::init();
  tmc::init();
  protocol::banner();
  protocol::broadcast(loaded ? "[CFG] iestatijumi ielasiti no atminas" : "[CFG] noklusejuma iestatijumi (nav SAVE)");
}

void loop() {
  protocol::poll();
  motion::update();
  runner::poll();
  sdcard::poll();
  ui::poll();

  const motion::State s = motion::state();
  tmc::poll(s == motion::State::Idle && motion::queued() == 0 && !runner::active());
  hal::idle();

#ifdef STATUS_LED_PIN
  // LED: lēni mirgo = gatavs, ātri = Alarm, deg = kustība
  const uint32_t now = millis();
  const uint16_t period = s == motion::State::Alarm ? 100 : 500;
  if (s == motion::State::Jog || s == motion::State::Move || s == motion::State::Home) {
    statusLed.writeAtomic(true);
  } else if (now - lastBlink >= period) {
    lastBlink = now;
    static bool on = false;
    on = !on;
    statusLed.writeAtomic(on);
  }
#endif
}
