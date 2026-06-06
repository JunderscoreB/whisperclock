/*
 * WhisperClock - Audio Engine Header
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#pragma once
#include <pebble.h>

/**
 * @struct AudioQueueItem
 * @brief Represents a single audio file and label in the playback queue.
 */
typedef struct {
  char filename[16];
  char display_text[16];
  int16_t delay_mod; // Additional ms delay after this clip plays
  int16_t trim_mod;  // Additional ms trim to cut the end of this clip early
} AudioQueueItem;

void generate_audio_playlist(void);
void trigger_playback(bool auto_exit);
void cancel_playback(void);
