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

/**
 * @struct WhisperSettings
 * @brief Core configuration structure for the WhisperClock app.
 * @note This struct is packed to ensure a consistent memory footprint
 * across the foreground app and the background worker.
 */
typedef struct __attribute__((__packed__)) {
  uint8_t prefix_mode;          // 0: None, 1: "It's", 2: "The time is..."
  bool say_ampm;                // Suffix the time with AM/PM
  int16_t playback_speed;       // Delay in ms between words
  int16_t gesture_buffer_size;  // Size of the DTW gesture recording window
  uint8_t clock_mode;           // 0: Auto, 1: 12-hour, 2: 24-hour
  uint8_t volume;               // Software volume multiplier (1-100)
  int16_t clip_trim;            // Milliseconds to trim from the end of audio files
  bool respect_quiet_time;      // Disable triggers during quiet hours
  uint8_t trigger_mode;         // 0: Gesture, 1: Tap, 2: Both
  uint8_t tap_count;            // Number of taps required to trigger
  uint8_t quiet_start_hour;     // Start hour for quiet time (24h format)
  uint8_t quiet_end_hour;       // End hour for quiet time (24h format)
  bool enable_beta_features;    // Toggle background worker physics
} WhisperSettings;

// Globally exposed settings instance
extern WhisperSettings s_settings;

// Window lifecycle management
void settings_init(void);
void settings_window_push(void);
void settings_deinit(void);

// Speaking UI Graphic Hooks
void show_speaking_graphic(void);
void hide_speaking_graphic(void);
