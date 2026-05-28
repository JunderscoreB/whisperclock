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
} AudioQueueItem;

void generate_audio_playlist(void);
void trigger_playback(bool auto_exit);
void cancel_playback(void);
