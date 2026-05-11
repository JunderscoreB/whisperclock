#include "settings_engine.h"
#include "gesture_engine.h" 
#include <stdlib.h> 

#define GESTURE_PERSIST_KEY 2 
#define SETTINGS_PERSIST_KEY 3

WhisperSettings s_settings;

static Window *s_menu_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[12];

static char s_volume_text[32];
static char s_speed_text[32]; 
static char s_trim_text[32]; 

static Window *s_about_window;
static ScrollLayer *s_about_scroll_layer;
static TextLayer *s_about_text_layer;

extern void trigger_playback(bool auto_exit);
extern void cancel_playback(void);

static void save_settings() {
  persist_write_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(WhisperSettings));
}

void settings_init() {
  s_settings.say_its = true;
  s_settings.playback_speed = 50; // NEW DEFAULT: 50 ms
  s_settings.gesture_buffer_size = 25; 
  s_settings.clock_mode = 0; 
  s_settings.say_ampm = true; 
  s_settings.volume = 60; 
  s_settings.clip_trim = 0; 
  s_settings.respect_quiet_time = true;

  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    persist_read_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(WhisperSettings));
    
    // NEW BOUNDS: 50 ms to 500 ms
    if (s_settings.volume < 10 || s_settings.volume > 100) s_settings.volume = 60;
    if (s_settings.playback_speed < 50 || s_settings.playback_speed > 500) s_settings.playback_speed = 50;
    if (s_settings.clip_trim < 0 || s_settings.clip_trim > 450) s_settings.clip_trim = 0; 
    if (s_settings.respect_quiet_time != true && s_settings.respect_quiet_time != false) {
      s_settings.respect_quiet_time = true; 
    }
  }
}

// --- Main Menu Callbacks ---
static void toggle_clock_mode_callback(int index, void *context) {
  s_settings.clock_mode++;
  if (s_settings.clock_mode > 2) s_settings.clock_mode = 0;

  if (s_settings.clock_mode == 0) s_menu_items[0].subtitle = "Auto (Watch Setting)";
  else if (s_settings.clock_mode == 1) s_menu_items[0].subtitle = "Force 12-Hour";
  else s_menu_items[0].subtitle = "Force 24-Hour";
  
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void record_gesture_callback(int index, void *context) { gesture_start_recording(); }

static void clear_gesture_callback(int index, void *context) {
  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_delete(GESTURE_PERSIST_KEY);
    vibes_double_pulse();
    s_menu_items[2].subtitle = "Cleared! (Using Default)"; 
  } else {
    s_menu_items[2].subtitle = "Already using default";
  }
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
}

static void change_record_time_callback(int index, void *context) {
  if (s_settings.gesture_buffer_size == 25) {
    s_settings.gesture_buffer_size = 30; s_menu_items[3].subtitle = "3.0 Seconds"; 
  } else if (s_settings.gesture_buffer_size == 30) {
    s_settings.gesture_buffer_size = 40; s_menu_items[3].subtitle = "4.0 Seconds";
  } else if (s_settings.gesture_buffer_size == 40) {
    s_settings.gesture_buffer_size = 20; s_menu_items[3].subtitle = "2.0 Seconds";
  } else {
    s_settings.gesture_buffer_size = 25; s_menu_items[3].subtitle = "2.5 Seconds (Default)";
  }
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void toggle_its_callback(int index, void *context) {
  s_settings.say_its = !s_settings.say_its;
  s_menu_items[4].subtitle = s_settings.say_its ? "Enabled" : "Disabled"; 
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void toggle_ampm_callback(int index, void *context) {
  s_settings.say_ampm = !s_settings.say_ampm;
  s_menu_items[5].subtitle = s_settings.say_ampm ? "Enabled" : "Disabled"; 
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void change_volume_callback(int index, void *context) {
  s_settings.volume += 10;
  if (s_settings.volume > 100) s_settings.volume = 10;
  
  snprintf(s_volume_text, sizeof(s_volume_text), "Level: %d%%", s_settings.volume);
  s_menu_items[6].subtitle = s_volume_text;
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void change_speed_callback(int index, void *context) {
  s_settings.playback_speed += 50; 
  if (s_settings.playback_speed > 500) s_settings.playback_speed = 50;
  
  snprintf(s_speed_text, sizeof(s_speed_text), "Interval: %d ms", s_settings.playback_speed);
  s_menu_items[7].subtitle = s_speed_text;
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void change_trim_callback(int index, void *context) {
  s_settings.clip_trim += 50; 
  if (s_settings.clip_trim > 450) s_settings.clip_trim = 0; 
  
  snprintf(s_trim_text, sizeof(s_trim_text), "Trim End: %d ms", s_settings.clip_trim);
  s_menu_items[8].subtitle = s_trim_text;
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

static void toggle_quiet_time_callback(int index, void *context) {
  s_settings.respect_quiet_time = !s_settings.respect_quiet_time;
  s_menu_items[9].subtitle = s_settings.respect_quiet_time ? "Mute during DND" : "Always Speak"; 
  layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
  save_settings();
}

// ==========================================
// SPEAKING GRAPHIC UI (With Giant Time)
// ==========================================
static Window *s_speaking_window = NULL;
static TextLayer *s_time_text_layer;
static TextLayer *s_speaking_text_layer;
static char s_time_buffer[16]; 

static void speaking_click_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_playback();
}

static void speaking_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, speaking_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, speaking_click_handler);
}

#ifdef PBL_TOUCH
static void speaking_touch_handler(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) cancel_playback();
}
#endif

static void speaking_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

#if defined(PBL_RGB_BACKLIGHT)
  light_set_color(GColorBlack);
#endif

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  bool is_military;
  if (s_settings.clock_mode == 1) is_military = false; 
  else if (s_settings.clock_mode == 2) is_military = true; 
  else is_military = clock_is_24h_style(); 

  // Format only the time text
  if (is_military) strftime(s_time_buffer, sizeof(s_time_buffer), "%H:%M", t);
  else strftime(s_time_buffer, sizeof(s_time_buffer), "%I:%M", t);

  // The Giant Time Layer
  s_time_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 45, bounds.size.w, 50));
  text_layer_set_text(s_time_text_layer, s_time_buffer);
  text_layer_set_font(s_time_text_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_text_layer));

  // The "Speaking..." Subtitle Layer
  s_speaking_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 + 5, bounds.size.w, 30));
  text_layer_set_text(s_speaking_text_layer, "Speaking...");
  text_layer_set_font(s_speaking_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_speaking_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_speaking_text_layer));
}

static void speaking_window_appear(Window *window) {
#ifdef PBL_TOUCH
  if (touch_service_is_enabled()) touch_service_subscribe(speaking_touch_handler, NULL);
#endif
}

static void speaking_window_disappear(Window *window) {
#ifdef PBL_TOUCH
  touch_service_unsubscribe();
#endif
}

static void speaking_window_unload(Window *window) {
#if defined(PBL_RGB_BACKLIGHT)
  light_set_system_color();
#endif
  text_layer_destroy(s_time_text_layer);
  text_layer_destroy(s_speaking_text_layer);
  window_destroy(s_speaking_window);
  s_speaking_window = NULL;
}

void show_speaking_graphic() {
  if (!s_speaking_window) {
    s_speaking_window = window_create();
    window_set_click_config_provider(s_speaking_window, speaking_click_config_provider);
    window_set_window_handlers(s_speaking_window, (WindowHandlers) {
      .load = speaking_window_load,
      .appear = speaking_window_appear,
      .disappear = speaking_window_disappear,
      .unload = speaking_window_unload,
    });
  }
  window_stack_push(s_speaking_window, true);
}

void hide_speaking_graphic() {
  if (s_speaking_window) window_stack_remove(s_speaking_window, true);
}

static void test_audio_callback(int index, void *context) { 
  show_speaking_graphic();
  trigger_playback(false); 
}

// ==========================================
// TOUCHSCREEN TRANSLATOR LOGIC 
// ==========================================

#ifdef PBL_TOUCH
static int16_t s_touch_start_y = 0;
static int16_t s_touch_last_y = 0;
static bool s_is_drag = false;

static void menu_touch_handler(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) {
    s_touch_start_y = event->y;
    s_touch_last_y = event->y;
    s_is_drag = false;
  } 
  else if (event->type == TouchEvent_PositionUpdate) {
    if (!s_is_drag && abs(event->y - s_touch_start_y) > 15) {
      s_is_drag = true;
    }
    
    if (s_is_drag) {
      int16_t delta_y = event->y - s_touch_last_y;
      MenuLayer *menu_layer = simple_menu_layer_get_menu_layer(s_simple_menu_layer);
      ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(menu_layer);
      
      GPoint offset = scroll_layer_get_content_offset(scroll_layer);
      offset.y += delta_y;
      
      int content_h = scroll_layer_get_content_size(scroll_layer).h;
      int layer_h = layer_get_bounds(scroll_layer_get_layer(scroll_layer)).size.h;
      int max_scroll = -(content_h - layer_h);
      if (max_scroll > 0) max_scroll = 0;
      
      if (offset.y > 0) offset.y = 0;
      if (offset.y < max_scroll) offset.y = max_scroll;
      
      scroll_layer_set_content_offset(scroll_layer, offset, false);
      s_touch_last_y = event->y;
    }
  } 
  else if (event->type == TouchEvent_Liftoff) {
    if (!s_is_drag) {
      MenuLayer *menu_layer = simple_menu_layer_get_menu_layer(s_simple_menu_layer);
      ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(menu_layer);
      
      GPoint offset = scroll_layer_get_content_offset(scroll_layer);
      int16_t content_y = event->y - offset.y; 
      
      int total_rows = s_menu_sections[0].num_items;
      int row_height = scroll_layer_get_content_size(scroll_layer).h / total_rows;
      int tapped_row = content_y / row_height;

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
  } 
  else if (event->type == TouchEvent_PositionUpdate) {
    if (!s_about_is_drag && abs(event->y - s_about_touch_start_y) > 15) {
      s_about_is_drag = true;
    }

    if (s_about_is_drag) {
      int16_t delta_y = event->y - s_about_touch_last_y;
      GPoint offset = scroll_layer_get_content_offset(s_about_scroll_layer);
      offset.y += delta_y;
      
      int content_h = scroll_layer_get_content_size(s_about_scroll_layer).h;
      int layer_h = layer_get_bounds(scroll_layer_get_layer(s_about_scroll_layer)).size.h;
      int max_scroll = -(content_h - layer_h);
      if (max_scroll > 0) max_scroll = 0;
      
      if (offset.y > 0) offset.y = 0;
      if (offset.y < max_scroll) offset.y = max_scroll;
      
      scroll_layer_set_content_offset(s_about_scroll_layer, offset, false);
      s_about_touch_last_y = event->y;
    }
  } 
  else if (event->type == TouchEvent_Liftoff) {
    if (!s_about_is_drag) window_stack_pop(true);
  }
}
#endif

// --- About Menu Window ---
static void about_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_about_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_about_scroll_layer, window);

 const char *about_text = 
    "WhisperClock\n\n"
    "Lift the watch to your ear for a private, spoken time check.\n\n"
    "How to Use:\n"
    "- Flick wrist to hear the time.\n"
    "- Tap any button or the screen to instantly stop audio.\n\n"
    "Key Features:\n"
    "- Record Gesture: Train a custom arm motion to replace the flick.\n"
    "- Quiet Time: Automatically mutes during Do Not Disturb.\n"
    "- Audio Trim: Cuts dead space for punchier playback.\n"
    "- Voice Interval: Adjusts the reading speed.";

  s_about_text_layer = text_layer_create(GRect(0, 10, bounds.size.w, 3000));
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
  text_layer_destroy(s_about_text_layer);
  scroll_layer_destroy(s_about_scroll_layer);
  window_destroy(s_about_window);
}

static void show_about_callback(int index, void *context) {
  s_about_window = window_create();
  window_set_window_handlers(s_about_window, (WindowHandlers) {
    .load = about_window_load,
    .appear = about_window_appear,
    .disappear = about_window_disappear,
    .unload = about_window_unload,
  });
  window_stack_push(s_about_window, true);
}

// --- Main Menu Window ---
static void menu_window_load(Window *window) {
  app_worker_kill();

  char *clock_text = "Auto (Watch Setting)";
  if (s_settings.clock_mode == 1) clock_text = "Force 12-Hour";
  if (s_settings.clock_mode == 2) clock_text = "Force 24-Hour";

  s_menu_items[0] = (SimpleMenuItem) { .title = "Clock Mode", .subtitle = clock_text, .callback = toggle_clock_mode_callback };
  s_menu_items[1] = (SimpleMenuItem) { .title = "Record Gesture", .subtitle = "Train your watch", .callback = record_gesture_callback };
  s_menu_items[2] = (SimpleMenuItem) { .title = "Clear Gesture", .subtitle = "Revert to default flick", .callback = clear_gesture_callback };
  
  char *time_text = "2.5 Seconds (Default)";
  if (s_settings.gesture_buffer_size == 20) time_text = "2.0 Seconds";
  if (s_settings.gesture_buffer_size == 30) time_text = "3.0 Seconds";
  if (s_settings.gesture_buffer_size == 40) time_text = "4.0 Seconds";

  s_menu_items[3] = (SimpleMenuItem) { .title = "Recording Time", .subtitle = time_text, .callback = change_record_time_callback };
  s_menu_items[4] = (SimpleMenuItem) { .title = "Prefix: \"It's\"", .subtitle = s_settings.say_its ? "Enabled" : "Disabled", .callback = toggle_its_callback };
  s_menu_items[5] = (SimpleMenuItem) { .title = "Speak AM/PM", .subtitle = s_settings.say_ampm ? "Enabled" : "Disabled", .callback = toggle_ampm_callback };
  
  snprintf(s_volume_text, sizeof(s_volume_text), "Level: %d%%", s_settings.volume);
  s_menu_items[6] = (SimpleMenuItem) { .title = "Speaker Volume", .subtitle = s_volume_text, .callback = change_volume_callback };

  snprintf(s_speed_text, sizeof(s_speed_text), "Interval: %d ms", s_settings.playback_speed);
  s_menu_items[7] = (SimpleMenuItem) { .title = "Voice Interval", .subtitle = s_speed_text, .callback = change_speed_callback };

  snprintf(s_trim_text, sizeof(s_trim_text), "Trim End: %d ms", s_settings.clip_trim);
  s_menu_items[8] = (SimpleMenuItem) { .title = "Audio Trim", .subtitle = s_trim_text, .callback = change_trim_callback };

  s_menu_items[9] = (SimpleMenuItem) { .title = "Quiet Time", .subtitle = s_settings.respect_quiet_time ? "Mute during DND" : "Always Speak", .callback = toggle_quiet_time_callback };
  s_menu_items[10] = (SimpleMenuItem) { .title = "Test Audio", .subtitle = "Preview time speaking", .callback = test_audio_callback };
  s_menu_items[11] = (SimpleMenuItem) { .title = "About / Help", .subtitle = "Instructions & Info", .callback = show_about_callback };

  s_menu_sections[0] = (SimpleMenuSection) { .num_items = 12, .items = s_menu_items };

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
  simple_menu_layer_destroy(s_simple_menu_layer);
  window_destroy(s_menu_window);
  app_worker_launch();
}

void settings_window_push() {
  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers) {
    .load = menu_window_load,
    .appear = menu_window_appear,       
    .disappear = menu_window_disappear, 
    .unload = menu_window_unload,
  });
  
  window_stack_push(s_menu_window, true);
}
