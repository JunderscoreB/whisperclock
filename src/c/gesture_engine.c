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
static TextLayer *s_recording_layer;

// NEW: Safety flag to prevent speaking while trying to record
static bool s_is_recording = false; 

// -------------------------------------------------------------------------
// THE MAIN TRIGGER GATEWAY (Quiet Time Logic)
// -------------------------------------------------------------------------
void on_gesture_detected() {
  // 1. Check if Quiet Time / Do Not Disturb is active
  if (s_settings.respect_quiet_time && quiet_time_is_active()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered, but Quiet Time is ON. Staying silent.");
    return; // Kill the trigger!
  }

  // 2. If we are clear to speak, fire the UI and Audio Engine
  APP_LOG(APP_LOG_LEVEL_INFO, "Gesture triggered! Speaking the time.");
  show_speaking_graphic();
  trigger_playback(true); 
}

// -------------------------------------------------------------------------
// DEFAULT WRIST FLICK HANDLER
// -------------------------------------------------------------------------
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  // Only fire the standard flick if we aren't currently recording!
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

// Helper function to satisfy strict C callback requirements
static void delayed_pop_callback(void *data) {
  window_stack_pop(true);
}

static void finish_recording(void *data) {
  accel_data_service_unsubscribe();
  s_is_recording = false; // Turn the safety flag back off
  persist_write_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
  
  text_layer_set_text(s_recording_layer, "Saved!\n\nRe-launch app\nto apply.");
  vibes_double_pulse();
  
  // Close the window after 2 seconds using our proper callback
  app_timer_register(2000, delayed_pop_callback, NULL);
}

static void start_listening(void *data) {
  text_layer_set_text(s_recording_layer, "Recording...");
  s_recording_index = 0;
  vibes_short_pulse(); // Haptic feedback so you know when to move
  
  accel_data_service_subscribe(1, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);

  // Stop recording based on your selected buffer size (e.g., 25 samples = 2.5 seconds)
  app_timer_register(s_settings.gesture_buffer_size * 100, finish_recording, NULL);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorVividCerulean);

  s_recording_layer = text_layer_create(GRect(0, (bounds.size.h / 2) - 40, bounds.size.w, 100));
  text_layer_set_background_color(s_recording_layer, GColorClear);
  text_layer_set_text_color(s_recording_layer, GColorWhite);
  text_layer_set_text_alignment(s_recording_layer, GTextAlignmentCenter);
  text_layer_set_font(s_recording_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  
  text_layer_set_text(s_recording_layer, "Get Ready...\n(3 seconds)");
  layer_add_child(window_layer, text_layer_get_layer(s_recording_layer));

  // Give the user 3 seconds to get their arm into the starting position
  app_timer_register(3000, start_listening, NULL);
}

static void window_unload(Window *window) {
  text_layer_destroy(s_recording_layer);
}

void gesture_start_recording() {
  s_is_recording = true; // Turn the safety flag on!
  s_recording_window = window_create();
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
  // Subscribe to the standard wrist flick tap service
  accel_tap_service_subscribe(accel_tap_handler);
}

void gesture_engine_deinit() {
  accel_tap_service_unsubscribe();
}
