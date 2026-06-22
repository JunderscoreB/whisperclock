/*
 * WhisperClock - Speaker Engine Implementation
 * Copyright (c) 2026 J_B (Core Devices Open-Source PebbleOS)
 *
 * Released under the MIT License.
 * * Notes:
 * Integrated with 2026 PebbleOS SDK to respect SPEAKER_MAX_SAMPLE_BYTES_TOTAL,
 * preventing STM32 heap panics during long ADPCM decoding passes.
 */

#include <pebble.h>
#include "speaker_engine.h"
#include "settings_engine.h"

#define AUDIO_CHUNK_SIZE 512
#define CHUNK_DELAY_MS 20

extern WhisperSettings s_settings;

// -----------------------------------------------------------------------------
// IMA ADPCM DECODER TABLES & LOGIC
// -----------------------------------------------------------------------------
static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const uint16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5904, 6494, 7143, 7857, 8643, 9508, 10459, 11504, 12654, 13920,
    15312, 16843, 18528, 20380, 22418, 24660, 27126, 29839, 32823
};

static inline int16_t decode_ima_adpcm_nibble(uint8_t nibble, int16_t *predicted_sample, int8_t *step_index) {
    int32_t step = ima_step_table[*step_index];
    int32_t diff = step >> 3;

    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;

    // Evaluate math as 32-bit before assigning back to 16-bit
    int32_t new_sample = *predicted_sample;
    if (nibble & 8) new_sample -= diff;
    else new_sample += diff;

    if (new_sample > 32767) new_sample = 32767;
    else if (new_sample < -32768) new_sample = -32768;

    *predicted_sample = (int16_t)new_sample;

    *step_index += ima_index_table[nibble];
    if (*step_index < 0) *step_index = 0;
    else if (*step_index > 88) *step_index = 88;

    return *predicted_sample;
}

void speaker_init(void) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Native 16-Bit & ADPCM Audio Engine Initialized.");
}

static uint32_t get_resource_id_for_filename(const char* filename) {
    char first = filename[0];

    if (first == 'a') {
        if (strcmp(filename, "almost.wav") == 0) return RESOURCE_ID_almost;
        if (strcmp(filename, "am.wav") == 0) return RESOURCE_ID_am;
        if (strcmp(filename, "a_quarter.wav") == 0) return RESOURCE_ID_a_quarter;
        if (strcmp(filename, "after.wav") == 0) return RESOURCE_ID_after;
        if (strcmp(filename, "at_the_tone.wav") == 0) return RESOURCE_ID_at_the_tone;
        if (strcmp(filename, "and.wav") == 0) return RESOURCE_ID_and;
    }
    if (first == 'b' && strcmp(filename, "beep.wav") == 0) return RESOURCE_ID_beep;
    if (first == 'h') {
        if (strcmp(filename, "hour.wav") == 0) return RESOURCE_ID_hour;
        if (strcmp(filename, "hours.wav") == 0) return RESOURCE_ID_hours;
        if (strcmp(filename, "hundred.wav") == 0) return RESOURCE_ID_hundred;
        if (strcmp(filename, "half.wav") == 0) return RESOURCE_ID_half;
    }
    if (first == 'i' && strcmp(filename, "its.wav") == 0) return RESOURCE_ID_its;
    if (first == 'j' && strcmp(filename, "just_after.wav") == 0) return RESOURCE_ID_just_after;
    if (first == 'm') {
        if (strcmp(filename, "midnight.wav") == 0) return RESOURCE_ID_midnight;
        if (strcmp(filename, "minute.wav") == 0) return RESOURCE_ID_minute;
        if (strcmp(filename, "minutes.wav") == 0) return RESOURCE_ID_minutes;
    }
    if (first == 'n' && strcmp(filename, "noon.wav") == 0) return RESOURCE_ID_noon;
    if (first == 'o') {
        if (strcmp(filename, "oclock.wav") == 0) return RESOURCE_ID_oclock;
        if (strcmp(filename, "oh.wav") == 0) return RESOURCE_ID_oh;
    }
    if (first == 'p') {
        if (strcmp(filename, "pm.wav") == 0) return RESOURCE_ID_pm;
        if (strcmp(filename, "past.wav") == 0) return RESOURCE_ID_past;
        // THE FIX: ADD PRECISELY TO THE DICTIONARY
        if (strcmp(filename, "precisely.wav") == 0) return RESOURCE_ID_precisely;
    }
    if (first == 'q' && strcmp(filename, "quarter.wav") == 0) return RESOURCE_ID_quarter;
    if (first == 's') {
        if (strcmp(filename, "second.wav") == 0) return RESOURCE_ID_second;
        if (strcmp(filename, "seconds.wav") == 0) return RESOURCE_ID_seconds;
    }
    if (first == 't') {
        if (strcmp(filename, "the-time-is.wav") == 0) return RESOURCE_ID_the_time_is;
        if (strcmp(filename, "to.wav") == 0) return RESOURCE_ID_to;
        if (strcmp(filename, "till.wav") == 0) return RESOURCE_ID_till;
    }
    if (first == 'z' && strcmp(filename, "zero.wav") == 0) return RESOURCE_ID_0;

    // --- NUMERIC FILES ---
    if (first == '0' && strcmp(filename, "0.wav") == 0) return RESOURCE_ID_0;
    if (first == '1') {
        if (strcmp(filename, "1.wav") == 0) return RESOURCE_ID_1;
        if (strcmp(filename, "10.wav") == 0) return RESOURCE_ID_10;
        if (strcmp(filename, "11.wav") == 0) return RESOURCE_ID_11;
        if (strcmp(filename, "12.wav") == 0) return RESOURCE_ID_12;
        if (strcmp(filename, "13.wav") == 0) return RESOURCE_ID_13;
        if (strcmp(filename, "14.wav") == 0) return RESOURCE_ID_14;
        if (strcmp(filename, "15.wav") == 0) return RESOURCE_ID_15;
        if (strcmp(filename, "16.wav") == 0) return RESOURCE_ID_16;
        if (strcmp(filename, "17.wav") == 0) return RESOURCE_ID_17;
        if (strcmp(filename, "18.wav") == 0) return RESOURCE_ID_18;
        if (strcmp(filename, "19.wav") == 0) return RESOURCE_ID_19;
    }
    if (first == '2') {
        if (strcmp(filename, "2.wav") == 0) return RESOURCE_ID_2;
        if (strcmp(filename, "20.wav") == 0) return RESOURCE_ID_20;
    }
    if (first == '3') {
        if (strcmp(filename, "3.wav") == 0) return RESOURCE_ID_3;
        if (strcmp(filename, "30.wav") == 0) return RESOURCE_ID_30;
    }
    if (first == '4') {
        if (strcmp(filename, "4.wav") == 0) return RESOURCE_ID_4;
        if (strcmp(filename, "40.wav") == 0) return RESOURCE_ID_40;
    }
    if (first == '5') {
        if (strcmp(filename, "5.wav") == 0) return RESOURCE_ID_5;
        if (strcmp(filename, "50.wav") == 0) return RESOURCE_ID_50;
    }
    if (first == '6' && strcmp(filename, "6.wav") == 0) return RESOURCE_ID_6;
    if (first == '7' && strcmp(filename, "7.wav") == 0) return RESOURCE_ID_7;
    if (first == '8' && strcmp(filename, "8.wav") == 0) return RESOURCE_ID_8;
    if (first == '9' && strcmp(filename, "9.wav") == 0) return RESOURCE_ID_9;

    return 0;
}

static uint8_t *s_audio_buffer = NULL;
static bool s_is_stream_open = false;
static size_t s_current_res_size = 0;
static size_t s_stream_offset = 0;

static AppTimer *s_chunk_timer = NULL;
static AppTimer *s_shutdown_timer = NULL;

static const uint8_t s_silence_chunk[AUDIO_CHUNK_SIZE] = {0};

void speaker_cancel(void) {
    if (s_chunk_timer) {
        app_timer_cancel(s_chunk_timer);
        s_chunk_timer = NULL;
    }
    if (s_shutdown_timer) {
        app_timer_cancel(s_shutdown_timer);
        s_shutdown_timer = NULL;
    }
    if (s_audio_buffer != NULL) {
        free(s_audio_buffer);
        s_audio_buffer = NULL;
    }
    if (s_is_stream_open) {
        speaker_stop();
        speaker_stream_close();
        s_is_stream_open = false;
    }
}

static void close_stream_callback(void *data) {
    s_shutdown_timer = NULL;
    if (s_is_stream_open) {
        speaker_stream_close();
        s_is_stream_open = false;
    }
}

static void push_audio_chunk(void *data) {
    s_chunk_timer = NULL;
    if (!s_is_stream_open) return;
    bool hardware_buffer_full = false;
    if (s_audio_buffer != NULL) {
        while (s_stream_offset < s_current_res_size && !hardware_buffer_full) {
            uint32_t chunk = s_current_res_size - s_stream_offset;
            if (chunk > AUDIO_CHUNK_SIZE) chunk = AUDIO_CHUNK_SIZE;
            uint32_t written = speaker_stream_write(s_audio_buffer + s_stream_offset, chunk);
            s_stream_offset += written;
            if (written < chunk) hardware_buffer_full = true;
        }
        if (s_stream_offset >= s_current_res_size) {
            free(s_audio_buffer);
            s_audio_buffer = NULL;
        }
    } else {
        while (!hardware_buffer_full) {
            uint32_t written = speaker_stream_write(s_silence_chunk, AUDIO_CHUNK_SIZE);
            if (written < AUDIO_CHUNK_SIZE) hardware_buffer_full = true;
        }
    }
    s_chunk_timer = app_timer_register(CHUNK_DELAY_MS, push_audio_chunk, NULL);
}

uint32_t speaker_play_file(const char* filename, int16_t extra_trim_ms) {
    uint32_t res_id = get_resource_id_for_filename(filename);
    if (res_id == 0) return 0;
    if (s_audio_buffer != NULL) { free(s_audio_buffer); s_audio_buffer = NULL; }

    ResHandle res_handle = resource_get_handle(res_id);
    size_t res_size = resource_size(res_handle);
    if (res_size < 44) return 0;

    uint8_t *raw_buffer = (uint8_t *)malloc(res_size);
    resource_load(res_handle, raw_buffer, res_size);

    uint32_t data_offset = 44;
    uint32_t data_size = 0;
    uint16_t audio_format = 1; // Default to standard PCM
    uint16_t block_align = 2;

    // Parse WAV Header to dynamically handle both PCM and ADPCM
    for (uint32_t i = 12; i < res_size - 8; ) {
        uint32_t chunk_size = raw_buffer[i+4] | (raw_buffer[i+5] << 8) | (raw_buffer[i+6] << 16) | (raw_buffer[i+7] << 24);
        if (raw_buffer[i] == 'f' && raw_buffer[i+1] == 'm' && raw_buffer[i+2] == 't' && raw_buffer[i+3] == ' ') {
            audio_format = raw_buffer[i+8] | (raw_buffer[i+9] << 8);
            block_align = raw_buffer[i+20] | (raw_buffer[i+21] << 8);
        } else if (raw_buffer[i] == 'd' && raw_buffer[i+1] == 'a' && raw_buffer[i+2] == 't' && raw_buffer[i+3] == 'a') {
            data_size = chunk_size;
            data_offset = i + 8;
            break;
        }
        i += 8 + chunk_size;
    }

    size_t num_samples = 0;
    if (audio_format == 17) { // 17 (0x0011) indicates IMA ADPCM
        uint32_t num_blocks = data_size / block_align;
        uint32_t samples_per_block = (block_align - 4) * 2 + 1;
        num_samples = num_blocks * samples_per_block;
    } else { // Standard 16-bit uncompressed PCM
        num_samples = data_size / 2;
    }

    // Process User Trim
    int32_t total_trim_ms = (int32_t)s_settings.clip_trim + (int32_t)extra_trim_ms;
    if (total_trim_ms < 0) total_trim_ms = 0;
    size_t trim_samples = total_trim_ms * 16;
    if (num_samples > trim_samples + 256) num_samples -= trim_samples;

    // -------------------------------------------------------------------------
    // NEW SDK 2026 INTEGRATION: SAFEGUARD AGAINST HEAP PANIC
    // -------------------------------------------------------------------------
    #ifdef SPEAKER_MAX_SAMPLE_BYTES_TOTAL
    // If the fully decoded raw PCM buffer exceeds the hardware ceiling exposed
    // by the SDK, we truncate to guarantee we don't OOM crash the watch.
    if ((num_samples * 2) > SPEAKER_MAX_SAMPLE_BYTES_TOTAL) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Clip size (%lu bytes) exceeds SPEAKER_MAX_SAMPLE_BYTES_TOTAL. Truncating.",
                (unsigned long)(num_samples * 2));
        num_samples = SPEAKER_MAX_SAMPLE_BYTES_TOTAL / 2;
    }
    #endif
    // -------------------------------------------------------------------------

    s_audio_buffer = (uint8_t *)malloc(num_samples * 2);
    int16_t *audio_samples = (int16_t *)s_audio_buffer;

    // DECOMPRESS OR COPY
    if (audio_format == 17) {
        size_t sample_idx = 0;
        int16_t pred_sample = 0;
        int8_t step_idx = 0;

        uint32_t blocks_to_decode = (num_samples + ((block_align - 4) * 2)) / ((block_align - 4) * 2 + 1);

        for (uint32_t b = 0; b < blocks_to_decode && sample_idx < num_samples; b++) {
            uint32_t block_start = data_offset + (b * block_align);
            if (block_start + block_align > res_size) break;

            // Extract the block header (Initial State)
            pred_sample = (int16_t)(raw_buffer[block_start] | (raw_buffer[block_start + 1] << 8));
            step_idx = raw_buffer[block_start + 2];
            if (step_idx > 88) step_idx = 88;

            audio_samples[sample_idx++] = pred_sample;

            // Extract and inflate the 4-bit nibbles
            for (uint32_t n = 4; n < block_align && sample_idx < num_samples; n++) {
                uint8_t byte = raw_buffer[block_start + n];
                audio_samples[sample_idx++] = decode_ima_adpcm_nibble(byte & 0x0F, &pred_sample, &step_idx);
                if (sample_idx < num_samples) {
                    audio_samples[sample_idx++] = decode_ima_adpcm_nibble((byte >> 4) & 0x0F, &pred_sample, &step_idx);
                }
            }
        }
    } else {
        // Standard PCM copy
        for (size_t i = 0; i < num_samples; i++) {
            audio_samples[i] = (int16_t)(raw_buffer[data_offset + (i * 2)] | (raw_buffer[data_offset + (i * 2) + 1] << 8));
        }
    }

    // POST-PROCESSING: Fade & Volume
    uint32_t play_volume = get_current_active_volume();
    size_t fade_samples = 1024;
    if (num_samples < fade_samples * 2) fade_samples = num_samples / 2;

    for (size_t i = 0; i < num_samples; i++) {
        int32_t sample = (int32_t)audio_samples[i];
        if (play_volume < 100) sample = (sample * (int32_t)play_volume) / 100;

        if (i < fade_samples) sample = (sample * (int32_t)i) / (int32_t)fade_samples;
        else if (i > num_samples - fade_samples) {
            int32_t fade_out_idx = num_samples - i;
            sample = (sample * fade_out_idx) / (int32_t)fade_samples;
        }
        audio_samples[i] = (int16_t)sample;
    }

    if (num_samples > 0) audio_samples[num_samples - 1] = 0;
    free(raw_buffer);
    s_current_res_size = num_samples * 2;
    s_stream_offset = 0;

    uint32_t duration_ms = num_samples / 16;

    if (s_shutdown_timer) app_timer_cancel(s_shutdown_timer);
    if (!s_is_stream_open) {
        if (speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, 100)) {
            s_is_stream_open = true;
            push_audio_chunk(NULL);
        } else return 0;
    }
    s_shutdown_timer = app_timer_register(duration_ms + 1500, close_stream_callback, NULL);
    return duration_ms;
}
