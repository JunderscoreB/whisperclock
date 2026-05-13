#pragma once
#include <pebble.h>

typedef struct {
  bool say_its;
  bool say_ampm;
  int16_t playback_speed;
  int16_t gesture_buffer_size;
  uint8_t clock_mode;
  uint8_t volume;
  int16_t clip_trim;
  bool respect_quiet_time;
  uint8_t trigger_mode; // NEW: 0 = Gesture, 1 = Tap, 2 = Both
  uint8_t tap_count;    // NEW: Number of taps required (e.g., 2, 3, 4)
} WhisperSettings;

void settings_init(void);
void settings_window_push(void);
void show_speaking_graphic(void);
void hide_speaking_graphic(void);
