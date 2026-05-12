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

static CustomAccelData s_gesture_template[MAX_BUFFER_SIZE];
static int s_recording_index = 0;
static Window *s_recording_window;

// UI State Variables
static TextLayer *s_recording_layer;     // Giant center text
static TextLayer *s_helper_text_layer;   // Smaller bottom text
static char s_dynamic_text_buffer[64];
static char s_helper_text_buffer[64];    // Buffer for "Recording Time X Seconds"

static bool s_is_recording = false; 
static int s_countdown_ticks = 0;
static AppTimer *s_ready_timer = NULL;
static AppTimer *s_countdown_timer = NULL;
static AppTimer *s_close_timer = NULL;

// -------------------------------------------------------------------------
// THE MAIN TRIGGER GATEWAY (Quiet Time Logic)
// -------------------------------------------------------------------------
void on_gesture_detected() {
  if (s_settings.respect_quiet_time && quiet_time_is_active()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered, but Quiet Time is ON. Staying silent.");
    return; 
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered! Speaking the time.");
  show_speaking_graphic();
  trigger_playback(true); 
}

// -------------------------------------------------------------------------
// DEFAULT WRIST FLICK HANDLER
// -------------------------------------------------------------------------
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (!s_is_recording) {
    if (axis == ACCEL_AXIS_Y || axis == ACCEL_AXIS_Z) {
      on_gesture_detected();
    }
  }
}

// -------------------------------------------------------------------------
// CUSTOM GESTURE RECORDING LOGIC
// -------------------------------------------------------------------------
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
  persist_write_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
  
  // Phase 3: Saved Screen (Red)
  window_set_background_color(s_recording_window, GColorRed);
  
  text_layer_set_text(s_recording_layer, "Saved!");
  // Hide the helper text since the recording is over
  layer_set_hidden(text_layer_get_layer(s_helper_text_layer), true); 
  
  vibes_double_pulse();
  
  s_close_timer = app_timer_register(2000, delayed_pop_callback, NULL);
}

static void recording_tick_callback(void *data) {
  if (s_countdown_ticks <= 0) {
    finish_recording(NULL);
    return;
  }

  int whole_sec = s_countdown_ticks / 10;
  int frac_sec = s_countdown_ticks % 10;

  snprintf(s_dynamic_text_buffer, sizeof(s_dynamic_text_buffer), "Recording...\n%d.%d", whole_sec, frac_sec);
  text_layer_set_text(s_recording_layer, s_dynamic_text_buffer);

  s_countdown_ticks--;
  
  s_countdown_timer = app_timer_register(100, recording_tick_callback, NULL);
}

static void start_listening(void *data) {
  s_ready_timer = NULL;
  
  // Phase 2: Recording Screen (Green)
  window_set_background_color(s_recording_window, GColorKellyGreen);
  
  s_recording_index = 0;
  vibes_short_pulse(); 
  
  accel_data_service_subscribe(1, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);

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

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Phase 1: Get Ready Screen (Amber)
  window_set_background_color(window, GColorOrange);

  // --- Giant Center Text ---
  s_recording_layer = text_layer_create(GRect(0, (bounds.size.h / 2) - 40, bounds.size.w, 100));
  text_layer_set_background_color(s_recording_layer, GColorClear);
  text_layer_set_text_color(s_recording_layer, GColorWhite);
  text_layer_set_text_alignment(s_recording_layer, GTextAlignmentCenter);
  text_layer_set_font(s_recording_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  
  text_layer_set_text(s_recording_layer, "Get Ready...");
  layer_add_child(window_layer, text_layer_get_layer(s_recording_layer));

  // --- Smaller Helper Text (Bottom) ---
  int whole_sec = s_settings.gesture_buffer_size / 10;
  int frac_sec = s_settings.gesture_buffer_size % 10;
  snprintf(s_helper_text_buffer, sizeof(s_helper_text_buffer), "Recording Time %d.%d sec\n\n(Press any button to cancel)", whole_sec, frac_sec);

  s_helper_text_layer = text_layer_create(GRect(0, bounds.size.h - 55, bounds.size.w, 60));
  text_layer_set_background_color(s_helper_text_layer, GColorClear);
  text_layer_set_text_color(s_helper_text_layer, GColorWhite);
  text_layer_set_text_alignment(s_helper_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_helper_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  
  text_layer_set_text(s_helper_text_layer, s_helper_text_buffer);
  layer_add_child(window_layer, text_layer_get_layer(s_helper_text_layer));

  s_ready_timer = app_timer_register(3000, start_listening, NULL);
}

static void window_unload(Window *window) {
  if (s_ready_timer) app_timer_cancel(s_ready_timer);
  if (s_countdown_timer) app_timer_cancel(s_countdown_timer);
  if (s_close_timer) app_timer_cancel(s_close_timer);

  if (s_is_recording) {
    accel_data_service_unsubscribe();
    s_is_recording = false;
  }

  text_layer_destroy(s_recording_layer);
  text_layer_destroy(s_helper_text_layer);
}

void gesture_start_recording() {
  s_is_recording = true; 
  s_recording_window = window_create();
  
  window_set_click_config_provider(s_recording_window, recording_click_config_provider);
  
  window_set_window_handlers(s_recording_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_recording_window, true);
}

// -------------------------------------------------------------------------
// BACKGROUND LIFECYCLE
// -------------------------------------------------------------------------
void gesture_engine_init() {
  accel_tap_service_subscribe(accel_tap_handler);
}

void gesture_engine_deinit() {
  accel_tap_service_unsubscribe();
}
