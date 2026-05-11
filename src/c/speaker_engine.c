#include "speaker_engine.h"
#include "settings_engine.h" 

#define AUDIO_CHUNK_SIZE 512
#define CHUNK_DELAY_MS 20 

extern WhisperSettings s_settings; 

void speaker_init(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Speaker Engine Initialized.");
}

static size_t find_wav_data_offset_and_size(const uint8_t *buffer, size_t file_size, size_t *out_data_size) {
    if (file_size < 44) {
        *out_data_size = file_size - 44;
        return 44; 
    }
    size_t offset = 12; 
    while (offset + 8 <= file_size) {
        uint32_t chunk_size = buffer[offset+4] | (buffer[offset+5] << 8) | 
                              (buffer[offset+6] << 16) | (buffer[offset+7] << 24);

        if (buffer[offset] == 'd' && buffer[offset+1] == 'a' && 
            buffer[offset+2] == 't' && buffer[offset+3] == 'a') {
            *out_data_size = chunk_size; 
            return offset + 8; 
        }
        offset += 8 + chunk_size; 
    }
    *out_data_size = file_size - 44;
    return 44; 
}

static uint32_t get_resource_id_for_filename(const char* filename) {
  if (strcmp(filename, "its.wav") == 0) return RESOURCE_ID_its;
  if (strcmp(filename, "noon.wav") == 0) return RESOURCE_ID_noon;
  if (strcmp(filename, "am.wav") == 0) return RESOURCE_ID_am;
  if (strcmp(filename, "pm.wav") == 0) return RESOURCE_ID_pm;
  if (strcmp(filename, "oclock.wav") == 0) return RESOURCE_ID_oclock;
  if (strcmp(filename, "hours.wav") == 0) return RESOURCE_ID_hours;
  if (strcmp(filename, "hundred.wav") == 0) return RESOURCE_ID_hundred;
  if (strcmp(filename, "oh.wav") == 0) return RESOURCE_ID_oh;
  if (strcmp(filename, "zero.wav") == 0 || strcmp(filename, "0.wav") == 0) return RESOURCE_ID_0;
  if (strcmp(filename, "1.wav") == 0) return RESOURCE_ID_1;
  if (strcmp(filename, "2.wav") == 0) return RESOURCE_ID_2;
  if (strcmp(filename, "3.wav") == 0) return RESOURCE_ID_3;
  if (strcmp(filename, "4.wav") == 0) return RESOURCE_ID_4;
  if (strcmp(filename, "5.wav") == 0) return RESOURCE_ID_5;
  if (strcmp(filename, "6.wav") == 0) return RESOURCE_ID_6;
  if (strcmp(filename, "7.wav") == 0) return RESOURCE_ID_7;
  if (strcmp(filename, "8.wav") == 0) return RESOURCE_ID_8;
  if (strcmp(filename, "9.wav") == 0) return RESOURCE_ID_9;
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
  if (strcmp(filename, "20.wav") == 0) return RESOURCE_ID_20;
  if (strcmp(filename, "30.wav") == 0) return RESOURCE_ID_30;
  if (strcmp(filename, "40.wav") == 0) return RESOURCE_ID_40;
  if (strcmp(filename, "50.wav") == 0) return RESOURCE_ID_50;
  return 0; 
}

static uint8_t *s_audio_buffer = NULL;
static bool s_is_stream_open = false;
static size_t s_current_res_size = 0;
static size_t s_stream_offset = 0;
static AppTimer *s_chunk_timer = NULL;
static bool s_amp_primed = false;

// NEW: Instantly halts all audio processing
void speaker_cancel(void) {
  if (s_chunk_timer) {
    app_timer_cancel(s_chunk_timer);
    s_chunk_timer = NULL;
  }
  if (s_is_stream_open) {
    speaker_stop();
    speaker_stream_close();
    s_is_stream_open = false;
  }
}

static void push_audio_chunk(void *data) {
    if (!s_is_stream_open || s_audio_buffer == NULL) return;
    bool hardware_buffer_full = false;
    while (s_stream_offset < s_current_res_size && !hardware_buffer_full) {
        size_t remaining = s_current_res_size - s_stream_offset;
        size_t chunk = remaining > AUDIO_CHUNK_SIZE ? AUDIO_CHUNK_SIZE : remaining;
        uint32_t bytes_written = speaker_stream_write(s_audio_buffer + s_stream_offset, chunk);
        s_stream_offset += bytes_written;
        if (bytes_written < chunk) hardware_buffer_full = true;
    }
    if (s_stream_offset < s_current_res_size) {
        s_chunk_timer = app_timer_register(CHUNK_DELAY_MS, push_audio_chunk, NULL);
    } else {
        s_chunk_timer = NULL; 
    }
}

uint32_t speaker_play_file(const char* filename) {
  uint32_t res_id = get_resource_id_for_filename(filename);
  if (res_id != 0) {
    APP_LOG(APP_LOG_LEVEL_INFO, "PLAYING: %s", filename);
    
    // Ensure any previously playing sound is stopped safely
    speaker_cancel();

    ResHandle res_handle = resource_get_handle(res_id);
    if (!res_handle) return 0;
    size_t res_size = resource_size(res_handle);

    if (s_audio_buffer != NULL) {
        free(s_audio_buffer);
        s_audio_buffer = NULL;
    }

    s_audio_buffer = (uint8_t *)malloc(res_size);
    if (s_audio_buffer == NULL) return 0;
    resource_load(res_handle, s_audio_buffer, res_size);

    size_t actual_data_length = 0;
    s_stream_offset = find_wav_data_offset_and_size(s_audio_buffer, res_size, &actual_data_length); 

    size_t trim_bytes = (s_settings.clip_trim * 16);
    if (actual_data_length > trim_bytes) actual_data_length -= trim_bytes; 
    else actual_data_length = 0; 

    if (s_stream_offset + actual_data_length > res_size) {
        actual_data_length = res_size - s_stream_offset;
    }

    s_current_res_size = s_stream_offset + actual_data_length;

    for (size_t i = s_stream_offset; i < s_current_res_size; i++) {
        s_audio_buffer[i] -= 128;
    }

    size_t fade_samples = 64;
    if (actual_data_length > fade_samples * 2) {
        for (size_t i = 0; i < fade_samples; i++) {
            int8_t *sample_in = (int8_t *)&s_audio_buffer[s_stream_offset + i];
            *sample_in = (int8_t)((*sample_in * (int)i) / fade_samples);

            int8_t *sample_out = (int8_t *)&s_audio_buffer[s_current_res_size - fade_samples + i];
            *sample_out = (int8_t)((*sample_out * (int)(fade_samples - i)) / fade_samples);
        }
    }

    uint8_t play_volume = (s_settings.volume >= 10 && s_settings.volume <= 100) ? s_settings.volume : 60;

    if (!s_amp_primed) {
        if (speaker_stream_open(SpeakerPcmFormat_16kHz_8bit, play_volume)) {
            uint8_t silence_frame[32] = {0}; 
            speaker_stream_write(silence_frame, sizeof(silence_frame));
            speaker_stop();
            speaker_stream_close();
            s_amp_primed = true;
        }
    }

    if (speaker_stream_open(SpeakerPcmFormat_16kHz_8bit, play_volume)) {
        s_is_stream_open = true;
        push_audio_chunk(NULL); 
        return actual_data_length / 16;
    }
  } 
  return 0; 
}
