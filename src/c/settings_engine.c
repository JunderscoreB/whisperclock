/* =====================================================================================
 * [ AI CONTEXT DIRECTIVE - DO NOT REMOVE OR ALTER THESE RULES ]
 * =====================================================================================
 * 1. HOLD-TO-SCROLL: The main menu and tuner menu DO NOT use Pebble's default
 * SimpleMenuLayer callbacks. They use a custom `MenuLayer` with
 * `window_single_repeating_click_subscribe` (100ms intervals) so the user can HOLD
 * the Up/Down buttons to rapidly scroll numeric values.
 * 2. UINT8_T UNDERFLOW PROTECTION: The `s_settings.volume` variable is an unsigned
 * 8-bit integer. When subtracting 5 from volume, the code MUST explicitly check
 * `if (volume <= 5) volume = 1;` to prevent the integer from wrapping backwards
 * around 0 and landing on 252%.
 * 3. KINETIC TOUCH PHYSICS: For PBL_TOUCH (Pebble 2), the menu scrolling uses a custom
 * velocity engine. `s_scroll_velocity` degrades by 10% (multiplied by 90/100) on a
 * 25ms `AppTimer` loop to simulate physical momentum after a swipe.
 * 4. TUNER BOUNDARIES: Calibration Tuner variables must allow ranges from -800ms to
 * +800ms to account for various source audio lengths. Trims must never go below 0ms.
 * 5. VISUALIZER STATE MACHINE: The UI exports `start_visualizer_for_clip()` and
 * `stop_visualizer()` to sync animations precisely with the audio engine's file
 * duration. When speaking, it hops between random active frames (1-3) every 100ms.
 * When silence is hit, it drops to frame 0 (flatline).
 * =====================================================================================
 */

#include <pebble.h>
#include "settings_engine.h"
#include <stdlib.h>
#include "gesture_engine.h"

#define GESTURE_PERSIST_KEY 2

// UI Touch Math Constraints (Dynamically scaled for Emery's higher resolution)
#ifdef PBL_EMERY
#define UI_ROW_HEIGHT 60
#define UI_HEADER_HEIGHT 22
#else
#define UI_ROW_HEIGHT 44
#define UI_HEADER_HEIGHT 16
#endif

WhisperSettings s_settings;

// --- MAIN MENU (CUSTOM MENULAYER) ---
static Window *s_menu_window = NULL;
static MenuLayer *s_main_menu_layer = NULL;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[30];
static int16_t s_main_edit_row = -1;

// --- FUZZY TUNER SUBMENU (CUSTOM MENULAYER) ---
static Window *s_tuner_window = NULL;
static MenuLayer *s_tuner_menu_layer = NULL;
static int16_t s_tuner_edit_row = -1;

static char s_volume_text[16];
static char s_night_volume_text[16];
static char s_speed_text[24];
static char s_trim_text[24];
static char s_quiet_start_text[24];
static char s_quiet_end_text[24];
static char s_sensitivity_text[16];
static char s_tap_sens_text[16];
static char s_x_mult_text[24];
static char s_y_mult_text[24];
static char s_z_mult_text[24];

static Window *s_about_window = NULL;
static ScrollLayer *s_about_scroll_layer = NULL;
static TextLayer *s_about_text_layer = NULL;

static Window *s_confirm_window = NULL;
static TextLayer *s_confirm_text_layer = NULL;

extern void trigger_playback(bool auto_exit);
extern void cancel_playback(void);
static void build_menu_items(void);

// Forward Declarations
void test_audio_callback(int index, void *context);
static void main_select_click_handler(ClickRecognizerRef recognizer, void *context);
static void show_about_callback(int index, void *context);
#ifdef PBL_TOUCH
static void kill_kinetic_timer(void);
static void tuner_touch_handler(const TouchEvent *event, void *context);
#endif

// --- UNIVERSAL VOLUME HELPER ---
uint8_t get_current_active_volume(void) {
  bool in_qt = false;
  if (s_settings.respect_quiet_time) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int hour = t->tm_hour;

    if (s_settings.quiet_start_hour < s_settings.quiet_end_hour) {
      in_qt = (hour >= s_settings.quiet_start_hour && hour < s_settings.quiet_end_hour);
    } else if (s_settings.quiet_start_hour > s_settings.quiet_end_hour) {
      in_qt = (hour >= s_settings.quiet_start_hour || hour < s_settings.quiet_end_hour);
    }
  }
  return in_qt ? s_settings.night_volume : s_settings.volume;
}


static void save_settings() {
  persist_write_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(WhisperSettings));
}

// SAFE MEMORY UPDATE: Decouples screen refreshes from flash memory writes
static void update_menu_ui_only() {
  build_menu_items();
  if (s_main_menu_layer) {
    menu_layer_reload_data(s_main_menu_layer);
  }
}

static void update_tuner_ui_only() {
  if (s_tuner_menu_layer) {
    menu_layer_reload_data(s_tuner_menu_layer);
  }
}

// Used for toggles and static state changes
static void update_menu_and_save() {
  save_settings();
  update_menu_ui_only();
}

static void update_tuner_and_save() {
  save_settings();
  update_tuner_ui_only();
}

void settings_init() {
  s_settings.prefix_mode = 2;
  s_settings.say_ampm = true;
  s_settings.is_us_dialect = true;
  s_settings.playback_speed = 0;
  s_settings.gesture_buffer_size = 50;
  s_settings.clock_mode = 6;
  s_settings.volume = 100;
  s_settings.clip_trim = 0;
  s_settings.enable_beta_features = false;

  // Night Mode Scheduling Defaults
  s_settings.respect_quiet_time = true;
  s_settings.quiet_start_hour = 22;
  s_settings.quiet_end_hour = 7;
  s_settings.night_volume = 10;
  s_settings.night_worker_sleep = true;

  // Independent Default Axes
  s_settings.gesture_mode = 0;
  s_settings.default_flick_sensitivity = 62;
  s_settings.tap_sensitivity = 15; // Default middle of the 0-30 scale
  s_settings.x_multiplier = 1000;
  s_settings.y_multiplier = 1000;
  s_settings.z_multiplier = 1000;

  s_settings.prefix_gap = 0;
  s_settings.prefix_trim = 0;
  s_settings.fuzzy_mod_gap = 10;
  s_settings.fuzzy_conv_gap = -30;
  s_settings.fuzzy_past_gap = -10;
  s_settings.fuzzy_to_gap = -60;
  s_settings.fuzzy_tight_gap = -20;
  s_settings.fuzzy_ampm_gap = 25;

  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    WhisperSettings temp_settings;
    int bytes_read = persist_read_data(SETTINGS_PERSIST_KEY, &temp_settings, sizeof(WhisperSettings));
    if (bytes_read == sizeof(WhisperSettings)) {
      s_settings = temp_settings;
    }
  }

  if (s_settings.prefix_mode > 2) s_settings.prefix_mode = 2;
  if (s_settings.clock_mode > 6) s_settings.clock_mode = 6;
  if (s_settings.volume > 100) s_settings.volume = 100;
  if (s_settings.playback_speed < 0 || s_settings.playback_speed > 350) s_settings.playback_speed = 0;
  if (s_settings.clip_trim < 0 || s_settings.clip_trim > 150) s_settings.clip_trim = 0;

  if (s_settings.quiet_start_hour > 23) s_settings.quiet_start_hour = 22;
  if (s_settings.quiet_end_hour > 23) s_settings.quiet_end_hour = 7;
  if (s_settings.night_volume > 100) s_settings.night_volume = 100;

  if (s_settings.gesture_mode > 2) s_settings.gesture_mode = 0;
  if (s_settings.default_flick_sensitivity < 55 || s_settings.default_flick_sensitivity > 70) s_settings.default_flick_sensitivity = 62;

  // Safely catch out-of-bounds tap settings (e.g. from the previous build's 55-70 bounds)
  if (s_settings.tap_sensitivity > 30) s_settings.tap_sensitivity = 15;

  if (s_settings.x_multiplier < 0 || s_settings.x_multiplier > 4000) s_settings.x_multiplier = 1000;
  if (s_settings.y_multiplier < 0 || s_settings.y_multiplier > 4000) s_settings.y_multiplier = 1000;
  if (s_settings.z_multiplier < 0 || s_settings.z_multiplier > 4000) s_settings.z_multiplier = 1000;
}

// -----------------------------------------------------------------------------
// MAIN MENU CUSTOM MENULAYER CALLBACKS & CLICK CONFIG
// -----------------------------------------------------------------------------

// EXPLICIT HEIGHT ENFORCEMENT TO FIX TOUCH MATH SCALING ISSUES
static int16_t menu_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return UI_ROW_HEIGHT;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return UI_HEADER_HEIGHT;
}

static uint16_t main_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t main_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_menu_sections[0].num_items;
}

static void main_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, "WhisperClock Settings");
}

static void main_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  SimpleMenuItem item = s_menu_items[cell_index->row];
  bool is_editing = (s_main_edit_row == cell_index->row);

  if (is_editing) {
    char edit_title[40];
    snprintf(edit_title, sizeof(edit_title), "> %s <", item.title);
    menu_cell_basic_draw(ctx, cell_layer, edit_title, item.subtitle, NULL);
  } else {
    menu_cell_basic_draw(ctx, cell_layer, item.title, item.subtitle, NULL);
  }
}

static void main_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_main_edit_row != -1) {
    const char* title = s_menu_items[s_main_edit_row].title;
    if (strcmp(title, "Speaker Volume") == 0) {
      if (s_settings.volume < 20) s_settings.volume += 1;
      else { s_settings.volume += 5; if (s_settings.volume > 100) s_settings.volume = 100; }
    } else if (strcmp(title, "Night Volume") == 0) {
      if (s_settings.night_volume < 20) s_settings.night_volume += 1;
      else { s_settings.night_volume += 5; if (s_settings.night_volume > 100) s_settings.night_volume = 100; }
    } else if (strcmp(title, "Voice Interval") == 0) {
      s_settings.playback_speed += 10; if (s_settings.playback_speed > 350) s_settings.playback_speed = 350;
    } else if (strcmp(title, "Audio Trim") == 0) {
      s_settings.clip_trim += 10; if (s_settings.clip_trim > 150) s_settings.clip_trim = 150;
    } else if (strcmp(title, "Night Start") == 0) {
      s_settings.quiet_start_hour = (s_settings.quiet_start_hour + 1) % 24;
    } else if (strcmp(title, "Night End") == 0) {
      s_settings.quiet_end_hour = (s_settings.quiet_end_hour + 1) % 24;
    } else if (strcmp(title, "Flick Sensitivity") == 0) {
      s_settings.default_flick_sensitivity--;
      if (s_settings.default_flick_sensitivity < 55) s_settings.default_flick_sensitivity = 55;
    } else if (strcmp(title, "Tap Sensitivity") == 0) {
      s_settings.tap_sensitivity++;
      if (s_settings.tap_sensitivity > 30) s_settings.tap_sensitivity = 30;
    } else if (strcmp(title, "X-Axis Sensitivity") == 0) {
      int val = s_settings.x_multiplier;
      if (val >= 500) val += 50;
      else if (val >= 10) val += 10;
      else if (val == 5) val = 10;
      else if (val == 0) val = 5;
      if (val > 4000) val = 4000;
      s_settings.x_multiplier = val;
    } else if (strcmp(title, "Y-Axis Sensitivity") == 0) {
      int val = s_settings.y_multiplier;
      if (val >= 500) val += 50;
      else if (val >= 10) val += 10;
      else if (val == 5) val = 10;
      else if (val == 0) val = 5;
      if (val > 4000) val = 4000;
      s_settings.y_multiplier = val;
    } else if (strcmp(title, "Z-Axis Sensitivity") == 0) {
      int val = s_settings.z_multiplier;
      if (val >= 500) val += 50;
      else if (val >= 10) val += 10;
      else if (val == 5) val = 10;
      else if (val == 0) val = 5;
      if (val > 4000) val = 4000;
      s_settings.z_multiplier = val;
    }
    update_menu_ui_only();
  } else {
    menu_layer_set_selected_next(s_main_menu_layer, true, MenuRowAlignCenter, true);
  }
}

static void main_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_main_edit_row != -1) {
    const char* title = s_menu_items[s_main_edit_row].title;
    if (strcmp(title, "Speaker Volume") == 0) {
      if (s_settings.volume == 0) { /* do nothing */ }
      else if (s_settings.volume <= 20) s_settings.volume -= 1;
      else s_settings.volume -= 5;
    } else if (strcmp(title, "Night Volume") == 0) {
      if (s_settings.night_volume == 0) { /* do nothing */ }
      else if (s_settings.night_volume <= 20) s_settings.night_volume -= 1;
      else s_settings.night_volume -= 5;
    } else if (strcmp(title, "Voice Interval") == 0) {
      s_settings.playback_speed -= 10; if (s_settings.playback_speed < 0) s_settings.playback_speed = 0;
    } else if (strcmp(title, "Audio Trim") == 0) {
      s_settings.clip_trim -= 10; if (s_settings.clip_trim < 0) s_settings.clip_trim = 0;
    } else if (strcmp(title, "Night Start") == 0) {
      s_settings.quiet_start_hour = (s_settings.quiet_start_hour == 0) ? 23 : s_settings.quiet_start_hour - 1;
    } else if (strcmp(title, "Night End") == 0) {
      s_settings.quiet_end_hour = (s_settings.quiet_end_hour == 0) ? 23 : s_settings.quiet_end_hour - 1;
    } else if (strcmp(title, "Flick Sensitivity") == 0) {
      s_settings.default_flick_sensitivity++;
      if (s_settings.default_flick_sensitivity > 70) s_settings.default_flick_sensitivity = 70;
    } else if (strcmp(title, "Tap Sensitivity") == 0) {
      if (s_settings.tap_sensitivity > 0) s_settings.tap_sensitivity--;
    } else if (strcmp(title, "X-Axis Sensitivity") == 0) {
      int val = s_settings.x_multiplier;
      if (val > 500) val -= 50;
      else if (val > 10) val -= 10;
      else if (val == 10) val = 5;
      else if (val == 5) val = 0;
      if (val < 0) val = 0;
      s_settings.x_multiplier = val;
    } else if (strcmp(title, "Y-Axis Sensitivity") == 0) {
      int val = s_settings.y_multiplier;
      if (val > 500) val -= 50;
      else if (val > 10) val -= 10;
      else if (val == 10) val = 5;
      else if (val == 5) val = 0;
      if (val < 0) val = 0;
      s_settings.y_multiplier = val;
    } else if (strcmp(title, "Z-Axis Sensitivity") == 0) {
      int val = s_settings.z_multiplier;
      if (val > 500) val -= 50;
      else if (val > 10) val -= 10;
      else if (val == 10) val = 5;
      else if (val == 5) val = 0;
      if (val < 0) val = 0;
      s_settings.z_multiplier = val;
    }
    update_menu_ui_only();
  } else {
    menu_layer_set_selected_next(s_main_menu_layer, false, MenuRowAlignCenter, true);
  }
}

static void main_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  MenuIndex idx = menu_layer_get_selected_index(s_main_menu_layer);
  const char* title = s_menu_items[idx.row].title;

  bool is_editable = (
    strcmp(title, "Speaker Volume") == 0 ||
    strcmp(title, "Night Volume") == 0 ||
    strcmp(title, "Voice Interval") == 0 ||
    strcmp(title, "Audio Trim") == 0 ||
    strcmp(title, "Night Start") == 0 ||
    strcmp(title, "Night End") == 0 ||
    strcmp(title, "Flick Sensitivity") == 0 ||
    strcmp(title, "Tap Sensitivity") == 0 ||
    strcmp(title, "X-Axis Sensitivity") == 0 ||
    strcmp(title, "Y-Axis Sensitivity") == 0 ||
    strcmp(title, "Z-Axis Sensitivity") == 0
  );

  if (is_editable) {
    if (s_main_edit_row == idx.row) {
      s_main_edit_row = -1;
      save_settings();
    } else {
      s_main_edit_row = idx.row;
    }
    menu_layer_reload_data(s_main_menu_layer);
  } else {
    if (s_menu_items[idx.row].callback) {
      s_menu_items[idx.row].callback(idx.row, NULL);
    }
  }
}

static void main_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_main_edit_row != -1) {
    s_main_edit_row = -1;
    save_settings();
    menu_layer_reload_data(s_main_menu_layer);
  } else {
    window_stack_pop(true);
  }
}

static void main_ccp(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, main_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, main_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, main_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, main_back_click_handler);
}


// -----------------------------------------------------------------------------
// FUZZY TUNER UI CALLBACKS (CUSTOM MENULAYER)
// -----------------------------------------------------------------------------
static uint16_t tuner_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t tuner_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return 10;
}

static void tuner_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, "Calibration Board");
}

static void tuner_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  char title[32];
  char subtitle[32];
  bool is_editing = (s_tuner_edit_row == cell_index->row);

  switch(cell_index->row) {
    case 0: strcpy(title, "Test Pacing"); strcpy(subtitle, "Hear changes live"); break;
    case 1: strcpy(title, "Prefix Gap"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.prefix_gap); break;
    case 2: strcpy(title, "Prefix Trim"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.prefix_trim); break;
    case 3: strcpy(title, "Almost/Just After"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.fuzzy_mod_gap); break;
    case 4: strcpy(title, "Fractions / Convo"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.fuzzy_conv_gap); break;
    case 5: strcpy(title, "Past"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.fuzzy_past_gap); break;
    case 6: strcpy(title, "To / Till / After"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.fuzzy_to_gap); break;
    case 7: strcpy(title, "Hour Snapping"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.fuzzy_tight_gap); break;
    case 8: strcpy(title, "AM / PM Blending"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_settings.fuzzy_ampm_gap); break;
    case 9: strcpy(title, "Reset to Zero"); strcpy(subtitle, "Clear all adjustments"); break;
  }

  if (is_editing) {
    char edit_title[40];
    snprintf(edit_title, sizeof(edit_title), "> %s <", title);
    menu_cell_basic_draw(ctx, cell_layer, edit_title, subtitle, NULL);
  } else {
    menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
  }
}

static void tuner_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_tuner_edit_row != -1) {
    switch(s_tuner_edit_row) {
      case 1: s_settings.prefix_gap += 20; if (s_settings.prefix_gap > 800) s_settings.prefix_gap = 800; break;
      case 2: s_settings.prefix_trim += 20; if (s_settings.prefix_trim > 800) s_settings.prefix_trim = 800; break;
      case 3: s_settings.fuzzy_mod_gap += 20; if (s_settings.fuzzy_mod_gap > 800) s_settings.fuzzy_mod_gap = 800; break;
      case 4: s_settings.fuzzy_conv_gap += 20; if (s_settings.fuzzy_conv_gap > 800) s_settings.fuzzy_conv_gap = 800; break;
      case 5: s_settings.fuzzy_past_gap += 20; if (s_settings.fuzzy_past_gap > 800) s_settings.fuzzy_past_gap = 800; break;
      case 6: s_settings.fuzzy_to_gap += 20; if (s_settings.fuzzy_to_gap > 800) s_settings.fuzzy_to_gap = 800; break;
      case 7: s_settings.fuzzy_tight_gap += 20; if (s_settings.fuzzy_tight_gap > 800) s_settings.fuzzy_tight_gap = 800; break;
      case 8: s_settings.fuzzy_ampm_gap += 20; if (s_settings.fuzzy_ampm_gap > 800) s_settings.fuzzy_ampm_gap = 800; break;
    }
    update_tuner_ui_only();
  } else {
    menu_layer_set_selected_next(s_tuner_menu_layer, true, MenuRowAlignCenter, true);
  }
}

static void tuner_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_tuner_edit_row != -1) {
    switch(s_tuner_edit_row) {
      case 1: s_settings.prefix_gap -= 20; if (s_settings.prefix_gap < -800) s_settings.prefix_gap = -800; break;
      case 2: s_settings.prefix_trim -= 20; if (s_settings.prefix_trim < 0) s_settings.prefix_trim = 0; break;
      case 3: s_settings.fuzzy_mod_gap -= 20; if (s_settings.fuzzy_mod_gap < -800) s_settings.fuzzy_mod_gap = -800; break;
      case 4: s_settings.fuzzy_conv_gap -= 20; if (s_settings.fuzzy_conv_gap < -800) s_settings.fuzzy_conv_gap = -800; break;
      case 5: s_settings.fuzzy_past_gap -= 20; if (s_settings.fuzzy_past_gap < -800) s_settings.fuzzy_past_gap = -800; break;
      case 6: s_settings.fuzzy_to_gap -= 20; if (s_settings.fuzzy_to_gap < -800) s_settings.fuzzy_to_gap = -800; break;
      case 7: s_settings.fuzzy_tight_gap -= 20; if (s_settings.fuzzy_tight_gap < -800) s_settings.fuzzy_tight_gap = -800; break;
      case 8: s_settings.fuzzy_ampm_gap -= 20; if (s_settings.fuzzy_ampm_gap < -800) s_settings.fuzzy_ampm_gap = -800; break;
    }
    update_tuner_ui_only();
  } else {
    menu_layer_set_selected_next(s_tuner_menu_layer, false, MenuRowAlignCenter, true);
  }
}

static void tuner_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  MenuIndex idx = menu_layer_get_selected_index(s_tuner_menu_layer);

  if (idx.row == 0) {
    test_audio_callback(0, NULL);
  }
  else if (idx.row == 9) {
    s_settings.prefix_gap = 0;
    s_settings.prefix_trim = 0;
    s_settings.fuzzy_mod_gap = 0;
    s_settings.fuzzy_conv_gap = 0;
    s_settings.fuzzy_past_gap = 0;
    s_settings.fuzzy_to_gap = 0;
    s_settings.fuzzy_tight_gap = 0;
    s_settings.fuzzy_ampm_gap = 0;

    s_tuner_edit_row = -1;
    update_tuner_and_save();
    vibes_short_pulse();
  }
  else {
    if (s_tuner_edit_row == idx.row) {
      s_tuner_edit_row = -1;
      save_settings();
    } else {
      s_tuner_edit_row = idx.row;
    }
    menu_layer_reload_data(s_tuner_menu_layer);
  }
}

static void tuner_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_tuner_edit_row != -1) {
    s_tuner_edit_row = -1;
    save_settings();
    menu_layer_reload_data(s_tuner_menu_layer);
  } else {
    window_stack_pop(true);
  }
}

static void tuner_ccp(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, tuner_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, tuner_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, tuner_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, tuner_back_click_handler);
}

static void tuner_window_appear(Window *window) {
  #ifdef PBL_TOUCH
  if (touch_service_is_enabled()) touch_service_subscribe(tuner_touch_handler, NULL);
  #endif
}

static void tuner_window_disappear(Window *window) {
  #ifdef PBL_TOUCH
  touch_service_unsubscribe();
  #endif
}

static void tuner_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_tuner_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_tuner_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = tuner_get_num_sections_callback,
    .get_num_rows = tuner_get_num_rows_callback,
    .get_cell_height = menu_get_cell_height_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = tuner_draw_header_callback,
    .draw_row = tuner_draw_row_callback,
  });

  window_set_click_config_provider(window, tuner_ccp);
  layer_add_child(window_layer, menu_layer_get_layer(s_tuner_menu_layer));

  #if defined(PBL_COLOR)
  menu_layer_set_normal_colors(s_tuner_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_tuner_menu_layer, GColorCobaltBlue, GColorWhite);
  #endif
}

static void tuner_window_unload(Window *window) {
  #ifdef PBL_TOUCH
  kill_kinetic_timer();
  #endif

  if (s_tuner_menu_layer) {
    menu_layer_destroy(s_tuner_menu_layer);
    s_tuner_menu_layer = NULL;
  }
  s_tuner_edit_row = -1;
}

static void open_tuner_callback(int index, void *context) {
  if (!s_tuner_window) {
    s_tuner_window = window_create();
    window_set_window_handlers(s_tuner_window, (WindowHandlers) {
      .load = tuner_window_load,
      .appear = tuner_window_appear,
      .disappear = tuner_window_disappear,
      .unload = tuner_window_unload
    });
  }
  window_stack_push(s_tuner_window, true);
}


// -----------------------------------------------------------------------------
// STANDARD SETTINGS TOGGLE CALLBACKS
// -----------------------------------------------------------------------------

static void toggle_beta_callback(int index, void *context) {
  s_settings.enable_beta_features = !s_settings.enable_beta_features;
  update_menu_and_save();
}

static void confirm_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_delete(GESTURE_PERSIST_KEY);
    vibes_double_pulse();
  }
  update_menu_and_save();
  window_stack_pop(true);
}

static void confirm_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, confirm_select_click_handler);
}

static void confirm_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  int16_t text_h = 120;
  s_confirm_text_layer = text_layer_create(GRect(0, (bounds.size.h - text_h) / 2, bounds.size.w, text_h));
  text_layer_set_text(s_confirm_text_layer, "Reset custom\ngesture?\n\n[SELECT] Confirm\n[BACK] Cancel");
  text_layer_set_font(s_confirm_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_confirm_text_layer, GTextAlignmentCenter);

  window_set_background_color(window, GColorWhite);
  text_layer_set_text_color(s_confirm_text_layer, GColorBlack);
  text_layer_set_background_color(s_confirm_text_layer, GColorClear);

  layer_add_child(window_layer, text_layer_get_layer(s_confirm_text_layer));
}

static void confirm_window_unload(Window *window) {
  if (s_confirm_text_layer) { text_layer_destroy(s_confirm_text_layer); s_confirm_text_layer = NULL; }
}

void push_reset_confirmation_window() {
  if (!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_window_handlers(s_confirm_window, (WindowHandlers) { .load = confirm_window_load, .unload = confirm_window_unload });
    window_set_click_config_provider(s_confirm_window, confirm_click_config_provider);
  }
  window_stack_push(s_confirm_window, true);
}

static void toggle_clock_mode_callback(int index, void *context) {
  s_settings.clock_mode++;
  if (s_settings.clock_mode > 6) s_settings.clock_mode = 0;

  if (s_main_menu_layer) {
    MenuIndex idx = { .section = 0, .row = 0 };
    menu_layer_set_selected_index(s_main_menu_layer, idx, MenuRowAlignTop, false);
  }

  update_menu_and_save();
}

static void toggle_dialect_callback(int index, void *context) {
  s_settings.is_us_dialect = !s_settings.is_us_dialect;
  update_menu_and_save();
}

static void toggle_quiet_time_callback(int index, void *context) {
  s_settings.respect_quiet_time = !s_settings.respect_quiet_time;
  update_menu_and_save();
}

static void toggle_night_worker_callback(int index, void *context) {
  s_settings.night_worker_sleep = !s_settings.night_worker_sleep;
  update_menu_and_save();
}

static void record_gesture_callback(int index, void *context) { gesture_start_recording(); }
static void clear_gesture_callback(int index, void *context) { push_reset_confirmation_window(); }

static void toggle_prefix_callback(int index, void *context) {
  if (s_settings.prefix_mode == 2) {
    s_settings.prefix_mode = 1;
  } else if (s_settings.prefix_mode == 1) {
    s_settings.prefix_mode = 0;
  } else {
    s_settings.prefix_mode = 2;
  }
  update_menu_and_save();
}

static void toggle_ampm_callback(int index, void *context) {
  s_settings.say_ampm = !s_settings.say_ampm;
  update_menu_and_save();
}

static void toggle_gesture_mode_callback(int index, void *context) {
  s_settings.gesture_mode = (s_settings.gesture_mode + 1) % 3;

  update_menu_and_save();

  if (s_main_menu_layer) {
    MenuIndex idx = { .section = 0, .row = index };
    menu_layer_set_selected_index(s_main_menu_layer, idx, MenuRowAlignCenter, false);
  }
}

static void build_menu_items() {
  if (s_settings.volume == 0) snprintf(s_volume_text, sizeof(s_volume_text), "Level: MUTE");
  else snprintf(s_volume_text, sizeof(s_volume_text), "Level: %d%%", s_settings.volume);

  if (s_settings.night_volume == 0) snprintf(s_night_volume_text, sizeof(s_night_volume_text), "Level: MUTE");
  else snprintf(s_night_volume_text, sizeof(s_night_volume_text), "Level: %d%%", s_settings.night_volume);

  snprintf(s_speed_text, sizeof(s_speed_text), "Interval: %d ms", s_settings.playback_speed);
  snprintf(s_trim_text, sizeof(s_trim_text), "Trim End: %d ms", s_settings.clip_trim);

  char *clock_text = "Digital 12-Hour";
  if (s_settings.clock_mode == MODE_24H_MILITARY) clock_text = "Military 24-Hour";
  else if (s_settings.clock_mode == MODE_24H_CIVILIAN) clock_text = "Civilian 24-Hour";
  else if (s_settings.clock_mode == MODE_COLLOQUIAL) clock_text = "Colloquial";
  else if (s_settings.clock_mode == MODE_TELECOM) clock_text = "Telecom Radio";
  else if (s_settings.clock_mode == MODE_FUZZY) clock_text = "Fuzzy";
  else if (s_settings.clock_mode == MODE_SYSTEM_DEFAULT) clock_text = "System Default";

  char *prefix_text = "None";
  if (s_settings.prefix_mode == 1) prefix_text = "\"It's...\"";
  else if (s_settings.prefix_mode == 2) prefix_text = "\"The time is...\"";

  int i = 0;

  // --- TOP AUDIO/BEHAVIOR BLOCK ---
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Clock Mode", .subtitle = clock_text, .callback = toggle_clock_mode_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Test Audio", .subtitle = "Preview time speaking", .callback = test_audio_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Speaker Volume", .subtitle = s_volume_text, .callback = NULL };

  // Scheduled Night Mode / Quiet Time
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Quiet Time", .subtitle = s_settings.respect_quiet_time ? "ON" : "OFF", .callback = toggle_quiet_time_callback };

  if (s_settings.respect_quiet_time) {
    snprintf(s_quiet_start_text, sizeof(s_quiet_start_text), "Night Start: %02d:00", s_settings.quiet_start_hour);
    snprintf(s_quiet_end_text, sizeof(s_quiet_end_text), "Night End: %02d:00", s_settings.quiet_end_hour);

    s_menu_items[i++] = (SimpleMenuItem) { .title = "Night Start", .subtitle = s_quiet_start_text, .callback = NULL };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Night End", .subtitle = s_quiet_end_text, .callback = NULL };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Night Volume", .subtitle = s_night_volume_text, .callback = NULL };

    // FIX: Night Worker correctly obeys the beta features toggle
    if (s_settings.enable_beta_features) {
      s_menu_items[i++] = (SimpleMenuItem) { .title = "Night Worker", .subtitle = s_settings.night_worker_sleep ? "SLEEP (Battery)" : "AWAKE (Gestures)", .callback = toggle_night_worker_callback };
    }
  }

  // --- TIME FORMATTING BLOCK ---
  if (s_settings.clock_mode == MODE_12H_DIGITAL ||
    s_settings.clock_mode == MODE_24H_MILITARY ||
    s_settings.clock_mode == MODE_24H_CIVILIAN ||
    s_settings.clock_mode == MODE_SYSTEM_DEFAULT) {

    s_menu_items[i++] = (SimpleMenuItem) { .title = "Time Prefix", .subtitle = prefix_text, .callback = toggle_prefix_callback };
    }

    if (s_settings.clock_mode == MODE_12H_DIGITAL ||
      s_settings.clock_mode == MODE_24H_MILITARY ||
      s_settings.clock_mode == MODE_24H_CIVILIAN ||
      s_settings.clock_mode == MODE_SYSTEM_DEFAULT ||
      s_settings.clock_mode == MODE_FUZZY) {

      char *ampm_title = "Speak AM/PM";
    bool is_24h = false;

    if (s_settings.clock_mode == MODE_SYSTEM_DEFAULT) {
      is_24h = clock_is_24h_style();
    } else if (s_settings.clock_mode == MODE_24H_MILITARY || s_settings.clock_mode == MODE_24H_CIVILIAN) {
      is_24h = true;
    }

    if (is_24h) {
      ampm_title = "Append 'Hours'";
    }
    s_menu_items[i++] = (SimpleMenuItem) { .title = ampm_title, .subtitle = s_settings.say_ampm ? "Enabled" : "Disabled", .callback = toggle_ampm_callback };
      }

      if (s_settings.clock_mode == MODE_COLLOQUIAL || s_settings.clock_mode == MODE_FUZZY) {
        s_menu_items[i++] = (SimpleMenuItem) { .title = "Phrase Dialect", .subtitle = s_settings.is_us_dialect ? "US (Till/After)" : "UK (To/Past)", .callback = toggle_dialect_callback };
      }

      // --- PACING AND TRIM ---
      s_menu_items[i++] = (SimpleMenuItem) { .title = "Voice Interval", .subtitle = s_speed_text, .callback = NULL };
      s_menu_items[i++] = (SimpleMenuItem) { .title = "Audio Trim", .subtitle = s_trim_text, .callback = NULL };
      s_menu_items[i++] = (SimpleMenuItem) { .title = "About / Help", .subtitle = "Instructions & Info", .callback = show_about_callback };

      // --- EXPERIMENTAL SETTINGS ---
      s_menu_items[i++] = (SimpleMenuItem) {
        .title = "Experimental Features",
        .subtitle = s_settings.enable_beta_features ? "ON: Config & Physics" : "OFF: Quick Launch Only",
        .callback = toggle_beta_callback
      };

      if (s_settings.enable_beta_features) {
        if (s_settings.clock_mode == MODE_COLLOQUIAL || s_settings.clock_mode == MODE_FUZZY) {
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Fuzzy Pacing Tuner", .subtitle = "Live audio calibration", .callback = open_tuner_callback };
        }

        char *gesture_text = "Default Flick";
        if (s_settings.gesture_mode == 1) gesture_text = "Tap Glass";
        else if (s_settings.gesture_mode == 2) gesture_text = "Custom Axes";

        s_menu_items[i++] = (SimpleMenuItem) { .title = "Gesture Mode", .subtitle = gesture_text, .callback = toggle_gesture_mode_callback };

        if (s_settings.gesture_mode == 0) {
          snprintf(s_sensitivity_text, sizeof(s_sensitivity_text), "Level: %d", 70 - s_settings.default_flick_sensitivity);
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Flick Sensitivity", .subtitle = s_sensitivity_text, .callback = NULL };

        } else if (s_settings.gesture_mode == 1) {
          snprintf(s_tap_sens_text, sizeof(s_tap_sens_text), "Level: %d", s_settings.tap_sensitivity);
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Tap Sensitivity", .subtitle = s_tap_sens_text, .callback = NULL };

        } else {
          // CUSTOM AXES MODE (Advanced UI)
          if (s_settings.x_multiplier == 0) snprintf(s_x_mult_text, sizeof(s_x_mult_text), "Scale: OFF");
          else snprintf(s_x_mult_text, sizeof(s_x_mult_text), "Scale: %d.%d%%", s_settings.x_multiplier / 10, s_settings.x_multiplier % 10);

          if (s_settings.y_multiplier == 0) snprintf(s_y_mult_text, sizeof(s_y_mult_text), "Scale: OFF");
          else snprintf(s_y_mult_text, sizeof(s_y_mult_text), "Scale: %d.%d%%", s_settings.y_multiplier / 10, s_settings.y_multiplier % 10);

          if (s_settings.z_multiplier == 0) snprintf(s_z_mult_text, sizeof(s_z_mult_text), "Scale: OFF");
          else snprintf(s_z_mult_text, sizeof(s_z_mult_text), "Scale: %d.%d%%", s_settings.z_multiplier / 10, s_settings.z_multiplier % 10);

          s_menu_items[i++] = (SimpleMenuItem) { .title = "X-Axis Sensitivity", .subtitle = s_x_mult_text, .callback = NULL };
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Y-Axis Sensitivity", .subtitle = s_y_mult_text, .callback = NULL };
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Z-Axis Sensitivity", .subtitle = s_z_mult_text, .callback = NULL };
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Record Gesture", .subtitle = "Train your watch", .callback = record_gesture_callback };

          char *clear_text = persist_exists(GESTURE_PERSIST_KEY) ? "Revert to default fallback" : "Already using default fallback";
          s_menu_items[i++] = (SimpleMenuItem) { .title = "Clear Gesture", .subtitle = clear_text, .callback = clear_gesture_callback };
        }
      }

      s_menu_sections[0].title = "WhisperClock Settings";
      s_menu_sections[0].num_items = i;
      s_menu_sections[0].items = s_menu_items;
}

// -----------------------------------------------------------------------------
// KINETIC PHYSICS ENGINE (PBL_TOUCH)
// -----------------------------------------------------------------------------
#ifdef PBL_TOUCH
static int16_t s_touch_start_y = 0;
static int16_t s_touch_last_y = 0;
static bool s_is_drag = false;

static int16_t s_scroll_velocity = 0;
static AppTimer *s_kinetic_timer = NULL;

static void kill_kinetic_timer() {
  if (s_kinetic_timer) {
    app_timer_cancel(s_kinetic_timer);
    s_kinetic_timer = NULL;
  }
}

static void apply_clamped_scroll(ScrollLayer *layer, int16_t delta_y) {
  GPoint offset = scroll_layer_get_content_offset(layer);
  offset.y += delta_y;

  int content_h = scroll_layer_get_content_size(layer).h;
  int layer_h = layer_get_bounds(scroll_layer_get_layer(layer)).size.h;
  int max_scroll = -(content_h - layer_h);
  if (max_scroll > 0) max_scroll = 0;

  if (offset.y > 0) offset.y = 0;
  if (offset.y < max_scroll) offset.y = max_scroll;

  scroll_layer_set_content_offset(layer, offset, false);
}

static void kinetic_timer_callback(void *data) {
  ScrollLayer *scroll_layer = (ScrollLayer *)data;
  if (!scroll_layer) return;

  GPoint old_offset = scroll_layer_get_content_offset(scroll_layer);
  apply_clamped_scroll(scroll_layer, s_scroll_velocity);
  GPoint new_offset = scroll_layer_get_content_offset(scroll_layer);

  s_scroll_velocity = (s_scroll_velocity * 90) / 100;

  if (abs(s_scroll_velocity) <= 1 || old_offset.y == new_offset.y) {
    s_scroll_velocity = 0;
    s_kinetic_timer = NULL;
  } else {
    s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, scroll_layer);
  }
}

static void menu_touch_handler(const TouchEvent *event, void *context) {
  MenuLayer *menu_layer = s_main_menu_layer;
  ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(menu_layer);

  if (event->type == TouchEvent_Touchdown) {
    s_touch_start_y = event->y;
    s_touch_last_y = event->y;
    s_is_drag = false;
    s_scroll_velocity = 0;
    kill_kinetic_timer();
  }
  else if (event->type == TouchEvent_PositionUpdate) {
    if (!s_is_drag && abs(event->y - s_touch_start_y) > 15) {
      s_is_drag = true;
    }

    if (s_is_drag) {
      int16_t delta_y = event->y - s_touch_last_y;
      s_scroll_velocity = delta_y;
      apply_clamped_scroll(scroll_layer, delta_y);
      s_touch_last_y = event->y;
    }
  }
  else if (event->type == TouchEvent_Liftoff) {
    if (!s_is_drag) {
      // FIX: Convert absolute screen coordinates to local window coordinates to account for the status bar
      Layer *window_layer = window_get_root_layer(s_menu_window);
      int16_t local_y = event->y - layer_get_frame(window_layer).origin.y;

      GPoint offset = scroll_layer_get_content_offset(scroll_layer);
      int16_t content_y = local_y - offset.y;
      int total_rows = s_menu_sections[0].num_items;

      int16_t header_height = UI_HEADER_HEIGHT;
      int16_t row_height = UI_ROW_HEIGHT;

      if (content_y >= header_height) {
        int tapped_row = (content_y - header_height) / row_height;

        if (tapped_row >= 0 && tapped_row < total_rows) {
          MenuIndex current_selection = menu_layer_get_selected_index(menu_layer);

          if (current_selection.row == tapped_row) {
            main_select_click_handler(NULL, NULL);
          } else {
            if (s_main_edit_row != -1) {
              s_main_edit_row = -1;
              menu_layer_reload_data(menu_layer);
            }
            MenuIndex new_selection = (MenuIndex){.section = 0, .row = tapped_row};
            menu_layer_set_selected_index(menu_layer, new_selection, MenuRowAlignCenter, true);
          }
        }
      }
    } else {
      if (abs(s_scroll_velocity) > 2) {
        s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, scroll_layer);
      }
    }
  }
}

static void tuner_touch_handler(const TouchEvent *event, void *context) {
  MenuLayer *menu_layer = s_tuner_menu_layer;
  ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(menu_layer);

  if (event->type == TouchEvent_Touchdown) {
    s_touch_start_y = event->y;
    s_touch_last_y = event->y;
    s_is_drag = false;
    s_scroll_velocity = 0;
    kill_kinetic_timer();
  }
  else if (event->type == TouchEvent_PositionUpdate) {
    if (!s_is_drag && abs(event->y - s_touch_start_y) > 15) {
      s_is_drag = true;
    }

    if (s_is_drag) {
      int16_t delta_y = event->y - s_touch_last_y;
      s_scroll_velocity = delta_y;
      apply_clamped_scroll(scroll_layer, delta_y);
      s_touch_last_y = event->y;
    }
  }
  else if (event->type == TouchEvent_Liftoff) {
    if (!s_is_drag) {
      // FIX: Convert absolute screen coordinates to local window coordinates to account for the status bar
      Layer *window_layer = window_get_root_layer(s_tuner_window);
      int16_t local_y = event->y - layer_get_frame(window_layer).origin.y;

      GPoint offset = scroll_layer_get_content_offset(scroll_layer);
      int16_t content_y = local_y - offset.y;
      int total_rows = 10;
      int16_t header_height = UI_HEADER_HEIGHT;
      int16_t row_height = UI_ROW_HEIGHT;

      if (content_y >= header_height) {
        int tapped_row = (content_y - header_height) / row_height;

        if (tapped_row >= 0 && tapped_row < total_rows) {
          MenuIndex current_selection = menu_layer_get_selected_index(menu_layer);

          if (current_selection.row == tapped_row) {
            tuner_select_click_handler(NULL, NULL);
          } else {
            if (s_tuner_edit_row != -1) {
              s_tuner_edit_row = -1;
              menu_layer_reload_data(menu_layer);
            }
            MenuIndex new_selection = (MenuIndex){.section = 0, .row = tapped_row};
            menu_layer_set_selected_index(menu_layer, new_selection, MenuRowAlignCenter, true);
          }
        }
      }
    } else {
      if (abs(s_scroll_velocity) > 2) {
        s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, scroll_layer);
      }
    }
  }
}

static int16_t s_about_touch_start_y = 0;
static int16_t s_about_touch_last_y = 0;
static bool s_about_is_drag = false;

static void about_touch_handler(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) {
    s_about_touch_start_y = event->y;
    s_about_touch_last_y = event->y;
    s_about_is_drag = false;
    s_scroll_velocity = 0;
    kill_kinetic_timer();
  }
  else if (event->type == TouchEvent_PositionUpdate) {
    if (!s_about_is_drag && abs(event->y - s_about_touch_start_y) > 15) {
      s_about_is_drag = true;
    }

    if (s_about_is_drag) {
      int16_t delta_y = event->y - s_about_touch_last_y;
      s_scroll_velocity = delta_y;
      apply_clamped_scroll(s_about_scroll_layer, delta_y);
      s_about_touch_last_y = event->y;
    }
  }
  else if (event->type == TouchEvent_Liftoff) {
    if (!s_about_is_drag) {
      window_stack_pop(true);
    } else {
      if (abs(s_scroll_velocity) > 2) {
        s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, s_about_scroll_layer);
      }
    }
  }
}
#endif

static void about_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_about_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_about_scroll_layer, window);

  const char *about_text =
  "WhisperClock v1.2\n\n"
  "A stealthy, spoken time check for your Pebble.\n\n"
  "--- HOW TO USE ---\n"
  "1. Map WhisperClock to 'Quick Launch' in your Pebble settings.\n"
  "2. Hold that button from your watchface to hear the time.\n"
  "(Press any button to instantly cancel audio).\n\n"
  "--- AUDIO CONTROLS ---\n"
  "Customize your playback in the main menu:\n"
  "- Modes: Cycle between 12-Hour, 24-Hour, Telecom Radio, and Colloquial styles.\n"
  "- Volume: Mute to 100%\n"
  "- Scheduled Night Mode: Mute or lower your volume at night, and auto-sleep gestures to save battery.\n"
  "- Interval: Adjust the pause between spoken words.\n"
  "- Trim: Cut the silent tails off audio clips for a punchier sentence.\n\n"
  "--- EXPERIMENTAL PHYSICS ---\n"
  "Turn on 'Experimental Features' to trigger the watch hands-free using custom wrist-flicks or glass taps!\n\n"
  "Created by J_B";

  s_about_text_layer = text_layer_create(GRect(0, 10, bounds.size.w, 4000));
  text_layer_set_text(s_about_text_layer, about_text);
  text_layer_set_font(s_about_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_about_text_layer, GTextAlignmentCenter);

  GSize max_size = text_layer_get_content_size(s_about_text_layer);
  text_layer_set_size(s_about_text_layer, max_size);
  scroll_layer_set_content_size(s_about_scroll_layer, GSize(bounds.size.w, max_size.h + 20));

  scroll_layer_add_child(s_about_scroll_layer, text_layer_get_layer(s_about_text_layer));
  layer_add_child(window_layer, scroll_layer_get_layer(s_about_scroll_layer));
}

static void about_window_appear(Window *window) {
  #ifdef PBL_TOUCH
  if (touch_service_is_enabled()) touch_service_subscribe(about_touch_handler, NULL);
  #endif
}

static void about_window_disappear(Window *window) {
  #ifdef PBL_TOUCH
  touch_service_unsubscribe();
  #endif
}

static void about_window_unload(Window *window) {
  #ifdef PBL_TOUCH
  kill_kinetic_timer();
  #endif
  if (s_about_text_layer) text_layer_destroy(s_about_text_layer);
  if (s_about_scroll_layer) scroll_layer_destroy(s_about_scroll_layer);
}

static void show_about_callback(int index, void *context) {
  if (!s_about_window) {
    s_about_window = window_create();
    window_set_window_handlers(s_about_window, (WindowHandlers) { .load = about_window_load, .appear = about_window_appear, .disappear = about_window_disappear, .unload = about_window_unload });
  }
  window_stack_push(s_about_window, true);
}

// -----------------------------------------------------------------------------
// SPEAKING UI OVERLAY (ANIMATION + TIME + DATE)
// -----------------------------------------------------------------------------
static Window *s_speaking_window = NULL;
static TextLayer *s_time_text_layer = NULL;
static TextLayer *s_date_text_layer = NULL;
static Layer *s_speaking_canvas_layer = NULL;

static GBitmap *s_gear_bitmap = NULL;

static AppTimer *s_settings_timer = NULL;
static bool s_settings_ready = false;

static char s_time_buffer[16];
static char s_date_buffer[32];
static const char *s_current_time_format;

static AppTimer *s_animation_timer = NULL;

// NEW VISUALIZER STATE MACHINE VARIABLES
static bool s_is_speaking = false;
static AppTimer *s_anim_stop_timer = NULL;

static const int BAR_HEIGHTS[4][5] = {
  {10, 10, 10, 10, 10},
  {10, 30, 10, 30, 10},
  {25, 15, 40, 15, 25},
  {15, 25, 20, 25, 15}
};
static int s_current_frame = 0;

static void settings_ready_callback(void *data) {
  s_settings_timer = NULL;
  s_settings_ready = true;
  if (s_speaking_canvas_layer) {
    layer_mark_dirty(s_speaking_canvas_layer);
  }
}

// DYNAMIC SYNC: Callback physically drops the waveform to a flatline exactly when the clip ends
static void anim_stop_callback(void *data) {
  s_anim_stop_timer = NULL;
  s_is_speaking = false;
  s_current_frame = 0;
  if (s_speaking_canvas_layer) {
    layer_mark_dirty(s_speaking_canvas_layer);
  }
}

// DYNAMIC SYNC: Start the visualizer and queue it to stop exactly when the clip ends
void start_visualizer_for_clip(uint32_t duration_ms) {
  s_is_speaking = true;

  if (s_anim_stop_timer) {
    app_timer_cancel(s_anim_stop_timer);
  }
  s_anim_stop_timer = app_timer_register(duration_ms, anim_stop_callback, NULL);

  // Instant Hop to an active frame
  s_current_frame = (rand() % 3) + 1;
  if (s_speaking_canvas_layer) {
    layer_mark_dirty(s_speaking_canvas_layer);
  }
}

// SAFETY: Instantly halts the waveform if playback is manually cancelled
void stop_visualizer(void) {
  if (s_anim_stop_timer) {
    app_timer_cancel(s_anim_stop_timer);
    s_anim_stop_timer = NULL;
  }
  s_is_speaking = false;
  s_current_frame = 0;
  if (s_speaking_canvas_layer) {
    layer_mark_dirty(s_speaking_canvas_layer);
  }
}

static void animation_timer_callback(void *data) {
  if (s_is_speaking) {
    // Pick a random active frame (1, 2, or 3) so it acts like a live voice waveform
    int next_frame = (rand() % 3) + 1;
    if (next_frame == s_current_frame) {
      next_frame = (next_frame % 3) + 1; // Prevent frame stalling
    }
    s_current_frame = next_frame;
    if (s_speaking_canvas_layer) {
      layer_mark_dirty(s_speaking_canvas_layer);
    }
  }
  // If not speaking, s_current_frame remains locked at 0 (flatline) by the stop callback

  s_animation_timer = app_timer_register(100, animation_timer_callback, NULL);
}

static void speaking_canvas_update_proc(Layer *layer, GContext *ctx) {
  if (!s_settings_ready) return;
  GRect bounds = layer_get_bounds(layer);

  if (s_gear_bitmap) {
    GRect bitmap_bounds = gbitmap_get_bounds(s_gear_bitmap);
    int gear_x = bounds.size.w - 15;
    int gear_y = 44;
    GRect dest_rect = GRect(
      gear_x - (bitmap_bounds.size.w / 2),
                            gear_y - (bitmap_bounds.size.h / 2),
                            bitmap_bounds.size.w,
                            bitmap_bounds.size.h
    );

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_gear_bitmap, dest_rect);
  }

  int center_x = bounds.size.w / 2;
  int center_y = 54;
  int bar_width = 8;
  int spacing = 12;
  int start_x = center_x - ((bar_width * 5 + spacing * 4) / 2) + (bar_width / 2);

  const int* heights = BAR_HEIGHTS[s_current_frame];

  graphics_context_set_antialiased(ctx, true);

  for(int i = 0; i < 5; i++) {
    if (i == 0 || i == 2 || i == 4) {
      graphics_context_set_fill_color(ctx, GColorCyan);
    } else {
      graphics_context_set_fill_color(ctx, GColorWhite);
    }

    int h = heights[i];
    int x_pos = start_x + (i * (bar_width + spacing));
    int y_pos = center_y - (h / 2);

    GRect pill_rect = GRect(x_pos - (bar_width/2), y_pos, bar_width, h);
    graphics_fill_rect(ctx, pill_rect, bar_width / 2, GCornersAll);
  }
}

static void speaking_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  strftime(s_time_buffer, sizeof(s_time_buffer), s_current_time_format, tick_time);
  text_layer_set_text(s_time_text_layer, s_time_buffer);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%a, %b %d", tick_time);
  text_layer_set_text(s_date_text_layer, s_date_buffer);
}

static void speaking_click_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_playback();
  hide_speaking_graphic();
}

static void speaking_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (!s_settings_ready) {
    cancel_playback();
    hide_speaking_graphic();
    return;
  }

  cancel_playback();
  hide_speaking_graphic();
  settings_window_push();
}

static void speaking_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, speaking_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, speaking_click_handler);
}

static void speaking_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorBlack);

  s_gear_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SETTINGS_GEAR);

  s_settings_ready = false;
  s_settings_timer = app_timer_register(400, settings_ready_callback, NULL);

  s_speaking_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_speaking_canvas_layer, speaking_canvas_update_proc);
  layer_add_child(window_layer, s_speaking_canvas_layer);

  s_time_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 20, bounds.size.w, 40));
  text_layer_set_background_color(s_time_text_layer, GColorClear);
  text_layer_set_text_color(s_time_text_layer, GColorWhite);
  text_layer_set_font(s_time_text_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_text_alignment(s_time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_text_layer));

  s_date_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 + 20, bounds.size.w, 30));
  text_layer_set_background_color(s_date_text_layer, GColorClear);
  text_layer_set_text_color(s_date_text_layer, GColorWhite);
  text_layer_set_font(s_date_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(s_date_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_text_layer));

  s_current_frame = 0;
  s_is_speaking = false;
  s_animation_timer = app_timer_register(100, animation_timer_callback, NULL);
}

static void speaking_window_appear(Window *window) {
  light_enable(false);

  gesture_engine_pause();

  bool is_military;
  if (s_settings.clock_mode == 1) is_military = true;
  else is_military = clock_is_24h_style();

  s_current_time_format = is_military ? "%H:%M:%S" : "%I:%M:%S";
  time_t temp = time(NULL); struct tm *tick_time = localtime(&temp);
  speaking_tick_handler(tick_time, SECOND_UNIT);
  tick_timer_service_subscribe(SECOND_UNIT, speaking_tick_handler);
}

static void speaking_window_disappear(Window *window) {
  tick_timer_service_unsubscribe();

  gesture_engine_resume();
}

static void speaking_window_unload(Window *window) {
  if (s_settings_timer) {
    app_timer_cancel(s_settings_timer);
    s_settings_timer = NULL;
  }

  if (s_animation_timer) {
    app_timer_cancel(s_animation_timer);
    s_animation_timer = NULL;
  }

  if (s_anim_stop_timer) {
    app_timer_cancel(s_anim_stop_timer);
    s_anim_stop_timer = NULL;
  }

  if (s_gear_bitmap) {
    gbitmap_destroy(s_gear_bitmap);
    s_gear_bitmap = NULL;
  }

  if (s_time_text_layer) text_layer_destroy(s_time_text_layer);
  if (s_date_text_layer) text_layer_destroy(s_date_text_layer);

  if (s_speaking_canvas_layer) {
    layer_destroy(s_speaking_canvas_layer);
    s_speaking_canvas_layer = NULL;
  }
}

void show_speaking_graphic() {
  light_enable(false);
  if (!s_speaking_window) {
    s_speaking_window = window_create();
    window_set_click_config_provider(s_speaking_window, speaking_click_config_provider);
    window_set_window_handlers(s_speaking_window, (WindowHandlers) { .load = speaking_window_load, .appear = speaking_window_appear, .disappear = speaking_window_disappear, .unload = speaking_window_unload });
  }
  window_stack_push(s_speaking_window, true);
}

void hide_speaking_graphic() {
  if (s_speaking_window) window_stack_remove(s_speaking_window, true);
}

void test_audio_callback(int index, void *context) { show_speaking_graphic(); trigger_playback(false); }

// -----------------------------------------------------------------------------
// MAIN MENU WINDOW
// -----------------------------------------------------------------------------
static void menu_window_load(Window *window) {
  build_menu_items();

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_main_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_main_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = main_get_num_sections_callback,
    .get_num_rows = main_get_num_rows_callback,
    .get_cell_height = menu_get_cell_height_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = main_draw_header_callback,
    .draw_row = main_draw_row_callback,
  });

  window_set_click_config_provider(window, main_ccp);
  layer_add_child(window_layer, menu_layer_get_layer(s_main_menu_layer));

  #if defined(PBL_COLOR)
  menu_layer_set_normal_colors(s_main_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_main_menu_layer, GColorCobaltBlue, GColorWhite);
  #endif
}

static void menu_window_appear(Window *window) {
  #ifdef PBL_TOUCH
  if (touch_service_is_enabled()) touch_service_subscribe(menu_touch_handler, NULL);
  #endif
}

static void menu_window_disappear(Window *window) {
  #ifdef PBL_TOUCH
  touch_service_unsubscribe();
  #endif
}

static void menu_window_unload(Window *window) {
  #ifdef PBL_TOUCH
  kill_kinetic_timer();
  #endif

  if (s_main_menu_layer) {
    menu_layer_destroy(s_main_menu_layer);
    s_main_menu_layer = NULL;
  }
  s_main_edit_row = -1;

  // Final catch-all to guarantee data state is stored when backing out of the app
  save_settings();
}

void settings_window_push() {
  if (!s_menu_window) {
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) { .load = menu_window_load, .appear = menu_window_appear, .disappear = menu_window_disappear, .unload = menu_window_unload });
  }
  window_stack_push(s_menu_window, true);
}

void settings_deinit() {
  if (s_tuner_window) { window_destroy(s_tuner_window); s_tuner_window = NULL; }
  if (s_confirm_window) { window_destroy(s_confirm_window); s_confirm_window = NULL; }
  if (s_about_window) { window_destroy(s_about_window); s_about_window = NULL; }
  if (s_speaking_window) { window_destroy(s_speaking_window); s_speaking_window = NULL; }
  if (s_menu_window) { window_destroy(s_menu_window); s_menu_window = NULL; }
}
