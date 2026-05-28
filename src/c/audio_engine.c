/*
 * WhisperClock
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 *
 * AI Disclosure: Portions of this file, including system architecture, 
 * audio upsampling algorithms, and preprocessor UI toggles, were 
 * generated and optimized with the assistance of generative AI 
 * (Google Gemini).
 */

#include <pebble.h>
#include "audio_engine.h"
#include "settings_engine.h"
#include "speaker_engine.h"

extern void hide_speaking_graphic(void);

AudioQueueItem s_audio_playlist[10];
int s_playlist_size = 0;

static int s_current_word_index = 0;
static AppTimer *s_queue_timer = NULL;
static bool s_auto_exit = false;

extern WhisperSettings s_settings;

static void queue_audio(const char* filename, const char* display_text) {
  if (s_playlist_size < 10) {
    strncpy(s_audio_playlist[s_playlist_size].filename, filename, 16);
    strncpy(s_audio_playlist[s_playlist_size].display_text, display_text, 16);
    s_playlist_size++;
  }
}

static void queue_number(int number) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", number);
    
    char file_buf[16];
    snprintf(file_buf, sizeof(file_buf), "%d.wav", number);
    
    queue_audio(file_buf, buf);
}

void generate_audio_playlist() {
  s_playlist_size = 0;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int hour = t->tm_hour;
  int min = t->tm_min;

  bool is_military;
  if (s_settings.clock_mode == 1) is_military = false; 
  else if (s_settings.clock_mode == 2) is_military = true; 
  else is_military = clock_is_24h_style();

  if (s_settings.say_its) {
      queue_audio("its.wav", "It's");
  }

  int display_hour = hour;
  if (!is_military) {
      display_hour = hour % 12;
      if (display_hour == 0) display_hour = 12;
  }
  
  if (is_military && hour == 0) {
      queue_audio("zero.wav", "Zero");
      queue_audio("hundred.wav", "Hundred");
  } else if (is_military && hour < 10) {
      queue_audio("zero.wav", "Zero");
      queue_number(hour);
  } else {
      queue_number(display_hour);
  }

  if (min == 0) {
      if (is_military) queue_audio("hundred.wav", "Hundred");
      else queue_audio("oclock.wav", "O'clock");
  } else if (min < 10) {
      queue_audio("oh.wav", "Oh");
      queue_number(min);
  } else if (min >= 10 && min <= 19) {
      queue_number(min);
  } else {
      int tens = (min / 10) * 10;
      int ones = min % 10;
      queue_number(tens);
      if (ones > 0) queue_number(ones);
  }

  if (!is_military && s_settings.say_ampm) {
      if (hour < 12) queue_audio("am.wav", "AM");
      else queue_audio("pm.wav", "PM");
  } else if (is_military && s_settings.say_ampm) {
      queue_audio("hours.wav", "Hours");
  }
}

void cancel_playback(void) {
  if (s_queue_timer) {
    app_timer_cancel(s_queue_timer);
    s_queue_timer = NULL;
  }
  speaker_cancel();
}

static void exit_app_callback(void *data) {
  s_queue_timer = NULL; 
  window_stack_pop_all(true);
}

static void hide_graphic_callback(void *data) {
  s_queue_timer = NULL; 
  hide_speaking_graphic();
}

static void play_next_word(void *data) {
  s_queue_timer = NULL; 

  if (s_current_word_index >= s_playlist_size) {
    if (s_auto_exit) window_stack_pop_all(true);
    return;
  }

  const char* next_file = s_audio_playlist[s_current_word_index].filename;
  uint32_t clip_duration_ms = speaker_play_file(next_file);

  s_current_word_index++;

  if (s_current_word_index < s_playlist_size) {
    uint32_t time_to_wait = clip_duration_ms + s_settings.playback_speed;
    s_queue_timer = app_timer_register(time_to_wait, play_next_word, NULL);
  } else {
    // 🟢 THE TRUE END-POP FIX: Wait 2500ms before killing the app! 
    // The speaker_engine takes 500ms to push padding + 1500ms to drain. 
    // If the app exits before that 2000ms finishes, the OS causes a pop.
    if (s_auto_exit) {
      s_queue_timer = app_timer_register(clip_duration_ms + 2500, exit_app_callback, NULL);
    } else {
      s_queue_timer = app_timer_register(clip_duration_ms + 2500, hide_graphic_callback, NULL);
    }
  }
}

void trigger_playback(bool auto_exit) {
  s_auto_exit = auto_exit;
  generate_audio_playlist();
  s_current_word_index = 0;
  
  cancel_playback();
  play_next_word(NULL);
}
