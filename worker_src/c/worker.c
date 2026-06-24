/*
 * WhisperClock - Background Physics Worker
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include <pebble_worker.h>

#define MAX_BUFFER_SIZE 50
#define GESTURE_PERSIST_KEY 2
#define SETTINGS_PERSIST_KEY 14
#define WAKE_REASON_PERSIST_KEY 4
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MIN3(a, b, c) MIN(MIN(a, b), c)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAX3(a, b, c) MAX(MAX(a, b), c)

// -----------------------------------------------------------------------------
// UPDATED SETTINGS STRUCT
// -----------------------------------------------------------------------------
typedef struct __attribute__((__packed__)) {
  uint8_t prefix_mode;
  bool say_ampm;
  bool is_us_dialect;

  // Independent Pacing Arrays
  int16_t mode_speed[6];

  uint8_t clock_mode;
  uint8_t volume;

  // Independent Trim Arrays
  int16_t mode_trim[6];

  bool enable_experimental_features;

  // Background Worker & Scheduling
  bool respect_quiet_time;
  uint8_t quiet_start_hour;
  uint8_t quiet_end_hour;
  uint8_t night_volume;
  bool night_worker_sleep;

  // Gesture Configuration
  uint8_t gesture_mode; // 0 = Default Flick, 1 = Tap Glass, 2 = Custom Axes
  uint8_t default_flick_sensitivity; // Range: 55 to 70
  uint8_t tap_sensitivity;           // Range: 0 to 30 (Mapped to 40-70% physics)

  // Physics & Gestures (Fully Isolated Axes)
  int16_t x_multiplier;
  int16_t y_multiplier;
  int16_t z_multiplier;
  int16_t gesture_buffer_size;

  // FUZZY SPECIFIC PACING
  int16_t fuzzy_mod_gap;
  int16_t fuzzy_conv_gap;
  int16_t fuzzy_past_gap;
  int16_t fuzzy_to_gap;
  int16_t fuzzy_tight_gap;
  int16_t fuzzy_ampm_gap;

  // PREFIX PACING
  int16_t prefix_gap;

} WhisperSettings;

WhisperSettings s_worker_settings;

// -----------------------------------------------------------------------------
// GESTURE & ACCELEROMETER BUFFERS
// -----------------------------------------------------------------------------
typedef struct {
  int16_t x; int16_t y; int16_t z;
} CustomAccelData;

typedef struct {
  AccelData data[MAX_BUFFER_SIZE];
  uint8_t index;
  bool is_full;
} AccelBuffer;

static AccelBuffer s_buffer;
static CustomAccelData s_gesture_template[MAX_BUFFER_SIZE];
static int s_current_buffer_size = 25;
static bool s_has_trained_gesture = false;

// Physics State Variables
static int s_cooldown = 0;
static uint8_t s_tap_count = 0;
static AppTimer *s_tap_timer = NULL;

static void trigger_app(uint8_t reason) {
  persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
  worker_launch_app();
}

// -----------------------------------------------------------------------------
// TAP ENGINE (COMBO BREAKER)
// -----------------------------------------------------------------------------
static void tap_timeout_callback(void *data) {
  s_tap_count = 0;
  s_tap_timer = NULL;
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_worker_settings.gesture_mode != 1) return;
  if (s_cooldown > 0) return;

  // Strict Single-Frame Geometry Logic
  if (axis == ACCEL_AXIS_Z) {
    s_tap_count++;
    APP_LOG(APP_LOG_LEVEL_INFO, ">>> HARDWARE TAP DETECTED! Sequence: %d", s_tap_count);

    if (s_tap_timer) {
      app_timer_reschedule(s_tap_timer, 750);
    } else {
      s_tap_timer = app_timer_register(750, tap_timeout_callback, NULL);
    }

    if (s_tap_count >= 2) { // Double tap required
      s_tap_count = 0;
      if (s_tap_timer) {
        app_timer_cancel(s_tap_timer);
        s_tap_timer = NULL;
      }
      APP_LOG(APP_LOG_LEVEL_INFO, ">>> TARGET REACHED! Waking app...");
      s_cooldown = 25 * 3; // 3 seconds cooldown
      trigger_app(3); // Wake reason 3: Tap
    }
  } else {
    // The Combo Breaker: If there's lateral violent motion, wipe tap sequence!
    if (s_tap_count > 0) {
      s_tap_count = 0;
      if (s_tap_timer) {
        app_timer_cancel(s_tap_timer);
        s_tap_timer = NULL;
      }
    }
  }
}

// -----------------------------------------------------------------------------
// DTW / FLICK ENGINE (25HZ LOOP)
// -----------------------------------------------------------------------------
static int calculate_dtw_distance() {
  if (!s_has_trained_gesture) return 999999;

  int total_distance = 0;
  for (int i = 0; i < s_current_buffer_size; i++) {
    int buf_idx = (s_buffer.index + i) % s_current_buffer_size;
    AccelData p = s_buffer.data[buf_idx];
    CustomAccelData t = s_gesture_template[i];

    int dx = ((p.x - t.x) * s_worker_settings.x_multiplier) / 1000;
    int dy = ((p.y - t.y) * s_worker_settings.y_multiplier) / 1000;
    int dz = ((p.z - t.z) * s_worker_settings.z_multiplier) / 1000;

    total_distance += (abs(dx) + abs(dy) + abs(dz));
  }
  return total_distance;
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  if (s_cooldown > 0) {
    s_cooldown -= num_samples;
    return;
  }

  for (uint32_t i = 0; i < num_samples; i++) {
    s_buffer.data[s_buffer.index] = data[i];
    s_buffer.index = (s_buffer.index + 1) % s_current_buffer_size;
    if (s_buffer.index == 0) s_buffer.is_full = true;
  }

  if (!s_buffer.is_full) return;

  if (s_worker_settings.gesture_mode == 2 && s_has_trained_gesture) {
    // Battery Saver: Pre-calculated threshold replaces heavy float/division math per tick
    int optimized_threshold = 20000;

    int current_distance = calculate_dtw_distance();
    if (current_distance < optimized_threshold) {
      s_cooldown = 25 * 3;
      trigger_app(2); // Wake reason 2: Custom DTW Gesture
    }
  } else if (s_worker_settings.gesture_mode == 0) {
    // Default wrist flick gesture ("Anti-Clap Wrist Flick: Bi-Directional Snap")
    int16_t recent_y = s_buffer.data[(s_buffer.index - 1 + s_current_buffer_size) % s_current_buffer_size].y;
    int16_t old_y = s_buffer.data[s_buffer.index].y;

    if (abs(recent_y - old_y) > (s_worker_settings.default_flick_sensitivity * 20)) {
      s_cooldown = 25 * 3;
      trigger_app(1); // Wake reason 1: Default Flick
    }
  }
}

// -----------------------------------------------------------------------------
// WORKER ENTRY POINT
// -----------------------------------------------------------------------------
int main(void) {
  // Initialize Default Worker Settings
  s_worker_settings.respect_quiet_time = true;
  s_worker_settings.quiet_start_hour = 22;
  s_worker_settings.quiet_end_hour = 7;
  s_worker_settings.night_volume = 10;
  s_worker_settings.night_worker_sleep = true;

  s_worker_settings.enable_experimental_features = false;

  s_worker_settings.gesture_mode = 0;
  s_worker_settings.default_flick_sensitivity = 62;
  s_worker_settings.tap_sensitivity = 15;
  s_worker_settings.x_multiplier = 1000;
  s_worker_settings.y_multiplier = 1000;
  s_worker_settings.z_multiplier = 1000;
  s_worker_settings.is_us_dialect = false;

  // NEW: Initialize isolated pacing and trim arrays explicitly
  for (int i = 0; i < 6; i++) {
    s_worker_settings.mode_speed[i] = -20;
    s_worker_settings.mode_trim[i] = 0;
  }

  // Load from persistent storage with the new SETTINGS_PERSIST_KEY (14)
  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    WhisperSettings temp_settings;
    int bytes = persist_read_data(SETTINGS_PERSIST_KEY, &temp_settings, sizeof(WhisperSettings));
    if (bytes == sizeof(WhisperSettings)) {
      s_worker_settings = temp_settings;
    }
  }

  s_current_buffer_size = s_worker_settings.gesture_buffer_size;
  if (s_current_buffer_size > MAX_BUFFER_SIZE) {
    s_current_buffer_size = MAX_BUFFER_SIZE;
  }

  if (!s_worker_settings.enable_experimental_features) {
    return 0; // Exit cleanly if Experimental Features are disabled
  }

  // Quiet Time Logic: Suspend the worker entirely at night to conserve battery
  if (s_worker_settings.respect_quiet_time && s_worker_settings.night_worker_sleep) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    uint8_t h = t->tm_hour;
    bool is_quiet = false;

    if (s_worker_settings.quiet_start_hour > s_worker_settings.quiet_end_hour) {
      is_quiet = (h >= s_worker_settings.quiet_start_hour || h < s_worker_settings.quiet_end_hour);
    } else {
      is_quiet = (h >= s_worker_settings.quiet_start_hour && h < s_worker_settings.quiet_end_hour);
    }

    if (is_quiet) {
      return 0; // Die gracefully until next launch
    }
  }

  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_read_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
    s_has_trained_gesture = true;
  }

  // Hook into native Tap service (Tap mode only)
  if (s_worker_settings.gesture_mode == 1) {
    accel_tap_service_subscribe(accel_tap_handler);
  }

  // Hook into Data service for raw 25Hz tracking (DTW or Default Flick modes)
  if (s_worker_settings.gesture_mode == 0 || s_worker_settings.gesture_mode == 2) {
    s_buffer.index = 0;
    s_buffer.is_full = false;
    accel_data_service_subscribe(25, accel_data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
  }

  // Block thread until app is launched or explicitly killed
  worker_event_loop();

  // Clean up
  accel_data_service_unsubscribe();
  accel_tap_service_unsubscribe();

  return 0;
}
