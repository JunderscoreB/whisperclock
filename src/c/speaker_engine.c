/*
 * WhisperClock - Speaker Engine Implementation
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include "speaker_engine.h"
#include "settings_engine.h"

#define AUDIO_CHUNK_SIZE 512
#define CHUNK_DELAY_MS 20

// NOTE: s_settings is declared in settings_engine.c.
// We must declare it 'extern' here so this file knows where it lives.
extern WhisperSettings s_settings;

void speaker_init(void) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Native 16-Bit Audio Engine Initialized.");
}

static uint32_t get_resource_id_for_filename(const char* filename) {
    char first = filename[0];
    if (first == 'i' && strcmp(filename, "its.wav") == 0) return RESOURCE_ID_its;
    if (first == 'n' && strcmp(filename, "noon.wav") == 0) return RESOURCE_ID_noon;
    if (first == 'm' && strcmp(filename, "midnight.wav") == 0) return RESOURCE_ID_midnight;
    if (first == 'a' && strcmp(filename, "am.wav") == 0) return RESOURCE_ID_am;
    if (first == 'p' && strcmp(filename, "pm.wav") == 0) return RESOURCE_ID_pm;
    if (first == 'o') {
        if (strcmp(filename, "oclock.wav") == 0) return RESOURCE_ID_oclock;
        if (strcmp(filename, "oh.wav") == 0) return RESOURCE_ID_oh;
    }
    if (first == 'h') {
        if (strcmp(filename, "hours.wav") == 0) return RESOURCE_ID_hours;
        if (strcmp(filename, "hundred.wav") == 0) return RESOURCE_ID_hundred;
    }
    if (first == 't' && strcmp(filename, "the-time-is.wav") == 0) return RESOURCE_ID_the_time_is;

    if (first == 'z' && strcmp(filename, "zero.wav") == 0) return RESOURCE_ID_0;
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
    uint32_t data_size = res_size > 44 ? res_size - 44 : 0;
    for (uint32_t i = 12; i < res_size - 8; ) {
        if (raw_buffer[i] == 'd' && raw_buffer[i+1] == 'a' && raw_buffer[i+2] == 't' && raw_buffer[i+3] == 'a') {
            data_size = raw_buffer[i+4] | (raw_buffer[i+5] << 8) | (raw_buffer[i+6] << 16) | (raw_buffer[i+7] << 24);
            data_offset = i + 8;
            break;
        }
        uint32_t chunk_size = raw_buffer[i+4] | (raw_buffer[i+5] << 8) | (raw_buffer[i+6] << 16) | (raw_buffer[i+7] << 24);
        i += 8 + chunk_size;
    }
    size_t num_samples = data_size / 2;
    int32_t total_trim_ms = (int32_t)s_settings.clip_trim + (int32_t)extra_trim_ms;
    if (total_trim_ms < 0) total_trim_ms = 0;
    size_t trim_samples = total_trim_ms * 16;
    if (num_samples > trim_samples + 256) num_samples -= trim_samples;
    s_audio_buffer = (uint8_t *)malloc(num_samples * 2);
    int16_t *audio_samples = (int16_t *)s_audio_buffer;
    uint32_t play_volume = s_settings.volume;
    size_t fade_samples = 1024;
    if (num_samples < fade_samples * 2) fade_samples = num_samples / 2;
    for (size_t i = 0; i < num_samples; i++) {
        int16_t raw_sample = (int16_t)(raw_buffer[data_offset + (i * 2)] | (raw_buffer[data_offset + (i * 2) + 1] << 8));
        int32_t sample = (int32_t)raw_sample;
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
