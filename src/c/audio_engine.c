/* =====================================================================================
 * [ AI CONTEXT DIRECTIVE - DO NOT REMOVE OR ALTER THESE RULES ]
 * =====================================================================================
 * 1. BACKLIGHT SUPPRESSION: Pushing 16-bit audio draws massive current. To prevent
 * hardware brownouts/crashes, `light_enable(false);` MUST be called before playback
 * begins and inside the play_next_word loop to keep the screen dark.
 * 2. LATENCY PRE-FIRE (30ms): The Pebble has a ~30ms file-loading latency. The playback
 * timer MUST always subtract `latency_compensation` (30ms) so the watch begins
 * loading the next audio buffer before the current one finishes.
 * 3. PLOSIVE BREATHING: The "PM" audio clip requires a split-second of silence before
 * it so the 'P' sound (a plosive) doesn't get swallowed by the preceding vowel.
 * AM/PM transitions must always use a POSITIVE gap (e.g., +25ms) against the speed.
 * 4. PHONETIC GLIDES (To/Till/After): Prepositions ending in soft sounds require
 * aggressive negative delays to fuse smoothly into the hour number.
 * 5. TELECOM DAC SPIN-UP: The `TELECOM_BEEP_CORRECTION_MS` (250ms) must be preserved.
 * It accounts for the physical time it takes the DAC to wake up from sleep before
 * playing the exact top-of-the-hour beep. The beep.wav file has 250ms of silence.
 * 6. BEEP VOLUME SPOOF: Pure sine waves lack the physical energy to push the speaker
 * membrane at low volumes. The engine temporarily spoofs `s_settings.volume` to a
 * minimum of 60% right before playing the beep, and restores it via timer afterward.
 * 7. AMPLIFIER POP PREVENTION: NEVER call `speaker_cancel()` immediately before
 * playing the telecom beep. It will power down the physical amplifier, causing an
 * audible "click" and swallowing the actual beep.wav file while the DAC spins back up.
 * 8. VISUALIZER SYNC: The audio engine explicitly calls `start_visualizer_for_clip()`
 * passing the exact duration of the audio clip to keep the UI perfectly synced.
 * =====================================================================================
 */

#include <pebble.h>
#include "audio_engine.h"
#include "settings_engine.h"
#include "speaker_engine.h"

#define TELECOM_BEEP_CORRECTION_MS 250

extern void hide_speaking_graphic(void);
extern void start_visualizer_for_clip(uint32_t duration_ms);
extern void stop_visualizer(void);

AudioQueueItem s_audio_playlist[20];
int s_playlist_size = 0;

static int s_current_word_index = 0;
static AppTimer *s_queue_timer = NULL;
static AppTimer *s_beep_timer = NULL;
static bool s_auto_exit = false;

static time_t s_telecom_target_sec = 0;

extern WhisperSettings s_settings;

static void queue_audio_ext(const char* filename, const char* display_text, int16_t delay_mod, int16_t trim_mod) {
  if (s_playlist_size < 20) {
    // SECURITY FIX: Guaranteed null-termination
    strncpy(s_audio_playlist[s_playlist_size].filename, filename, 15);
    s_audio_playlist[s_playlist_size].filename[15] = '\0';

    strncpy(s_audio_playlist[s_playlist_size].display_text, display_text, 15);
    s_audio_playlist[s_playlist_size].display_text[15] = '\0';

    s_audio_playlist[s_playlist_size].delay_mod = delay_mod;
    s_audio_playlist[s_playlist_size].trim_mod = trim_mod;
    s_playlist_size++;
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Audio queue full! Dropped file: %s", filename);
  }
}

// Trims and delays zeroed out for tighter custom voice clips
static void queue_audio(const char* filename, const char* display_text) {
  queue_audio_ext(filename, display_text, 0, 0);
}

static void queue_number_ext(int number, int16_t delay_mod, int16_t trim_mod) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", number);
  char file_buf[16];
  snprintf(file_buf, sizeof(file_buf), "%d.wav", number);
  queue_audio_ext(file_buf, buf, delay_mod, trim_mod);
}

static void queue_complex_number(int num, int16_t delay, int16_t trim) {
  if (num < 20) {
    queue_number_ext(num, delay, trim);
  } else {
    int tens = (num / 10) * 10;
    int ones = num % 10;
    if (ones > 0) {
      queue_number_ext(tens, -s_settings.playback_speed, 0);
      queue_number_ext(ones, delay, trim);
    } else {
      queue_number_ext(tens, delay, trim);
    }
  }
}

static void append_prefix(uint8_t prefix) {
  if (prefix == PREFIX_ITS) {
    queue_audio_ext("its.wav", "It's", s_settings.prefix_gap, 0);
  } else if (prefix == PREFIX_THE_TIME_IS) {
    queue_audio_ext("the-time-is.wav", "The time is", s_settings.prefix_gap, 0); // Trim forced to 0
  }
}

static void build_12h_digital(uint8_t h, uint8_t m, bool use_ampm) {
  int16_t std_delay = 0;

  // Plosive glide lock: Always attaches to the previous word regardless of global speed
  int16_t ampm_delay = -s_settings.playback_speed + 25;
  int16_t ampm_trim = 0;

  if (m == 0 && h == 0) {
    queue_audio_ext("midnight.wav", "Midnight", 0, 0);
    return;
  }
  if (m == 0 && h == 12) {
    queue_audio_ext("noon.wav", "Noon", 0, 0);
    return;
  }

  uint8_t display_h = h % 12;
  if (display_h == 0) display_h = 12;

  queue_number_ext(display_h, m == 0 ? std_delay : std_delay, 0);

  if (m == 0) {
    queue_audio_ext("oclock.wav", "O'clock", use_ampm ? ampm_delay : 0, ampm_trim);
  } else if (m < 10) {
    queue_audio_ext("oh.wav", "Oh", std_delay, 0);
    queue_number_ext(m, use_ampm ? ampm_delay : std_delay, use_ampm ? ampm_trim : 0);
  } else {
    queue_complex_number(m, use_ampm ? ampm_delay : std_delay, use_ampm ? ampm_trim : 0);
  }

  if (use_ampm) {
    queue_audio_ext(h < 12 ? "am.wav" : "pm.wav", h < 12 ? "AM" : "PM", 0, ampm_trim);
  }
}

static void build_24h_military(uint8_t h, uint8_t m, bool say_hours) {
  int16_t std_delay = 0;

  if (h < 10) {
    queue_audio_ext("zero.wav", "Zero", std_delay, 0);
    queue_number_ext(h, std_delay, 0);
  } else {
    queue_complex_number(h, std_delay, 0);
  }

  if (m == 0) {
    if (h != 0) queue_audio_ext("hundred.wav", "Hundred", say_hours ? std_delay : std_delay, 0);
  } else if (m < 10) {
    queue_audio_ext("zero.wav", "Zero", std_delay, 0);
    queue_number_ext(m, say_hours ? std_delay : std_delay, 0);
  } else {
    queue_complex_number(m, say_hours ? std_delay : std_delay, 0);
  }

  if (say_hours) {
    queue_audio_ext("hours.wav", "Hours", std_delay, 0);
  }
}

static void build_24h_civilian(uint8_t h, uint8_t m, bool say_hours) {
  int16_t std_delay = 0;

  if (m == 0 && h == 0) {
    queue_audio_ext("midnight.wav", "Midnight", 0, 0);
    return;
  }

  if (h < 10) {
    queue_audio_ext("oh.wav", "Oh", std_delay, 0);
    queue_number_ext(h, std_delay, 0);
  } else {
    queue_complex_number(h, std_delay, 0);
  }

  if (m == 0) {
    if (h != 0) queue_audio_ext("hundred.wav", "Hundred", say_hours ? std_delay : std_delay, 0);
  } else if (m < 10) {
    queue_audio_ext("oh.wav", "Oh", std_delay, 0);
    queue_number_ext(m, say_hours ? std_delay : std_delay, 0);
  } else {
    queue_complex_number(m, say_hours ? std_delay : std_delay, 0);
  }

  if (say_hours) {
    queue_audio_ext("hours.wav", "Hours", std_delay, 0);
  }
}

static void build_colloquial(uint8_t h, uint8_t m, bool is_us) {
  if (m == 0) {
    build_12h_digital(h, m, false);
    return;
  }

  bool is_past = (m <= 30);
  uint8_t rel_m = is_past ? m : (60 - m);
  uint8_t rel_h = is_past ? h : (h + 1);

  // Derive absolute bounds for conversational overrides
  int abs_h = rel_h % 24;
  bool is_midnight = (abs_h == 0 || abs_h == 24);
  bool is_noon = (abs_h == 12);

  uint8_t display_h = abs_h % 12;
  if (display_h == 0) display_h = 12;

  int16_t cd = -s_settings.playback_speed;
  int16_t ct = 0;

  int16_t past_d = -s_settings.playback_speed;
  int16_t past_t = 0;

  int16_t to_d = -s_settings.playback_speed;
  int16_t to_t = 0;

  if (rel_m == 15) {
    queue_audio_ext(is_us ? "a_quarter.wav" : "quarter.wav", is_us ? "A Quarter" : "Quarter", cd, ct);
  } else if (rel_m == 30) {
    queue_audio_ext("half.wav", "Half", cd, ct);
  } else {
    queue_complex_number(rel_m, cd, ct);
    if (rel_m == 1) queue_audio_ext("minute.wav", "Minute", cd, ct);
    else queue_audio_ext("minutes.wav", "Minutes", cd, ct);
  }

  if (rel_m == 30) {
    queue_audio_ext("past.wav", "Past", past_d, past_t);
  } else {
    if (is_us) {
      queue_audio_ext(is_past ? "after.wav" : "till.wav", is_past ? "After" : "Till", to_d, to_t);
    } else {
      queue_audio_ext(is_past ? "past.wav" : "to.wav", is_past ? "Past" : "To", is_past ? past_d : to_d, is_past ? past_t : to_t);
    }
  }

  if (is_midnight) {
    queue_audio_ext("midnight.wav", "Midnight", 0, 0);
  } else if (is_noon) {
    queue_audio_ext("noon.wav", "Noon", 0, 0);
  } else {
    queue_number_ext(display_h, cd, ct);
  }
}

static void build_fuzzy(uint8_t h, uint8_t m, bool is_us, bool use_ampm) {
  int anchor = ((m + 7) / 15) * 15;
  int diff = m - anchor;

  int16_t mod_delay = (anchor == 0 || anchor == 60) ? (-s_settings.playback_speed) : s_settings.fuzzy_mod_gap;
  int16_t mod_trim = 0;

  int16_t conv_delay = s_settings.fuzzy_conv_gap;
  int16_t conv_trim = 0;

  int16_t past_delay = s_settings.fuzzy_past_gap;
  int16_t past_trim = 0;

  int16_t to_delay = s_settings.fuzzy_to_gap;
  int16_t to_trim = 0;

  int16_t tight_delay = s_settings.fuzzy_tight_gap;
  int16_t tight_trim = 0;

  int16_t ampm_delay = s_settings.fuzzy_ampm_gap;
  int16_t ampm_trim = 0;

  if (diff < -1) {
    queue_audio_ext("almost.wav", "Almost", mod_delay, mod_trim);
  } else if (diff > 1) {
    queue_audio_ext("just_after.wav", "Just After", mod_delay, mod_trim);
  }

  int abs_h = (h + ((anchor == 60 || anchor == 45) ? 1 : 0)) % 24;
  uint8_t target_h = abs_h % 12;
  if (target_h == 0) target_h = 12;

  bool is_midnight = (abs_h == 0 || abs_h == 24);
  bool is_noon = (abs_h == 12);

  if (anchor == 0 || anchor == 60) {
    // Zero anchors have no prefix
  } else if (anchor == 15) {
    queue_audio_ext(is_us ? "a_quarter.wav" : "quarter.wav", is_us ? "A Quarter" : "Quarter", conv_delay, conv_trim);
    queue_audio_ext(is_us ? "after.wav" : "past.wav", is_us ? "After" : "Past", is_us ? to_delay : past_delay, is_us ? to_trim : past_trim);
  } else if (anchor == 30) {
    queue_audio_ext("half.wav", "Half", conv_delay, conv_trim);
    queue_audio_ext("past.wav", "Past", past_delay, past_trim);
  } else if (anchor == 45) {
    queue_audio_ext(is_us ? "a_quarter.wav" : "quarter.wav", is_us ? "A Quarter" : "Quarter", conv_delay, conv_trim);
    queue_audio_ext(is_us ? "till.wav" : "to.wav", is_us ? "Till" : "To", to_delay, to_trim);
  }

  if (is_midnight) {
    queue_audio_ext("midnight.wav", "Midnight", 0, conv_trim);
  } else if (is_noon) {
    queue_audio_ext("noon.wav", "Noon", 0, conv_trim);
  } else {
    if (anchor == 0 || anchor == 60) {
      queue_number_ext(target_h, tight_delay, tight_trim);
      queue_audio_ext("oclock.wav", "O'clock", use_ampm ? ampm_delay : 0, tight_trim);
    } else {
      queue_number_ext(target_h, use_ampm ? ampm_delay : 0, ampm_trim);
    }

    if (use_ampm) {
      queue_audio_ext(abs_h < 12 ? "am.wav" : "pm.wav", abs_h < 12 ? "AM" : "PM", 0, ampm_trim);
    }
  }
}

// -------------------------------------------------------------------------
// TELECOM DYNAMIC ENGINE
// -------------------------------------------------------------------------

static uint32_t get_playlist_duration_estimate() {
  int32_t total_ms = 0;
  for (int i = 0; i < s_playlist_size; i++) {
    int32_t word_ms = 600;

    if (strcmp(s_audio_playlist[i].filename, "at_the_tone.wav") == 0) word_ms = 900;
    else if (strcmp(s_audio_playlist[i].filename, "precisely.wav") == 0) word_ms = 850;
    else if (strcmp(s_audio_playlist[i].filename, "seconds.wav") == 0 || strcmp(s_audio_playlist[i].filename, "second.wav") == 0) word_ms = 850;
    else if (strcmp(s_audio_playlist[i].filename, "minutes.wav") == 0 || strcmp(s_audio_playlist[i].filename, "minute.wav") == 0) word_ms = 800;
    else if (strcmp(s_audio_playlist[i].filename, "hours.wav") == 0 || strcmp(s_audio_playlist[i].filename, "hour.wav") == 0) word_ms = 800;
    else if (strcmp(s_audio_playlist[i].filename, "and.wav") == 0) word_ms = 400;

    word_ms -= s_settings.clip_trim;
    word_ms -= s_audio_playlist[i].trim_mod;
    if (word_ms < 100) word_ms = 100;

    total_ms += word_ms;
    total_ms += s_settings.playback_speed;
    total_ms += s_audio_playlist[i].delay_mod;
  }
  return (uint32_t)total_ms;
}

static void internal_build_telecom(struct tm *t) {
  queue_audio("at_the_tone.wav", "At the tone");

  // Changed to force a 250ms gap before reading the hour value
  queue_complex_number(t->tm_hour, 250, 0);
  queue_audio_ext((t->tm_hour == 1) ? "hour.wav" : "Hour", (t->tm_hour == 1) ? "Hour" : "Hours", 0, 0);

  // DYNAMIC PRECISE LOGIC:
  if (t->tm_sec == 0) {
    if (t->tm_min > 0) {
      queue_complex_number(t->tm_min, 0, 0);
      queue_audio_ext((t->tm_min == 1) ? "minute.wav" : "minutes.wav", (t->tm_min == 1) ? "Minute" : "Minutes", 0, 0);
    }
    queue_audio("precisely.wav", "Precisely");
  } else {
    queue_complex_number(t->tm_min, 0, 0);
    queue_audio_ext((t->tm_min == 1) ? "minute.wav" : "minutes.wav", (t->tm_min == 1) ? "Minute" : "Minutes", 0, 0);

    queue_audio("and.wav", "And");

    queue_complex_number(t->tm_sec, 0, 0);
    queue_audio_ext((t->tm_sec == 1) ? "second.wav" : "seconds.wav", (t->tm_sec == 1) ? "Second" : "Seconds", 0, 0);
  }
}

static void build_telecom_radio() {
  time_t now_sec;
  uint16_t now_ms;
  time_ms(&now_sec, &now_ms);

  time_t dummy_target = now_sec + 8;
  struct tm *t_dummy = localtime(&dummy_target);
  internal_build_telecom(t_dummy);

  uint32_t phrase_duration_ms = get_playlist_duration_estimate();
  s_playlist_size = 0;

  uint32_t total_projected_ms = now_ms + phrase_duration_ms + 800;
  uint32_t seconds_to_add = (total_projected_ms / 1000) + 1;

  s_telecom_target_sec = now_sec + seconds_to_add;
  struct tm *t_target = localtime(&s_telecom_target_sec);

  internal_build_telecom(t_target);
}

// --- Main Engine ---

void generate_audio_playlist() {
  s_playlist_size = 0;
  s_telecom_target_sec = 0;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  uint8_t hour = t->tm_hour;
  uint8_t min = t->tm_min;

  if (s_settings.clock_mode == MODE_TELECOM) {
    build_telecom_radio();
    return;
  }

  uint8_t effective_mode = s_settings.clock_mode;

  if (effective_mode == MODE_SYSTEM_DEFAULT) {
    effective_mode = clock_is_24h_style() ? MODE_24H_CIVILIAN : MODE_12H_DIGITAL;
  }

  if (effective_mode == MODE_COLLOQUIAL || effective_mode == MODE_FUZZY) {
    append_prefix(PREFIX_ITS);
  } else {
    append_prefix(s_settings.prefix_mode);
  }

  switch(effective_mode) {
    case MODE_12H_DIGITAL:
      build_12h_digital(hour, min, s_settings.say_ampm); break;
    case MODE_24H_MILITARY:
      build_24h_military(hour, min, s_settings.say_ampm); break;
    case MODE_24H_CIVILIAN:
      build_24h_civilian(hour, min, s_settings.say_ampm); break;
    case MODE_COLLOQUIAL:
      build_colloquial(hour, min, s_settings.is_us_dialect); break;
    case MODE_FUZZY:
      build_fuzzy(hour, min, s_settings.is_us_dialect, s_settings.say_ampm); break;
    default:
      build_12h_digital(hour, min, s_settings.say_ampm); break;
  }
}

// --- Playback Handlers ---

void cancel_playback(void) {
  s_telecom_target_sec = 0;
  if (s_queue_timer) {
    app_timer_cancel(s_queue_timer);
    s_queue_timer = NULL;
  }
  if (s_beep_timer) {
    app_timer_cancel(s_beep_timer);
    s_beep_timer = NULL;
  }
  speaker_cancel();
  stop_visualizer(); // Instantly kill UI wave animation
  light_enable(true);
}

static void exit_app_callback(void *data) {
  s_queue_timer = NULL;
  s_beep_timer = NULL;
  window_stack_pop_all(true);
}

static void hide_graphic_callback(void *data) {
  s_queue_timer = NULL;
  s_beep_timer = NULL;
  hide_speaking_graphic();
}

static uint8_t s_original_volume = 0;
static uint8_t s_original_night_volume = 0;

static void restore_volume_callback(void *data) {
  s_settings.volume = s_original_volume;
  s_settings.night_volume = s_original_night_volume;
}

static void telecom_beep_callback(void *data) {
  s_beep_timer = NULL;
  if (s_queue_timer) {
    app_timer_cancel(s_queue_timer);
    s_queue_timer = NULL;
  }

  light_enable(false);

  // TEMPORARY VOLUME SPOOF: Ensure the beep punches through at low volumes
  s_original_volume = s_settings.volume;
  s_original_night_volume = s_settings.night_volume;

  if (s_settings.volume < 60) {
    s_settings.volume = 60;
  }
  if (s_settings.night_volume < 60) {
    s_settings.night_volume = 60;
  }

  uint32_t clip_duration_ms = speaker_play_file("beep.wav", 0);
  start_visualizer_for_clip(clip_duration_ms);

  // Silently revert the volume back to the user's setting shortly after playback
  app_timer_register(clip_duration_ms + 100, restore_volume_callback, NULL);

  if (s_auto_exit) {
    s_queue_timer = app_timer_register(clip_duration_ms + 1000, exit_app_callback, NULL);
  } else {
    s_queue_timer = app_timer_register(clip_duration_ms + 1000, hide_graphic_callback, NULL);
  }
}

static void play_next_word(void *data) {
  s_queue_timer = NULL;

  light_enable(false);

  if (s_current_word_index >= s_playlist_size) {
    if (s_settings.clock_mode == MODE_TELECOM && s_telecom_target_sec > 0) {
      return;
    }

    if (s_auto_exit) window_stack_pop_all(true);
    else hide_speaking_graphic();
    return;
  }

  const char* next_file = s_audio_playlist[s_current_word_index].filename;
  int16_t trim_mod = s_audio_playlist[s_current_word_index].trim_mod;
  int16_t delay_mod = s_audio_playlist[s_current_word_index].delay_mod;

  uint32_t clip_duration_ms = speaker_play_file(next_file, trim_mod);
  start_visualizer_for_clip(clip_duration_ms); // Trigger perfectly synced waveform bounce

  s_current_word_index++;

  if (s_current_word_index < s_playlist_size) {

    // REDUCED FROM 75 to 30: Prevents file truncation when words are pushed back-to-back
    int32_t latency_compensation = 30;

    int32_t time_to_wait = (int32_t)clip_duration_ms + (int32_t)s_settings.playback_speed + (int32_t)delay_mod - latency_compensation;
    if (time_to_wait < 1) time_to_wait = 1;

    s_queue_timer = app_timer_register((uint32_t)time_to_wait, play_next_word, NULL);
  } else {
    s_queue_timer = app_timer_register(clip_duration_ms + 1000, play_next_word, NULL);
  }
}

void trigger_playback(bool auto_exit) {
  s_auto_exit = auto_exit;

  light_enable(false);

  if (s_settings.clock_mode != MODE_TELECOM) {
    cancel_playback();
  }

  // Explicitly reset the playlist size so the buffer properly clears
  s_playlist_size = 0;
  generate_audio_playlist();

  if (s_settings.clock_mode == MODE_TELECOM && s_telecom_target_sec > 0) {
    time_t now_sec;
    uint16_t now_ms;
    time_ms(&now_sec, &now_ms);

    int32_t beep_wait = (s_telecom_target_sec - now_sec) * 1000 - now_ms + TELECOM_BEEP_CORRECTION_MS;
    if (beep_wait > 0) {
      s_beep_timer = app_timer_register(beep_wait, telecom_beep_callback, NULL);
    } else {
      telecom_beep_callback(NULL);
      return;
    }
  }

  s_current_word_index = 0;
  if (s_playlist_size > 0) {
    s_queue_timer = app_timer_register(1, play_next_word, NULL);
  }
}
