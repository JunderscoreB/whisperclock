/*
 * WhisperClock - App Entry Point
 * Copyright (c) 2026 J_B
 *
 * Portions of this project's v1.2 audio engine logic and settings configuration were generated and refactored with the assistance of a large language model (Gemini).
 *
 * Released under the MIT License.
 */

#include <pebble.h>
#include "settings_engine.h"
#include "speaker_engine.h"
#include "gesture_engine.h"

#define WAKE_REASON_PERSIST_KEY 4

extern WhisperSettings s_settings;
extern void show_speaking_graphic(void);
extern void trigger_playback(bool auto_exit);
extern void settings_window_push(void);

static void init(void) {
  settings_init();
  speaker_init();
  gesture_engine_init();

  // THE GLOBAL FIX: Silence the background worker completely while the app
  // is alive. This prevents speaker vibrations from causing a tap-loop!
  if (app_worker_is_running()) {
    app_worker_kill();
  }

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
    // Standard Launch from Pebble Menu
    APP_LOG(APP_LOG_LEVEL_INFO, "App Woken by: STANDARD MENU LAUNCH");
    settings_window_push();
  }
}

static void deinit(void) {
  gesture_engine_deinit();
  settings_deinit();
  speaker_cancel();

  // Revive the worker as we safely yield back to the OS
  if (s_settings.enable_beta_features) {
    if (!app_worker_is_running()) {
      app_worker_launch();
    }
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
