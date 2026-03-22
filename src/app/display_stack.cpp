#include "app/display_stack.hpp"

#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board/supported/waveshare/BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1.h"

namespace app {
namespace {

constexpr const char *kTag = "display";

constexpr gpio_num_t kPanelSpiClockGpio = GPIO_NUM_2;
constexpr gpio_num_t kPanelSpiDataGpio = GPIO_NUM_1;
constexpr gpio_num_t kBacklightGpio = GPIO_NUM_6;
constexpr gpio_num_t kTouchInterruptGpio = GPIO_NUM_16;

constexpr uint8_t kExpanderAddress = 0x20;
constexpr uint8_t kExpanderRegInput = 0x00;
constexpr uint8_t kExpanderRegOutput = 0x01;
constexpr uint8_t kExpanderRegConfig = 0x03;

constexpr uint8_t kExpanderBitLcdReset = 0;
constexpr uint8_t kExpanderBitTouchReset = 1;
constexpr uint8_t kExpanderBitLcdCs = 2;
constexpr uint8_t kExpanderBitBuzzer = 7;

constexpr uint16_t kDisplayWidth = 480;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint32_t kPixelClockHz = 12 * 1000 * 1000;
constexpr size_t kBounceLines = 40;
constexpr size_t kBouncePixels = kDisplayWidth * kBounceLines;

struct PanelInitCommand {
    uint8_t command;
    uint8_t data[16];
    uint8_t data_size;
    uint16_t delay_ms;
};

static const PanelInitCommand kPanelInitCommands[] = {
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, {0x3B, 0x00}, 2, 0},
    {0xC1, {0x0B, 0x02}, 2, 0},
    {0xC2, {0x07, 0x02}, 2, 0},
    {0xCC, {0x10}, 1, 0},
    {0xCD, {0x08}, 1, 0},
    {0xB0, {0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 16, 0},
    {0xB1, {0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}, 16, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, {0x6D}, 1, 0},
    {0xB1, {0x37}, 1, 0},
    {0xB2, {0x81}, 1, 0},
    {0xB3, {0x80}, 1, 0},
    {0xB5, {0x43}, 1, 0},
    {0xB7, {0x85}, 1, 0},
    {0xB8, {0x20}, 1, 0},
    {0xC1, {0x78}, 1, 0},
    {0xC2, {0x78}, 1, 0},
    {0xD0, {0x88}, 1, 0},
    {0xE0, {0x00, 0x00, 0x02}, 3, 0},
    {0xE1, {0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, {0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, {0x22, 0x00}, 2, 0},
    {0xE5, {0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, {0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, {0x22, 0x00}, 2, 0},
    {0xE8, {0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xEB, {0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, {0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, {0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, {0x08}, 1, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, {0x00}, 1, 0},
    {0x3A, {0x66}, 1, 0},
    {0x11, {0x00}, 0, 480},
    {0x20, {0x00}, 0, 120},
    {0x29, {0x00}, 0, 0},
};

uint8_t s_expander_output = 0x07;

esp_err_t expander_write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t buffer[2] = {reg, value};
    return i2c_master_transmit(device, buffer, sizeof(buffer), 100);
}

esp_err_t expander_set_output(DisplayStack &stack, uint8_t bit, bool level)
{
    if (level) {
        s_expander_output |= static_cast<uint8_t>(1U << bit);
    } else {
        s_expander_output &= static_cast<uint8_t>(~(1U << bit));
    }
    return expander_write_register(stack.expander, kExpanderRegOutput, s_expander_output);
}

esp_err_t init_i2c_bus(DisplayStack &stack)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_15,
        .scl_io_num = GPIO_NUM_7,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 4,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &stack.i2c_bus), kTag, "create i2c bus failed");

    const i2c_device_config_t expander_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kExpanderAddress,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    return i2c_master_bus_add_device(stack.i2c_bus, &expander_config, &stack.expander);
}

esp_err_t bootstrap_expander(DisplayStack &stack)
{
    ESP_RETURN_ON_ERROR(expander_write_register(stack.expander, kExpanderRegConfig, 0x78), kTag, "expander dir failed");
    s_expander_output = 0x07;
    ESP_RETURN_ON_ERROR(expander_write_register(stack.expander, kExpanderRegOutput, s_expander_output), kTag, "expander out failed");

    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitLcdReset, false), kTag, "lcd reset low failed");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitLcdReset, true), kTag, "lcd reset high failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitTouchReset, false), kTag, "touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitTouchReset, true), kTag, "touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitLcdCs, true), kTag, "lcd cs idle failed");
    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitBuzzer, false), kTag, "buzzer off failed");
    return ESP_OK;
}

void panel_spi_delay()
{
    esp_rom_delay_us(1);
}

void panel_spi_clock_idle()
{
    gpio_set_level(kPanelSpiClockGpio, 0);
}

void lvgl_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    auto *touch = static_cast<esp_lcd_touch_handle_t>(lv_indev_get_driver_data(indev));
    if (touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (esp_lcd_touch_read_data(touch) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t strength = 0;
    uint8_t point_count = 0;
    const bool pressed = esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &point_count, 1);

    if (pressed && point_count > 0) {
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void IRAM_ATTR lvgl_touch_interrupt_callback(esp_lcd_touch_handle_t touch)
{
    auto *indev = static_cast<lv_indev_t *>(touch->config.user_data);
    if (indev != nullptr) {
        lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, indev);
    }
}

esp_err_t panel_spi_init_gpios()
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(kPanelSpiClockGpio) | BIT64(kPanelSpiDataGpio) | BIT64(kBacklightGpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "panel spi gpio config failed");
    gpio_set_level(kPanelSpiClockGpio, 0);
    gpio_set_level(kPanelSpiDataGpio, 0);
    gpio_set_level(kBacklightGpio, 1);
    return ESP_OK;
}

esp_err_t panel_spi_write_package(DisplayStack &stack, bool is_command, uint8_t value)
{
    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitLcdCs, false), kTag, "lcd cs low failed");
    panel_spi_delay();

    uint16_t bits = static_cast<uint16_t>(value);
    for (int bit = 8; bit >= 0; --bit) {
        const bool level = (bit == 8) ? !is_command : ((bits >> (bit)) & 0x01U);
        gpio_set_level(kPanelSpiDataGpio, level ? 1 : 0);
        panel_spi_clock_idle();
        panel_spi_delay();
        gpio_set_level(kPanelSpiClockGpio, 1);
        panel_spi_delay();
        gpio_set_level(kPanelSpiClockGpio, 0);
    }

    gpio_set_level(kPanelSpiDataGpio, 0);
    panel_spi_delay();
    return expander_set_output(stack, kExpanderBitLcdCs, true);
}

esp_err_t send_panel_init_sequence(DisplayStack &stack)
{
    for (const auto &entry : kPanelInitCommands) {
        ESP_RETURN_ON_ERROR(panel_spi_write_package(stack, true, entry.command), kTag, "panel command failed");
        for (size_t i = 0; i < entry.data_size; ++i) {
            ESP_RETURN_ON_ERROR(panel_spi_write_package(stack, false, entry.data[i]), kTag, "panel data failed");
        }
        if (entry.delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(entry.delay_ms));
        }
    }
    return ESP_OK;
}

esp_err_t init_panel(DisplayStack &stack)
{
    ESP_LOGI(kTag, "rgb timing pclk=%" PRIu32 " bounce_lines=%u bounce_px=%u", kPixelClockHz, static_cast<unsigned>(kBounceLines), static_cast<unsigned>(kBouncePixels));

    const esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = kPixelClockHz,
            .h_res = kDisplayWidth,
            .v_res = kDisplayHeight,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .vsync_pulse_width = 3,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .hsync_idle_low = 0,
                .vsync_idle_low = 0,
                .de_idle_high = 0,
                .pclk_active_neg = 0,
                .pclk_idle_high = 0,
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = kBouncePixels,
        .dma_burst_size = 64,
        .hsync_gpio_num = 38,
        .vsync_gpio_num = 39,
        .de_gpio_num = 40,
        .pclk_gpio_num = 41,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            5, 45, 48, 47, 21, 14, 13, 12,
            11, 10, 9, 46, 3, 8, 18, 17,
        },
        .flags = {
            .disp_active_low = 0,
            .refresh_on_demand = 0,
            .fb_in_psram = 1,
            .double_fb = 0,
            .no_fb = 0,
            .bb_invalidate_cache = 0,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &stack.panel), kTag, "new rgb panel failed");
    return esp_lcd_panel_init(stack.panel);
}

esp_err_t init_lvgl(DisplayStack &stack)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = 1;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), kTag, "lvgl init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = nullptr,
        .panel_handle = stack.panel,
        .control_handle = nullptr,
        .buffer_size = kDisplayWidth * kDisplayHeight,
        .double_buffer = false,
        .trans_size = 0,
        .hres = kDisplayWidth,
        .vres = kDisplayHeight,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .rounder_cb = nullptr,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = true,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    stack.display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    return stack.display != nullptr ? ESP_OK : ESP_FAIL;
}

esp_err_t init_touch(DisplayStack &stack)
{
    const esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(stack.i2c_bus, &touch_io_config, &stack.touch_io), kTag, "touch io failed");

    const esp_lcd_touch_config_t touch_config = {
        .x_max = kDisplayWidth,
        .y_max = kDisplayHeight,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = kTouchInterruptGpio,
        .levels = {
            .reset = 0,
            .interrupt = 1,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst820(stack.touch_io, &touch_config, &stack.touch), kTag, "touch init failed");
    gpio_set_pull_mode(kTouchInterruptGpio, GPIO_PULLUP_ONLY);
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    stack.touch_indev = lv_indev_create();
    if (stack.touch_indev != nullptr) {
        lv_indev_set_type(stack.touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_mode(stack.touch_indev, LV_INDEV_MODE_EVENT);
        lv_indev_set_read_cb(stack.touch_indev, lvgl_touchpad_read);
        lv_indev_set_disp(stack.touch_indev, stack.display);
        lv_indev_set_driver_data(stack.touch_indev, stack.touch);
    }

    lvgl_port_unlock();
    if (stack.touch_indev == nullptr) {
        return ESP_FAIL;
    }

    return esp_lcd_touch_register_interrupt_callback_with_data(
        stack.touch, lvgl_touch_interrupt_callback, stack.touch_indev
    );
}

} // namespace

esp_err_t initialize_display(DisplayStack &stack)
{
    ESP_LOGI(kTag, "bootstrapping display");
    ESP_RETURN_ON_ERROR(init_i2c_bus(stack), kTag, "i2c init failed");
    ESP_RETURN_ON_ERROR(bootstrap_expander(stack), kTag, "expander init failed");
    ESP_RETURN_ON_ERROR(panel_spi_init_gpios(), kTag, "panel gpio init failed");
    ESP_RETURN_ON_ERROR(send_panel_init_sequence(stack), kTag, "panel init sequence failed");
    ESP_RETURN_ON_ERROR(init_panel(stack), kTag, "panel init failed");
    ESP_RETURN_ON_ERROR(init_lvgl(stack), kTag, "lvgl init failed");
    ESP_RETURN_ON_ERROR(init_touch(stack), kTag, "touch init failed");
    return ESP_OK;
}

esp_err_t play_buzzer_chirp(const DisplayStack &stack, uint32_t duration_ms)
{
    DisplayStack &mutable_stack = const_cast<DisplayStack &>(stack);
    ESP_RETURN_ON_ERROR(expander_set_output(mutable_stack, kExpanderBitBuzzer, true), kTag, "buzzer on failed");
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return expander_set_output(mutable_stack, kExpanderBitBuzzer, false);
}

} // namespace app
