/*
 * WhisperClock - Settings Engine Implementation
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include <pebble.h>
#include "settings_engine.h"
#include <stdlib.h>
#include "gesture_engine.h"

#define GESTURE_PERSIST_KEY 2
#define SETTINGS_PERSIST_KEY 3

WhisperSettings s_settings;

static Window *s_menu_window = NULL;
static SimpleMenuLayer *s_simple_menu_layer = NULL;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[20]; // Expanded to hold all items dynamically

// Static text buffers for dynamic menu labels
static char s_volume_text[16];
static char s_speed_text[24];
static char s_trim_text[24];
static char s_tap_count_text[16];
static char s_quiet_start_text[24];
static char s_quiet_end_text[24];

static Window *s_about_window = NULL;
static ScrollLayer *s_about_scroll_layer = NULL;
static TextLayer *s_about_text_layer = NULL;

static Window *s_confirm_window = NULL;
static TextLayer *s_confirm_text_layer = NULL;

// External Audio Engine Hooks
extern void trigger_playback(bool auto_exit);
extern void cancel_playback(void);
static void build_menu_items(void);

/**
 * @brief Saves the current settings to persistent storage.
 */
static void save_settings() {
  persist_write_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(WhisperSettings));
}

/**
 * @brief Helper to force Pebble to recalculate the scroll boundary
 * when the accordion menu expands or collapses.
 */
static void update_menu_and_save() {
  save_settings();
  build_menu_items();
  if (s_simple_menu_layer) {
    MenuLayer *internal_menu = simple_menu_layer_get_menu_layer(s_simple_menu_layer);
    menu_layer_reload_data(internal_menu);
  }
}

void settings_init() {
  // Establish safe defaults
  s_settings.say_its = true;
  s_settings.playback_speed = 50;
  s_settings.gesture_buffer_size = 25;
  s_settings.clock_mode = 0;
  s_settings.say_ampm = true;
  s_settings.volume = 60;
  s_settings.clip_trim = 0;
  s_settings.respect_quiet_time = true;
  s_settings.trigger_mode = 0;
  s_settings.tap_count = 3;
  s_settings.quiet_start_hour = 22;
  s_settings.quiet_end_hour = 7;
  s_settings.enable_beta_features = false;

  // Load user preferences, falling back gracefully if version mismatch occurs
  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    int bytes_read = persist_read_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(WhisperSettings));

    // Bounds checking & memory safety if reading an older version of the struct
    if (bytes_read < (int)sizeof(WhisperSettings)) {
      s_settings.enable_beta_features = false;
    }

    // Sanitize loaded values to prevent out-of-bounds crashes
    if (s_settings.volume < 1 || s_settings.volume > 100) s_settings.volume = 60;
    if (s_settings.playback_speed < 50 || s_settings.playback_speed > 500) s_settings.playback_speed = 50;
    if (s_settings.clip_trim < 0 || s_settings.clip_trim > 450) s_settings.clip_trim = 0;
    if (s_settings.trigger_mode > 2) s_settings.trigger_mode = 0;
    if (s_settings.tap_count < 2 || s_settings.tap_count > 5) s_settings.tap_count = 3;
    if (s_settings.quiet_start_hour > 23) s_settings.quiet_start_hour = 22;
    if (s_settings.quiet_end_hour > 23) s_settings.quiet_end_hour = 7;
  }
}

// -----------------------------------------------------
// CALLBACK FUNCTIONS (All using the unified UI helper)
// -----------------------------------------------------

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

  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorDukeBlue, GColorWhite));
  text_layer_set_text_color(s_confirm_text_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
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
  if (s_settings.clock_mode > 2) s_settings.clock_mode = 0;
  update_menu_and_save();
}

static void toggle_quiet_time_callback(int index, void *context) {
  s_settings.respect_quiet_time = !s_settings.respect_quiet_time;
  update_menu_and_save();
}

static void change_quiet_start_callback(int index, void *context) {
  s_settings.quiet_start_hour = (s_settings.quiet_start_hour + 1) % 24;
  update_menu_and_save();
}

static void change_quiet_end_callback(int index, void *context) {
  s_settings.quiet_end_hour = (s_settings.quiet_end_hour + 1) % 24;
  update_menu_and_save();
}

static void toggle_trigger_mode_callback(int index, void *context) {
  s_settings.trigger_mode++;
  if (s_settings.trigger_mode > 2) s_settings.trigger_mode = 0;
  update_menu_and_save();
}

static void change_tap_count_callback(int index, void *context) {
  s_settings.tap_count++;
  if (s_settings.tap_count > 5) s_settings.tap_count = 2;
  update_menu_and_save();
}

static void record_gesture_callback(int index, void *context) { gesture_start_recording(); }
static void clear_gesture_callback(int index, void *context) { push_reset_confirmation_window(); }

static void change_record_time_callback(int index, void *context) {
  if (s_settings.gesture_buffer_size == 25) { s_settings.gesture_buffer_size = 30; }
  else if (s_settings.gesture_buffer_size == 30) { s_settings.gesture_buffer_size = 40; }
  else if (s_settings.gesture_buffer_size == 40) { s_settings.gesture_buffer_size = 20; }
  else { s_settings.gesture_buffer_size = 25; }
  update_menu_and_save();
}

static void toggle_its_callback(int index, void *context) {
  s_settings.say_its = !s_settings.say_its;
  update_menu_and_save();
}

static void toggle_ampm_callback(int index, void *context) {
  s_settings.say_ampm = !s_settings.say_ampm;
  update_menu_and_save();
}

static void change_volume_callback(int index, void *context) {
  if (s_settings.volume >= 1 && s_settings.volume < 5) { s_settings.volume += 1; }
  else if (s_settings.volume >= 5 && s_settings.volume < 100) { s_settings.volume = (s_settings.volume == 5) ? 10 : s_settings.volume + 10; }
  else { s_settings.volume = 1; }
  update_menu_and_save();
}

static void change_speed_callback(int index, void *context) {
  s_settings.playback_speed += 50;
  if (s_settings.playback_speed > 500) s_settings.playback_speed = 50;
  update_menu_and_save();
}

static void change_trim_callback(int index, void *context) {
  s_settings.clip_trim += 50;
  if (s_settings.clip_trim > 450) s_settings.clip_trim = 0;
  update_menu_and_save();
}

// -----------------------------------------------------------------------------
// SPEAKING UI OVERLAY
// -----------------------------------------------------------------------------
static Window *s_speaking_window = NULL;
static TextLayer *s_time_text_layer = NULL;
static TextLayer *s_speaking_text_layer = NULL;
static Layer *s_speaking_canvas_layer = NULL;
static GPath *s_arrow_path = NULL;

static char s_time_buffer[16];
static const char *s_current_time_format;

// GPath definition for the arrow
static const GPathInfo ARROW_PATH_INFO = {
  .num_points = 3,
  .points = (GPoint []) {{-10, -10}, {0, 0}, {-10, 10}}
};

static void speaking_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Render the arrow on the right edge, pointing to the UP button (y ~ 44)
  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_move_to(s_arrow_path, GPoint(bounds.size.w - 8, 44));
  gpath_draw_filled(ctx, s_arrow_path);

  // Draw the "Settings" text with an added 15px buffer to the left of the arrow
  graphics_context_set_text_color(ctx, GColorBlack);

  // Shifted from -75 to -90 to pull it safely away from the arrow's tail.
  // Box width increased to 65 to ensure the word "Settings" fits comfortably.
  GRect text_bounds = GRect(bounds.size.w - 90, 34, 65, 20);
  graphics_draw_text(ctx, "Settings",
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     text_bounds,
                     GTextOverflowModeWordWrap,
                     GTextAlignmentRight,
                     NULL);
}

static void speaking_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  strftime(s_time_buffer, sizeof(s_time_buffer), s_current_time_format, tick_time);
  text_layer_set_text(s_time_text_layer, s_time_buffer);
}

static void speaking_click_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_playback(); // Halt audio engine immediately
  hide_speaking_graphic();
}

// UP button will cancel audio and immediately open settings
static void speaking_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_playback();
  hide_speaking_graphic();
  settings_window_push();
}

static void speaking_click_config_provider(void *context) {
  // Bind the UP button to open settings
  window_single_click_subscribe(BUTTON_ID_UP, speaking_up_click_handler);

  // Bind the rest of the buttons to simply cancel playback
  window_single_click_subscribe(BUTTON_ID_SELECT, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, speaking_click_handler);
}

static void speaking_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  #if defined(PBL_RGB_BACKLIGHT)
  light_set_color(GColorBlack); // Stealth mode: disable backlight if possible
  #endif

  // Initialize the canvas and the arrow GPath
  s_arrow_path = gpath_create(&ARROW_PATH_INFO);
  s_speaking_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_speaking_canvas_layer, speaking_canvas_update_proc);
  layer_add_child(window_layer, s_speaking_canvas_layer);

  s_time_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 45, bounds.size.w, 50));
  text_layer_set_background_color(s_time_text_layer, GColorClear);
  text_layer_set_text_color(s_time_text_layer, GColorBlack);
  text_layer_set_font(s_time_text_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_text_alignment(s_time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_text_layer));

  s_speaking_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 + 5, bounds.size.w, 30));
  text_layer_set_text(s_speaking_text_layer, "Speaking...");
  text_layer_set_font(s_speaking_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_speaking_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_speaking_text_layer));
}

static void speaking_window_appear(Window *window) {
  bool is_military;
  if (s_settings.clock_mode == 1) is_military = false;
  else if (s_settings.clock_mode == 2) is_military = true;
  else is_military = clock_is_24h_style();

  s_current_time_format = is_military ? "%H:%M:%S" : "%I:%M:%S";
  time_t temp = time(NULL); struct tm *tick_time = localtime(&temp);
  speaking_tick_handler(tick_time, SECOND_UNIT);
  tick_timer_service_subscribe(SECOND_UNIT, speaking_tick_handler);
}

static void speaking_window_disappear(Window *window) {
  tick_timer_service_unsubscribe();
}

static void speaking_window_unload(Window *window) {
  #if defined(PBL_RGB_BACKLIGHT)
  light_set_system_color(); // Restore system backlight settings
  #endif
  if (s_time_text_layer) text_layer_destroy(s_time_text_layer);
  if (s_speaking_text_layer) text_layer_destroy(s_speaking_text_layer);

  if (s_arrow_path) {
    gpath_destroy(s_arrow_path);
    s_arrow_path = NULL;
  }
  if (s_speaking_canvas_layer) {
    layer_destroy(s_speaking_canvas_layer);
    s_speaking_canvas_layer = NULL;
  }
}

void show_speaking_graphic() {
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

static void show_about_callback(int index, void *context);
static void test_audio_callback(int index, void *context) { show_speaking_graphic(); trigger_playback(false); }

// -----------------------------------------------------
// DYNAMIC MENU BUILDER (ACCORDION LOGIC)
// -----------------------------------------------------
static void build_menu_items() {
  snprintf(s_volume_text, sizeof(s_volume_text), "Level: %d%%", s_settings.volume);
  snprintf(s_speed_text, sizeof(s_speed_text), "Interval: %d ms", s_settings.playback_speed);
  snprintf(s_trim_text, sizeof(s_trim_text), "Trim End: %d ms", s_settings.clip_trim);

  char *clock_text = "Auto (Watch Setting)";
  if (s_settings.clock_mode == 1) clock_text = "Force 12-Hour";
  if (s_settings.clock_mode == 2) clock_text = "Force 24-Hour";

  int i = 0; // Dynamic indexer to prevent empty rows when accordion is closed

  s_menu_items[i++] = (SimpleMenuItem) {
    .title = "Beta Features",
    .subtitle = s_settings.enable_beta_features ? "ON: Gestures & Tapping" : "OFF: Quick Launch Only",
    .callback = toggle_beta_callback
  };

  s_menu_items[i++] = (SimpleMenuItem) { .title = "Clock Mode", .subtitle = clock_text, .callback = toggle_clock_mode_callback };

  // Expand the accordion if Beta is ON
  if (s_settings.enable_beta_features) {
    char *trigger_text = "Gesture Sweep";
    if (s_settings.trigger_mode == 1) trigger_text = "Glass Tapping";
    if (s_settings.trigger_mode == 2) trigger_text = "Gesture or Tap";

    char *time_text = "1.0 Second (Default)";
    if (s_settings.gesture_buffer_size == 20) time_text = "0.8 Seconds";
    if (s_settings.gesture_buffer_size == 30) time_text = "1.2 Seconds";
    if (s_settings.gesture_buffer_size == 40) time_text = "1.6 Seconds";

    snprintf(s_tap_count_text, sizeof(s_tap_count_text), "%d Taps", s_settings.tap_count);
    snprintf(s_quiet_start_text, sizeof(s_quiet_start_text), "Sleep Start: %02d:00", s_settings.quiet_start_hour);
    snprintf(s_quiet_end_text, sizeof(s_quiet_end_text), "Sleep End: %02d:00", s_settings.quiet_end_hour);

    s_menu_items[i++] = (SimpleMenuItem) { .title = "Quiet Time", .subtitle = s_settings.respect_quiet_time ? "Mute & Worker Sleep" : "Always Speak", .callback = toggle_quiet_time_callback };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Worker Sleep Start", .subtitle = s_quiet_start_text, .callback = change_quiet_start_callback };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Worker Sleep End", .subtitle = s_quiet_end_text, .callback = change_quiet_end_callback };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Trigger Method", .subtitle = trigger_text, .callback = toggle_trigger_mode_callback };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Knock Count", .subtitle = s_tap_count_text, .callback = change_tap_count_callback };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Record Gesture", .subtitle = "Train your watch", .callback = record_gesture_callback };

    char *clear_text = persist_exists(GESTURE_PERSIST_KEY) ? "Revert to default flick" : "Already using default";
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Clear Gesture", .subtitle = clear_text, .callback = clear_gesture_callback };
    s_menu_items[i++] = (SimpleMenuItem) { .title = "Recording Time", .subtitle = time_text, .callback = change_record_time_callback };
  }

  // Standard items appended after the accordion block
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Prefix: \"It's\"", .subtitle = s_settings.say_its ? "Enabled" : "Disabled", .callback = toggle_its_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Speak AM/PM", .subtitle = s_settings.say_ampm ? "Enabled" : "Disabled", .callback = toggle_ampm_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Speaker Volume", .subtitle = s_volume_text, .callback = change_volume_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Voice Interval", .subtitle = s_speed_text, .callback = change_speed_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Audio Trim", .subtitle = s_trim_text, .callback = change_trim_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "Test Audio", .subtitle = "Preview time speaking", .callback = test_audio_callback };
  s_menu_items[i++] = (SimpleMenuItem) { .title = "About / Help", .subtitle = "Instructions & Info", .callback = show_about_callback };

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

/**
 * @brief Safely applies a vertical scroll delta while enforcing physical layer boundaries.
 */
static void apply_clamped_scroll(ScrollLayer *layer, int16_t delta_y) {
  GPoint offset = scroll_layer_get_content_offset(layer);
  offset.y += delta_y;

  int content_h = scroll_layer_get_content_size(layer).h;
  int layer_h = layer_get_bounds(scroll_layer_get_layer(layer)).size.h;
  int max_scroll = -(content_h - layer_h);
  if (max_scroll > 0) max_scroll = 0; // Prevent upward overscroll if content is small

  if (offset.y > 0) offset.y = 0;
  if (offset.y < max_scroll) offset.y = max_scroll;

  scroll_layer_set_content_offset(layer, offset, false);
}

/**
 * @brief The friction loop simulating physical momentum for touch interfaces.
 * Decays velocity by 10% each frame (25ms).
 */
static void kinetic_timer_callback(void *data) {
  ScrollLayer *scroll_layer = (ScrollLayer *)data;
  if (!scroll_layer) return;

  GPoint old_offset = scroll_layer_get_content_offset(scroll_layer);
  apply_clamped_scroll(scroll_layer, s_scroll_velocity);
  GPoint new_offset = scroll_layer_get_content_offset(scroll_layer);

  // Apply friction
  s_scroll_velocity = (s_scroll_velocity * 90) / 100;

  // Stop if velocity is dead or we hit a physical boundary
  if (abs(s_scroll_velocity) <= 1 || old_offset.y == new_offset.y) {
    s_scroll_velocity = 0;
    s_kinetic_timer = NULL;
  } else {
    // Run at ~40 FPS for fluid motion
    s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, scroll_layer);
  }
}

static void menu_touch_handler(const TouchEvent *event, void *context) {
  MenuLayer *menu_layer = simple_menu_layer_get_menu_layer(s_simple_menu_layer);
  ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(menu_layer);

  if (event->type == TouchEvent_Touchdown) {
    s_touch_start_y = event->y;
    s_touch_last_y = event->y;
    s_is_drag = false;

    // Tap to kill momentum
    s_scroll_velocity = 0;
    kill_kinetic_timer();
  }
  else if (event->type == TouchEvent_PositionUpdate) {
    if (!s_is_drag && abs(event->y - s_touch_start_y) > 15) {
      s_is_drag = true;
    }

    if (s_is_drag) {
      int16_t delta_y = event->y - s_touch_last_y;
      s_scroll_velocity = delta_y; // Track raw finger velocity
      apply_clamped_scroll(scroll_layer, delta_y);
      s_touch_last_y = event->y;
    }
  }
  else if (event->type == TouchEvent_Liftoff) {
    if (!s_is_drag) {
      // Direct Tap Logic
      GPoint offset = scroll_layer_get_content_offset(scroll_layer);
      int16_t content_y = event->y - offset.y;
      int total_rows = s_menu_sections[0].num_items;

      // Correct for the 16px Section Header offset built into SimpleMenuLayer
      int16_t header_height = 16;

      if (content_y >= header_height) {
        int content_h = scroll_layer_get_content_size(scroll_layer).h;
        int row_height = (content_h - header_height) / total_rows;
        int tapped_row = (content_y - header_height) / row_height;

        if (tapped_row >= 0 && tapped_row < total_rows) {
          MenuIndex current_selection = menu_layer_get_selected_index(menu_layer);

          if (current_selection.row == tapped_row) {
            if (s_menu_items[tapped_row].callback != NULL) {
              s_menu_items[tapped_row].callback(tapped_row, NULL);
            }
          } else {
            MenuIndex new_selection = (MenuIndex){.section = 0, .row = tapped_row};
            menu_layer_set_selected_index(menu_layer, new_selection, MenuRowAlignCenter, true);
          }
        }
      }
    } else {
      // Finger lift: trigger physics engine if throwing fast enough
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

// -----------------------------------------------------------------------------
// ABOUT WINDOW
// -----------------------------------------------------------------------------
static void about_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_about_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_about_scroll_layer, window);

  const char *about_text =
  "WhisperClock v0.9 RC1\n\n"
  "A stealthy, spoken time check for your Pebble.\n\n"
  "--- HOW TO USE ---\n"
  "1. Map WhisperClock to 'Quick Launch' in your Pebble settings.\n"
  "2. Hold that button from your watchface to hear the time.\n"
  "(Press any button to instantly cancel audio).\n\n"
  "--- AUDIO CONTROLS ---\n"
  "Customize your playback in the main menu:\n"
  "- Volume: 1% to 100%\n"
  "- Interval: Adjust the pause between spoken words.\n"
  "- Trim: Cut the silent tails off audio clips for a punchier sentence.\n"
  "- Prefix: Toggle \"It's\" and \"AM/PM\".\n\n"
  "--- BETA PHYSICS ---\n"
  "Turn on 'Beta Features' to trigger the watch hands-free using custom wrist-flicks or glass tapping!\n\n"
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
  kill_kinetic_timer(); // Safety: prevent segfaults if user hits back button while gliding!
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
// MAIN MENU WINDOW
// -----------------------------------------------------------------------------
static void menu_window_load(Window *window) {
  // Suspend worker polling while user is configuring settings to save CPU
  app_worker_kill();

  build_menu_items();

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_simple_menu_layer = simple_menu_layer_create(bounds, window, s_menu_sections, 1, NULL);
  menu_layer_set_click_config_onto_window(simple_menu_layer_get_menu_layer(s_simple_menu_layer), window);
  layer_add_child(window_layer, simple_menu_layer_get_layer(s_simple_menu_layer));
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
  if (s_simple_menu_layer) simple_menu_layer_destroy(s_simple_menu_layer);

  // Re-launch physics engine upon exit if Beta is active
  if (s_settings.enable_beta_features) {
    app_worker_launch();
  }
}

void settings_window_push() {
  if (!s_menu_window) {
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) { .load = menu_window_load, .appear = menu_window_appear, .disappear = menu_window_disappear, .unload = menu_window_unload });
  }
  window_stack_push(s_menu_window, true);
}

void settings_deinit() {
  if (s_confirm_window) { window_destroy(s_confirm_window); s_confirm_window = NULL; }
  if (s_about_window) { window_destroy(s_about_window); s_about_window = NULL; }
  if (s_speaking_window) { window_destroy(s_speaking_window); s_speaking_window = NULL; }
  if (s_menu_window) { window_destroy(s_menu_window); s_menu_window = NULL; }
}
