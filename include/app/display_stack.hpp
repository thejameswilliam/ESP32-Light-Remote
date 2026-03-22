#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

struct esp_lcd_touch_s;
typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;
typedef struct _lv_indev_t lv_indev_t;

namespace app {

// Shared handles for the display, touch controller, and expander-backed peripherals.
struct DisplayStack {
    lv_display_t *display = nullptr;
    lv_indev_t *touch_indev = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t touch_io = nullptr;
    esp_lcd_touch_handle_t touch = nullptr;
    i2c_master_bus_handle_t i2c_bus = nullptr;
    i2c_master_dev_handle_t expander = nullptr;
};

// Initializes the LCD panel, LVGL display, touch controller, and expander-backed outputs.
esp_err_t initialize_display(DisplayStack &stack);

// Plays the short preset-save confirmation chirp on the board buzzer.
esp_err_t play_buzzer_chirp(const DisplayStack &stack, uint32_t duration_ms = 35);

} // namespace app
