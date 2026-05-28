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

#define MAX_BUFFER_SIZE 40
#define GESTURE_PERSIST_KEY 2

extern WhisperSettings s_settings;

typedef struct {
  int16_t x; int16_t y; int16_t z;
} CustomAccelData;

// Buffer to store the recorded gesture points which will be persisted to memory
static CustomAccelData s_gesture_template[MAX_BUFFER_SIZE];
static int s_recording_index = 0;

static Window *s_recording_window = NULL;
static TextLayer *s_recording_layer;     
static TextLayer *s_helper_text_layer;   
static char s_dynamic_text_buffer[64];
static char s_helper_text_buffer[64];    

static bool s_is_recording = false; 
static int s_countdown_ticks = 0;
static AppTimer *s_ready_timer = NULL;
static AppTimer *s_countdown_timer = NULL;
static AppTimer *s_close_timer = NULL;

void on_gesture_detected() {
  if (s_settings.respect_quiet_time && quiet_time_is_active()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered, but Quiet Time is ON. Staying silent.");
    return; 
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered! Speaking the time.");
  show_speaking_graphic();
  trigger_playback(true); 
}

static uint32_t s_last_tap_epoch_ms = 0;
static int s_current_taps = 0;

/**
 * @brief Foreground fallback tap handler, used primarily when testing 
 * gesture configurations inside the app. Background processing handled by worker.
 */
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_is_recording) return;

  time_t now_s;
  uint16_t now_ms;
  time_ms(&now_s, &now_ms);
  uint32_t now_epoch_ms = (now_s * 1000) + now_ms;

  if (s_settings.trigger_mode == 1) { 
    if (axis == ACCEL_AXIS_Z || axis == ACCEL_AXIS_X || axis == ACCEL_AXIS_Y) {
      if (now_epoch_ms - s_last_tap_epoch_ms < 200) return; 

      if (now_epoch_ms - s_last_tap_epoch_ms > 2500) s_current_taps = 1;
      else s_current_taps++;
      
      s_last_tap_epoch_ms = now_epoch_ms;
      if (s_current_taps >= s_settings.tap_count) {
        s_current_taps = 0;
        on_gesture_detected();
      }
    }
  } 
  else if (s_settings.trigger_mode == 0) { 
    if (axis == ACCEL_AXIS_Y || axis == ACCEL_AXIS_Z) {
      if (now_epoch_ms - s_last_tap_epoch_ms < 1000) return; 
      s_last_tap_epoch_ms = now_epoch_ms;
      on_gesture_detected();
    }
  } 
  else { 
    if (axis == ACCEL_AXIS_Z || axis == ACCEL_AXIS_X || axis == ACCEL_AXIS_Y) {
      if (now_epoch_ms - s_last_tap_epoch_ms < 200) return;

      if (now_epoch_ms - s_last_tap_epoch_ms > 2500) s_current_taps = 1;
      else s_current_taps++;
      
      s_last_tap_epoch_ms = now_epoch_ms;
      if (s_current_taps >= s_settings.tap_count) {
        s_current_taps = 0;
        on_gesture_detected();
      }
    } else if (axis == ACCEL_AXIS_Y) {
      if (now_epoch_ms - s_last_tap_epoch_ms < 1000) return;
      s_last_tap_epoch_ms = now_epoch_ms;
      on_gesture_detected();
    }
  }
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  for (uint32_t i = 0; i < num_samples; i++) {
    if (s_recording_index < s_settings.gesture_buffer_size) {
      s_gesture_template[s_recording_index].x = data[i].x;
      s_gesture_template[s_recording_index].y = data[i].y;
      s_gesture_template[s_recording_index].z = data[i].z;
      s_recording_index++;
    }
  }
}

static void delayed_pop_callback(void *data) {
  s_close_timer = NULL;
  window_stack_pop(true);
}

static void finish_recording(void *data) {
  s_countdown_timer = NULL;
  accel_data_service_unsubscribe();
  s_is_recording = false; 
  
  // Write the recorded spatial template array to persistence for the DTW worker
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

  int display_time = (s_countdown_ticks * 40) / 100; 
  int whole_sec = display_time / 10;
  int frac_sec = display_time % 10;
  
  snprintf(s_dynamic_text_buffer, sizeof(s_dynamic_text_buffer), "Recording...\n%d.%d", whole_sec, frac_sec);
  text_layer_set_text(s_recording_layer, s_dynamic_text_buffer);

  s_countdown_ticks--;
  s_countdown_timer = app_timer_register(40, recording_tick_callback, NULL);
}

static void start_listening(void *data) {
  s_ready_timer = NULL;
  window_set_background_color(s_recording_window, GColorKellyGreen);
  
  s_recording_index = 0;
  vibes_short_pulse(); 
  
  accel_data_service_subscribe(1, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);

  s_countdown_ticks = s_settings.gesture_buffer_size;
  recording_tick_callback(NULL);
}

static void cancel_recording_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

#ifdef PBL_TOUCH
static void recording_touch_handler(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) {
    cancel_recording_handler(NULL, NULL);
  }
}
#endif

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

  int display_time = (s_settings.gesture_buffer_size * 40) / 100;
  int whole_sec = display_time / 10;
  int frac_sec = display_time % 10;
  snprintf(s_helper_text_buffer, sizeof(s_helper_text_buffer), "Recording Time %d.%d sec\n\n(Tap Screen to cancel)", whole_sec, frac_sec);

  s_helper_text_layer = text_layer_create(GRect(0, bounds.size.h - 55, bounds.size.w, 60));
  text_layer_set_background_color(s_helper_text_layer, GColorClear);
  text_layer_set_text_color(s_helper_text_layer, GColorWhite);
  text_layer_set_text_alignment(s_helper_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_helper_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_helper_text_layer, s_helper_text_buffer);
  layer_add_child(window_layer, text_layer_get_layer(s_helper_text_layer));

  s_ready_timer = app_timer_register(3000, start_listening, NULL);
}

static void recording_window_appear(Window *window) {
#ifdef PBL_TOUCH
  if (touch_service_is_enabled()) touch_service_subscribe(recording_touch_handler, NULL);
#endif
}

static void recording_window_disappear(Window *window) {
#ifdef PBL_TOUCH
  touch_service_unsubscribe();
#endif
}

static void recording_window_unload(Window *window) {
  if (s_ready_timer) app_timer_cancel(s_ready_timer);
  if (s_countdown_timer) app_timer_cancel(s_countdown_timer);
  if (s_close_timer) app_timer_cancel(s_close_timer);

  if (s_is_recording) {
    accel_data_service_unsubscribe();
    s_is_recording = false;
  }

  if (s_recording_layer) text_layer_destroy(s_recording_layer);
  if (s_helper_text_layer) text_layer_destroy(s_helper_text_layer);
}

void gesture_start_recording() {
  if (!s_recording_window) {
    s_recording_window = window_create();
    window_set_click_config_provider(s_recording_window, recording_click_config_provider);
    window_set_window_handlers(s_recording_window, (WindowHandlers) {
      .load = recording_window_load,
      .appear = recording_window_appear,
      .disappear = recording_window_disappear,
      .unload = recording_window_unload,
    });
  }
  s_is_recording = true; 
  window_stack_push(s_recording_window, true);
}

void gesture_engine_init() {
  accel_tap_service_subscribe(accel_tap_handler);
}

void gesture_engine_deinit() {
  accel_tap_service_unsubscribe();
  if (s_recording_window) {
    window_destroy(s_recording_window);
    s_recording_window = NULL;
  }
}
