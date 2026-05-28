/*
 * WhisperClock - Settings Engine Header
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 *
 * AI Disclosure: Portions of this file, including system architecture, 
 * audio upsampling algorithms, and preprocessor UI toggles, were 
 * generated and optimized with the assistance of generative AI 
 * (Google Gemini).
 */

#pragma once
#include <pebble.h>

#define SETTINGS_PERSIST_KEY 3

typedef struct __attribute__((__packed__)) {
  bool say_its;
  bool say_ampm;
  int16_t playback_speed;
  int16_t gesture_buffer_size;
  uint8_t clock_mode;
  uint8_t volume;
  int16_t clip_trim;
  bool respect_quiet_time;
  uint8_t trigger_mode; 
  uint8_t tap_count;    
  uint8_t quiet_start_hour; 
  uint8_t quiet_end_hour; 
  bool enable_beta_features; 
} WhisperSettings;

// Globally exposed settings instance
extern WhisperSettings s_settings;

// Window lifecycle
void settings_init(void);
void settings_window_push(void);
void settings_deinit(void);

// 🟢 RESTORED: Speaking UI Graphic Hooks
void show_speaking_graphic(void);
void hide_speaking_graphic(void);
