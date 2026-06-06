/*
 * WhisperClock - App Entry Point
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include <pebble.h>
#include "settings_engine.h"
#include "speaker_engine.h"

#define WAKE_REASON_PERSIST_KEY 4

extern void show_speaking_graphic();
extern void trigger_playback(bool auto_exit);

static void init(void) {
  settings_init();
  speaker_init();

  AppLaunchReason reason = launch_reason();

  // Route application flow based on system launch trigger
  if (reason == APP_LAUNCH_QUICK_LAUNCH) {
    APP_LOG(APP_LOG_LEVEL_INFO, "App Woken by: QUICK LAUNCH BUTTON");
    show_speaking_graphic();
    trigger_playback(true);
  }
  else if (persist_exists(WAKE_REASON_PERSIST_KEY)) {
    uint8_t wake_reason = 0;
    persist_read_data(WAKE_REASON_PERSIST_KEY, &wake_reason, sizeof(wake_reason));
    persist_delete(WAKE_REASON_PERSIST_KEY);

    APP_LOG(APP_LOG_LEVEL_INFO, "App Woken by: WORKER (Reason: %d)", wake_reason);
    show_speaking_graphic();
    trigger_playback(true);
  }
  else {
    // Standard Launch: Enter settings menu
    APP_LOG(APP_LOG_LEVEL_INFO, "App Woken by: SYSTEM MENU");
    settings_window_push();
  }
}

static void deinit(void) {
  settings_deinit();
  speaker_cancel();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
