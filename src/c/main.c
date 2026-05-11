#include <pebble.h>
#include "audio_engine.h"
#include "settings_engine.h"
#include "gesture_engine.h" 
#include "speaker_engine.h"

// Note: All UI and playback logic is now safely handled by 
// audio_engine.c and settings_engine.c!

static void init() {
  // 1. Boot up all our subsystems
  settings_init(); 
  speaker_init(); 
  gesture_engine_init(); // 🟢 NEW: Starts listening for wrist flicks!

  app_worker_launch(); 

  // 2. The purest, native way to launch an app. No delays.
  if (launch_reason() == APP_LAUNCH_WORKER) {
    // 🟢 NEW: Use the robust UI we built that includes the Kill Switch!
    show_speaking_graphic(); 
    trigger_playback(true);
  } else {
    settings_window_push();
  }
}

static void deinit() {
  gesture_engine_deinit(); // 🟢 NEW: Cleans up the accelerometer
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
