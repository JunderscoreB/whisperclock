/*
 * WhisperClock - Settings Engine Header
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#pragma once
#include <pebble.h>

// Bumped to 14 to force a clean memory wipe during OTA update
#define SETTINGS_PERSIST_KEY 14

// Prefix Enums
#define PREFIX_NONE 0
#define PREFIX_ITS 1
#define PREFIX_THE_TIME_IS 2

// Mode Enums
#define MODE_12H_DIGITAL 0
#define MODE_24H_MILITARY 1
#define MODE_24H_CIVILIAN 2
#define MODE_COLLOQUIAL 3
#define MODE_TELECOM 4
#define MODE_FUZZY 5
#define MODE_SYSTEM_DEFAULT 6

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

  // OTHER TUNER VARIABLES
  int16_t oh_glide;
  int16_t telecom_offset;

} WhisperSettings;

void settings_init(void);
void settings_deinit(void);
void settings_window_push(void);
uint32_t get_current_active_volume(void);
void save_settings(void);
