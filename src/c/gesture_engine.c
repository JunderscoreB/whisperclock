/*
 * WhisperClock - Gesture Engine Implementation
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include <pebble.h>
#include "gesture_engine.h"
#include "settings_engine.h"
#include "audio_engine.h"

#define MAX_BUFFER_SIZE 50
#define GESTURE_PERSIST_KEY 2
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MIN3(a, b, c) MIN(MIN(a, b), c)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAX3(a, b, c) MAX(MAX(a, b), c)

extern WhisperSettings s_settings;

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
static int s_recording_index = 0;
static bool s_is_recording = false;
static bool s_is_paused = false;

// Physics Variables
static int s_cooldown = 0;
static bool s_has_trained_gesture = false;
static int s_current_buffer_size = 25;
static uint32_t s_absolute_tick = 0;
static int16_t s_last_accel_x = 0;
static int16_t s_last_accel_y = 0;
static int16_t s_last_accel_z = 0;
static bool s_accel_primed = false;

// Default Flick Bi-Directional State Tracking
static uint8_t s_flick_state = 0;
static int8_t s_flick_initial_sign = 0;
static uint8_t s_flick_timeout = 0;

// DTW Memory Arrays
static int32_t s_prev_row[MAX_BUFFER_SIZE];
static int32_t s_curr_row[MAX_BUFFER_SIZE];
static AccelData s_flat_live_data[MAX_BUFFER_SIZE];
static CustomAccelData s_centered_live[MAX_BUFFER_SIZE];
static CustomAccelData s_centered_saved[MAX_BUFFER_SIZE];

// UI Variables
static Window *s_recording_window = NULL;
static TextLayer *s_recording_layer;
static TextLayer *s_helper_text_layer;
static char s_dynamic_text_buffer[64];
static char s_helper_text_buffer[64];
static int s_countdown_ticks = 0;
static AppTimer *s_ready_timer = NULL;
static AppTimer *s_countdown_timer = NULL;
static AppTimer *s_close_timer = NULL;

void gesture_engine_pause(void) { s_is_paused = true; }
void gesture_engine_resume(void) { s_is_paused = false; s_cooldown = 20; }

static bool is_custom_quiet_time() {
  if (!s_settings.respect_quiet_time) return false;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int hour = t->tm_hour;

  if (s_settings.quiet_start_hour == s_settings.quiet_end_hour) return false;
  if (s_settings.quiet_start_hour < s_settings.quiet_end_hour) {
    return (hour >= s_settings.quiet_start_hour && hour < s_settings.quiet_end_hour);
  } else {
    return (hour >= s_settings.quiet_start_hour || hour < s_settings.quiet_end_hour);
  }
}

void on_gesture_detected() {
  bool in_qt = is_custom_quiet_time();
  bool is_muted = in_qt ? (s_settings.night_volume == 0) : (s_settings.volume == 0);

  if (is_muted) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered, but active volume is MUTE. Staying silent.");
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered! Speaking the time.");
  show_speaking_graphic();
  trigger_playback(true);
}

static int32_t get_distance(CustomAccelData live_point, CustomAccelData template_point) {
  return abs(live_point.x - template_point.x) + abs(live_point.y - template_point.y) + abs(live_point.z - template_point.z);
}

static int32_t calculate_dtw_cost(uint32_t length) {
  s_prev_row[0] = get_distance(s_centered_live[0], s_centered_saved[0]);
  for (uint32_t j = 1; j < length; j++) s_prev_row[j] = s_prev_row[j-1] + get_distance(s_centered_live[0], s_centered_saved[j]);

  for (uint32_t i = 1; i < length; i++) {
    s_curr_row[0] = s_prev_row[0] + get_distance(s_centered_live[i], s_centered_saved[0]);
    for (uint32_t j = 1; j < length; j++) {
      int32_t cost = get_distance(s_centered_live[i], s_centered_saved[j]);
      int32_t cheapest_path = MIN3(s_prev_row[j], s_curr_row[j-1], s_prev_row[j-1]);
      s_curr_row[j] = cost + cheapest_path;
    }
    for (uint32_t k = 0; k < length; k++) s_prev_row[k] = s_curr_row[k];
  }
  return s_prev_row[length - 1];
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  if (s_is_paused && !s_is_recording) return;

  for (uint32_t i = 0; i < num_samples; i++) {
    if (s_is_recording) {
      if (s_recording_index < s_settings.gesture_buffer_size) {
        s_gesture_template[s_recording_index].x = data[i].x;
        s_gesture_template[s_recording_index].y = data[i].y;
        s_gesture_template[s_recording_index].z = data[i].z;
        s_recording_index++;
      }
      continue;
    }

    s_absolute_tick++;

    if (s_cooldown > 0) {
      s_cooldown--;
      continue;
    }

    if (!s_accel_primed) {
      s_last_accel_x = data[i].x;
      s_last_accel_y = data[i].y;
      s_last_accel_z = data[i].z;
      s_accel_primed = true;
      continue;
    }

    int32_t raw_dx = data[i].x - s_last_accel_x;
    int32_t raw_dy = data[i].y - s_last_accel_y;
    int32_t raw_dz = data[i].z - s_last_accel_z;

    s_last_accel_x = data[i].x;
    s_last_accel_y = data[i].y;
    s_last_accel_z = data[i].z;

    // --- BRANCH A: DEFAULT WRIST-FLICK MODE ---
    if (s_settings.gesture_mode == 0) {

      int32_t dy = (raw_dy * (int32_t)(s_settings.default_flick_sensitivity * 10)) / 1000L;
      int32_t y_sq = dy * dy;

      if (s_flick_state == 0) {
        if (y_sq > 1500000) {
          s_flick_state = 1;
          s_flick_initial_sign = (dy > 0) ? 1 : -1;
          s_flick_timeout = 6;
        }
      }
      else if (s_flick_state == 1) {
        s_flick_timeout--;
        if (s_flick_timeout == 0) {
          s_flick_state = 0;
        } else {
          if (y_sq > 1500000) {
            int8_t current_sign = (dy > 0) ? 1 : -1;
            if (current_sign != s_flick_initial_sign) {

              APP_LOG(APP_LOG_LEVEL_INFO, ">>> APP SNAP FLICK TRIGGERED! Timeout Left: %d", s_flick_timeout);

              s_flick_state = 0;
              s_cooldown = 50;
              on_gesture_detected();
            }
          }
        }
      }
      continue;
    }

    // --- BRANCH C: TAP GLASS MODE ---
    else if (s_settings.gesture_mode == 1) {
      int32_t multiplier = 70 - s_settings.tap_sensitivity;

      int32_t dx = (raw_dx * multiplier) / 100L;
      int32_t dy = (raw_dy * multiplier) / 100L;
      int32_t dz = (raw_dz * multiplier) / 100L;

      int32_t x_sq = dx * dx;
      int32_t y_sq = dy * dy;
      int32_t z_sq = dz * dz;

      if (z_sq > 1500000) {
        if (z_sq > (x_sq + y_sq) * 2) {
          APP_LOG(APP_LOG_LEVEL_INFO, ">>> APP TAP TRIGGERED! Z: %ld, XY Wobble: %ld", z_sq, (x_sq + y_sq));
          s_cooldown = 50;
          on_gesture_detected();
        }
      }
      continue;
    }


    // --- BRANCH B: CUSTOM AXES MODE ---
    if (s_settings.x_multiplier == 0 && s_settings.y_multiplier == 0 && s_settings.z_multiplier == 0) continue;

    int32_t dx = (s_settings.x_multiplier == 0) ? 0 : (raw_dx * s_settings.x_multiplier) / 1000L;
    int32_t dy = (s_settings.y_multiplier == 0) ? 0 : (raw_dy * s_settings.y_multiplier) / 1000L;
    int32_t dz = (s_settings.z_multiplier == 0) ? 0 : (raw_dz * s_settings.z_multiplier) / 1000L;

    if (s_absolute_tick % 25 == 0) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "APP UI | X: %ld | Y: %ld | Z: %ld", dx, dy, dz);
    }

    if (!s_has_trained_gesture) {
      int32_t jerk_sq = (dx * dx) + (dy * dy) + (dz * dz);

      if (jerk_sq > 1500000) {
        APP_LOG(APP_LOG_LEVEL_INFO, ">>> APP CUSTOM FLICK TRIGGERED! Jerk_Sq: %ld", jerk_sq);
        APP_LOG(APP_LOG_LEVEL_INFO, ">>> APP AXIS SNAPSHOT | X: %ld, Y: %ld, Z: %ld", dx, dy, dz);

        s_cooldown = 50;
        on_gesture_detected();
      }
      continue;
    }

    s_buffer.data[s_buffer.index] = data[i];
    s_buffer.index++;

    if (s_buffer.index >= s_current_buffer_size) {
      s_buffer.index = 0;
      s_buffer.is_full = true;
    }

    if (s_buffer.is_full && (s_absolute_tick % 5 == 0)) {
      int16_t min_x = 4000, max_x = -4000;
      int16_t min_y = 4000, max_y = -4000;
      int16_t min_z = 4000, max_z = -4000;

      for (int j = 0; j < s_current_buffer_size; j++) {
        if (s_buffer.data[j].x < min_x) min_x = s_buffer.data[j].x;
        if (s_buffer.data[j].x > max_x) max_x = s_buffer.data[j].x;
        if (s_buffer.data[j].y < min_y) min_y = s_buffer.data[j].y;
        if (s_buffer.data[j].y > max_y) max_y = s_buffer.data[j].y;
        if (s_buffer.data[j].z < min_z) min_z = s_buffer.data[j].z;
        if (s_buffer.data[j].z > max_z) max_z = s_buffer.data[j].z;
      }

      int16_t range_x = max_x - min_x;
      int16_t range_y = max_y - min_y;
      int16_t range_z = max_z - min_z;

      bool skip_dtw = true;
      if (s_settings.x_multiplier > 0) {
        if (range_x >= (800L * 1000L) / s_settings.x_multiplier) skip_dtw = false;
      }
      if (s_settings.y_multiplier > 0) {
        if (range_y >= (800L * 1000L) / s_settings.y_multiplier) skip_dtw = false;
      }
      if (s_settings.z_multiplier > 0) {
        if (range_z >= (800L * 1000L) / s_settings.z_multiplier) skip_dtw = false;
      }
      if (skip_dtw) continue;

      uint32_t flat_index = 0;
      for (uint32_t j = s_buffer.index; j < (uint32_t)s_current_buffer_size; j++) s_flat_live_data[flat_index++] = s_buffer.data[j];
      for (uint32_t j = 0; j < s_buffer.index; j++) s_flat_live_data[flat_index++] = s_buffer.data[j];

      int16_t offset_live_x = s_flat_live_data[s_current_buffer_size - 1].x;
      int16_t offset_live_y = s_flat_live_data[s_current_buffer_size - 1].y;
      int16_t offset_live_z = s_flat_live_data[s_current_buffer_size - 1].z;

      int16_t offset_saved_x = s_gesture_template[s_current_buffer_size - 1].x;
      int16_t offset_saved_y = s_gesture_template[s_current_buffer_size - 1].y;
      int16_t offset_saved_z = s_gesture_template[s_current_buffer_size - 1].z;

      for (int j = 0; j < s_current_buffer_size; j++) {
        int32_t weight = 50 + ((j * 50) / (s_current_buffer_size - 1));

        int32_t live_x = (s_settings.x_multiplier == 0) ? 0 : s_flat_live_data[j].x - offset_live_x;
        int32_t live_y = (s_settings.y_multiplier == 0) ? 0 : s_flat_live_data[j].y - offset_live_y;
        int32_t live_z = (s_settings.z_multiplier == 0) ? 0 : s_flat_live_data[j].z - offset_live_z;

        int32_t saved_x = (s_settings.x_multiplier == 0) ? 0 : s_gesture_template[j].x - offset_saved_x;
        int32_t saved_y = (s_settings.y_multiplier == 0) ? 0 : s_gesture_template[j].y - offset_saved_y;
        int32_t saved_z = (s_settings.z_multiplier == 0) ? 0 : s_gesture_template[j].z - offset_saved_z;

        s_centered_live[j].x = (live_x * weight) / 100;
        s_centered_live[j].y = (live_y * weight) / 100;
        s_centered_live[j].z = (live_z * weight) / 100;

        s_centered_saved[j].x = (saved_x * weight) / 100;
        s_centered_saved[j].y = (saved_y * weight) / 100;
        s_centered_saved[j].z = (saved_z * weight) / 100;
      }

      int32_t dtw_cost = calculate_dtw_cost(s_current_buffer_size);

      int32_t max_mult = MAX3(s_settings.x_multiplier, s_settings.y_multiplier, s_settings.z_multiplier);
      int32_t base_threshold = s_current_buffer_size * 3000;
      int32_t dynamic_threshold = (base_threshold * max_mult) / 1000;

      if (dtw_cost < dynamic_threshold) {
        APP_LOG(APP_LOG_LEVEL_INFO, ">>> APP DTW GESTURE TRIGGERED! Cost: %ld | Threshold: %ld", dtw_cost, dynamic_threshold);

        s_cooldown = 50;
        s_buffer.is_full = false;
        s_buffer.index = 0;
        on_gesture_detected();
      }
    }
  }
}

static void delayed_pop_callback(void *data) {
  s_close_timer = NULL;
  window_stack_pop(true);
}

static void finish_recording(void *data) {
  s_countdown_timer = NULL;
  s_is_recording = false;
  s_has_trained_gesture = true;

  persist_write_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));

  window_set_background_color(s_recording_window, GColorRed);
  text_layer_set_text(s_recording_layer, "Saved!");
  layer_set_hidden(text_layer_get_layer(s_helper_text_layer), true);

  vibes_double_pulse();
  s_close_timer = app_timer_register(2000, delayed_pop_callback, NULL);
}

static void recording_tick_callback(void *data) {
  if (s_countdown_ticks <= 0) {
    finish_recording(NULL);
    return;
  }

  int display_time = (s_countdown_ticks * 20) / 100;
  snprintf(s_dynamic_text_buffer, sizeof(s_dynamic_text_buffer), "Recording...\n%d.%d", display_time / 10, display_time % 10);
  text_layer_set_text(s_recording_layer, s_dynamic_text_buffer);

  s_countdown_ticks--;
  s_countdown_timer = app_timer_register(20, recording_tick_callback, NULL);
}

static void start_listening(void *data) {
  s_ready_timer = NULL;
  window_set_background_color(s_recording_window, GColorKellyGreen);

  s_recording_index = 0;
  vibes_short_pulse();

  s_countdown_ticks = s_settings.gesture_buffer_size;
  recording_tick_callback(NULL);
}

static void cancel_recording_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void recording_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, cancel_recording_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, cancel_recording_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, cancel_recording_handler);
}

static void recording_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorOrange);

  s_recording_layer = text_layer_create(GRect(0, (bounds.size.h / 2) - 40, bounds.size.w, 100));
  text_layer_set_background_color(s_recording_layer, GColorClear);
  text_layer_set_text_color(s_recording_layer, GColorWhite);
  text_layer_set_text_alignment(s_recording_layer, GTextAlignmentCenter);
  text_layer_set_font(s_recording_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text(s_recording_layer, "Get Ready...");
  layer_add_child(window_layer, text_layer_get_layer(s_recording_layer));

  int display_time = (s_settings.gesture_buffer_size * 20) / 100;
  snprintf(s_helper_text_buffer, sizeof(s_helper_text_buffer), "Recording Time %d.%d sec\n\n(Press button to cancel)", display_time / 10, display_time % 10);

  s_helper_text_layer = text_layer_create(GRect(0, bounds.size.h - 55, bounds.size.w, 60));
  text_layer_set_background_color(s_helper_text_layer, GColorClear);
  text_layer_set_text_color(s_helper_text_layer, GColorWhite);
  text_layer_set_text_alignment(s_helper_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_helper_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_helper_text_layer, s_helper_text_buffer);
  layer_add_child(window_layer, text_layer_get_layer(s_helper_text_layer));

  s_ready_timer = app_timer_register(3000, start_listening, NULL);
}

static void recording_window_unload(Window *window) {
  if (s_ready_timer) app_timer_cancel(s_ready_timer);
  if (s_countdown_timer) app_timer_cancel(s_countdown_timer);
  if (s_close_timer) app_timer_cancel(s_close_timer);

  s_is_recording = false;
  if (s_recording_layer) text_layer_destroy(s_recording_layer);
  if (s_helper_text_layer) text_layer_destroy(s_helper_text_layer);
}

void gesture_start_recording() {
  if (!s_recording_window) {
    s_recording_window = window_create();
    window_set_click_config_provider(s_recording_window, recording_click_config_provider);
    window_set_window_handlers(s_recording_window, (WindowHandlers) {
      .load = recording_window_load,
      .unload = recording_window_unload,
    });
  }
  s_is_recording = true;
  window_stack_push(s_recording_window, true);
}

void gesture_engine_init() {
  s_current_buffer_size = s_settings.gesture_buffer_size;

  s_flick_state = 0;
  s_flick_initial_sign = 0;
  s_flick_timeout = 0;

  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_read_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
    s_has_trained_gesture = true;
  } else {
    s_has_trained_gesture = false;
  }

  accel_data_service_subscribe(5, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
}

void gesture_engine_deinit() {
  accel_data_service_unsubscribe();
  if (s_recording_window) {
    window_destroy(s_recording_window);
    s_recording_window = NULL;
  }
}
