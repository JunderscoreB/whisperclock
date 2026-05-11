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
} WhisperSettings;

void settings_init(void);
void settings_window_push(void);
void show_speaking_graphic(void);
void hide_speaking_graphic(void);
