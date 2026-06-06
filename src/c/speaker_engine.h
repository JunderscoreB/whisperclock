/*
 * WhisperClock - Speaker Engine Header
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#pragma once
#include <pebble.h>

/**
 * @brief Prepares the speaker engine for playback.
 */
void speaker_init(void);

/**
 * @brief Parses a WAV resource, upsamples it, applies software volume,
 * and queues it to the DAC.
 * @param filename The virtual filename mapped to a raw resource ID.
 * @param extra_trim_ms Dynamic trim to cut the tail off this specific file.
 * @return Estimated duration of the clip in milliseconds, or 0 on error.
 */
uint32_t speaker_play_file(const char* filename, int16_t extra_trim_ms);

/**
 * @brief Immediately halts audio stream and zeroes out the buffer.
 */
void speaker_cancel(void);
