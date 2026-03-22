#include "app/display_stack.hpp"

#include <array>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "drivers/lcd/port/esp_lcd_st7701.h"
#include "drivers/lcd/port/esp_panel_lcd_vendor_types.h"

namespace app {
namespace {

constexpr const char *kTag = "display";

constexpr gpio_num_t kPanelSpiClockGpio = GPIO_NUM_2;
constexpr gpio_num_t kPanelSpiDataGpio = GPIO_NUM_1;
constexpr gpio_num_t kBacklightGpio = GPIO_NUM_6;
constexpr gpio_num_t kTouchInterruptGpio = GPIO_NUM_16;

constexpr uint8_t kExpanderAddress = 0x20;
constexpr uint8_t kExpanderRegOutput = 0x01;
constexpr uint8_t kExpanderRegConfig = 0x03;

constexpr uint8_t kExpanderBitLcdReset = 0;
constexpr uint8_t kExpanderBitTouchReset = 1;
constexpr uint8_t kExpanderBitLcdCs = 2;
constexpr uint8_t kExpanderBitBuzzer = 7;
constexpr bool kBuzzerIdleLevel = false;
constexpr bool kBuzzerActiveLevel = true;
// On this board the touch IRQ idles high and asserts low on touch.
constexpr int kTouchInterruptActiveLevel = 0;
constexpr uint8_t kTouchPollBudgetOnIrq = 8;
constexpr uint8_t kTouchPollBudgetOnTouch = 24;

constexpr uint16_t kDisplayWidth = 480;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint32_t kPixelClockHz = 12 * 1000 * 1000;
constexpr uint32_t kBounceLines = 40;
constexpr uint32_t kBouncePixels = kDisplayWidth * kBounceLines;

struct PanelControlIo {
    esp_lcd_panel_io_t base;
    DisplayStack *stack = nullptr;
};

volatile bool s_touch_irq_pending = false;
uint8_t s_touch_poll_budget = 0;

static const esp_panel_lcd_vendor_init_cmd_t kPanelInitCommands[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x07, 0x02}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31,
                       0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31,
                       0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x6D}, 1, 0},
    {0xB1, (uint8_t[]){0x37}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x43}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE5, (uint8_t[]){0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE8, (uint8_t[]){0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF,
                       0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 480},
    {0x20, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

bool is_touch_interrupt_active()
{
    return gpio_get_level(kTouchInterruptGpio) == kTouchInterruptActiveLevel;
}

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
        .trans_queue_depth = 0,
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
    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitBuzzer, kBuzzerIdleLevel), kTag, "buzzer off failed");
    return ESP_OK;
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
    ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "panel gpio config failed");
    gpio_set_level(kPanelSpiClockGpio, 0);
    gpio_set_level(kPanelSpiDataGpio, 0);
    gpio_set_level(kBacklightGpio, 1);
    return ESP_OK;
}

void panel_spi_delay()
{
    esp_rom_delay_us(1);
}

esp_err_t panel_spi_write_package(DisplayStack &stack, bool is_command, uint8_t value)
{
    ESP_RETURN_ON_ERROR(expander_set_output(stack, kExpanderBitLcdCs, false), kTag, "lcd cs low failed");
    panel_spi_delay();

    for (int bit = 8; bit >= 0; --bit) {
        const bool level = (bit == 8) ? !is_command : ((value >> bit) & 0x01U);
        gpio_set_level(kPanelSpiDataGpio, level ? 1 : 0);
        gpio_set_level(kPanelSpiClockGpio, 0);
        panel_spi_delay();
        gpio_set_level(kPanelSpiClockGpio, 1);
        panel_spi_delay();
        gpio_set_level(kPanelSpiClockGpio, 0);
    }

    gpio_set_level(kPanelSpiDataGpio, 0);
    panel_spi_delay();
    return expander_set_output(stack, kExpanderBitLcdCs, true);
}

esp_err_t panel_control_io_rx_param(esp_lcd_panel_io_t *io, int lcd_cmd, void *param, size_t param_size)
{
    (void)io;
    (void)lcd_cmd;
    (void)param;
    (void)param_size;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t panel_control_io_tx_param(esp_lcd_panel_io_t *io, int lcd_cmd, const void *param, size_t param_size)
{
    auto *panel_io = reinterpret_cast<PanelControlIo *>(io);
    auto &stack = *panel_io->stack;

    if (lcd_cmd >= 0) {
        ESP_RETURN_ON_ERROR(
            panel_spi_write_package(stack, true, static_cast<uint8_t>(lcd_cmd)),
            kTag,
            "panel command failed"
        );
    }

    if (param != nullptr && param_size > 0) {
        const auto *bytes = static_cast<const uint8_t *>(param);
        for (size_t i = 0; i < param_size; ++i) {
            ESP_RETURN_ON_ERROR(panel_spi_write_package(stack, false, bytes[i]), kTag, "panel data failed");
        }
    }

    return ESP_OK;
}

esp_err_t panel_control_io_tx_color(esp_lcd_panel_io_t *io, int lcd_cmd, const void *color, size_t color_size)
{
    (void)io;
    (void)lcd_cmd;
    (void)color;
    (void)color_size;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t panel_control_io_del(esp_lcd_panel_io_t *io)
{
    free(reinterpret_cast<PanelControlIo *>(io));
    return ESP_OK;
}

esp_err_t panel_control_io_register_callbacks(esp_lcd_panel_io_t *io, const esp_lcd_panel_io_callbacks_t *cbs, void *user_ctx)
{
    (void)io;
    (void)cbs;
    (void)user_ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_lcd_panel_io_handle_t create_panel_control_io(DisplayStack &stack)
{
    auto *panel_io = static_cast<PanelControlIo *>(calloc(1, sizeof(PanelControlIo)));
    if (panel_io == nullptr) {
        return nullptr;
    }

    panel_io->stack = &stack;
    panel_io->base.rx_param = panel_control_io_rx_param;
    panel_io->base.tx_param = panel_control_io_tx_param;
    panel_io->base.tx_color = panel_control_io_tx_color;
    panel_io->base.del = panel_control_io_del;
    panel_io->base.register_event_callbacks = panel_control_io_register_callbacks;
    return reinterpret_cast<esp_lcd_panel_io_handle_t>(panel_io);
}

esp_err_t init_panel(DisplayStack &stack)
{
    ESP_LOGI(
        kTag,
        "rgb timing pclk=%" PRIu32 " bounce_lines=%" PRIu32 " bounce_px=%" PRIu32,
        kPixelClockHz,
        kBounceLines,
        kBouncePixels
    );

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

    esp_lcd_panel_io_handle_t panel_io = create_panel_control_io(stack);
    ESP_RETURN_ON_FALSE(panel_io != nullptr, ESP_ERR_NO_MEM, kTag, "new panel io failed");

    const esp_panel_lcd_vendor_config_t vendor_config = {
        .hor_res = kDisplayWidth,
        .ver_res = kDisplayHeight,
        .init_cmds = kPanelInitCommands,
        .init_cmds_size = sizeof(kPanelInitCommands) / sizeof(kPanelInitCommands[0]),
        .rgb_config = &panel_config,
        .flags = {
            .mirror_by_cmd = 0,
            .auto_del_panel_io = 0,
            .use_rgb_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_dev_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = const_cast<esp_panel_lcd_vendor_config_t *>(&vendor_config),
    };

    ESP_LOGI(kTag, "panel step: new st7701");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(panel_io, &panel_dev_config, &stack.panel), kTag, "new st7701 panel failed");

    ESP_LOGI(kTag, "panel step: reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(stack.panel), kTag, "st7701 panel reset failed");

    ESP_LOGI(kTag, "panel step: init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(stack.panel), kTag, "st7701 panel init failed");

    ESP_LOGI(kTag, "panel step: display on");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(stack.panel, true), kTag, "panel display on failed");
    return ESP_OK;
}

esp_err_t init_lvgl(DisplayStack &stack)
{
    ESP_LOGI(kTag, "lvgl init start");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_priority = 4;
    lvgl_cfg.task_stack = 6144;
    lvgl_cfg.task_affinity = -1;
    lvgl_cfg.task_max_sleep_ms = 500;
    lvgl_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), kTag, "lvgl port init failed");

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
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .sw_rotate = false,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = false,
#endif
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
    ESP_RETURN_ON_FALSE(stack.display != nullptr, ESP_FAIL, kTag, "lvgl rgb display add failed");
    lv_display_set_default(stack.display);

    ESP_LOGI(kTag, "lvgl ready");
    return ESP_OK;
}

void lvgl_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    auto *stack = static_cast<DisplayStack *>(lv_indev_get_driver_data(indev));
    if ((stack == nullptr) || (stack->touch == nullptr)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (s_touch_irq_pending && (s_touch_poll_budget < kTouchPollBudgetOnIrq)) {
        s_touch_poll_budget = kTouchPollBudgetOnIrq;
    }

    if (!s_touch_irq_pending && (s_touch_poll_budget == 0) && !is_touch_interrupt_active()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    const esp_err_t read_err = esp_lcd_touch_read_data(stack->touch);
    if (read_err != ESP_OK) {
        static uint32_t error_log_count = 0;
        if (error_log_count < 5) {
            ESP_LOGW(kTag, "touch read failed err=0x%x", read_err);
            ++error_log_count;
        }
        if (s_touch_poll_budget > 0) {
            --s_touch_poll_budget;
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t strength = 0;
    uint8_t points = 0;
    const bool touched = esp_lcd_touch_get_coordinates(stack->touch, &x, &y, &strength, &points, 1);
    s_touch_irq_pending = false;
    if (!touched || (points == 0)) {
        if (s_touch_poll_budget > 0) {
            --s_touch_poll_budget;
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    s_touch_poll_budget = kTouchPollBudgetOnTouch;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

void IRAM_ATTR touch_interrupt_callback(esp_lcd_touch_handle_t tp)
{
    auto *stack = static_cast<DisplayStack *>(tp->config.user_data);
    if ((stack == nullptr) || (stack->touch_indev == nullptr)) {
        return;
    }

    s_touch_irq_pending = true;
    lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, stack->touch_indev);
}

esp_err_t init_touch(DisplayStack &stack)
{
    ESP_LOGI(kTag, "touch init start");

    const esp_lcd_panel_io_i2c_config_t touch_io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(stack.i2c_bus, &touch_io_cfg, &stack.touch_io), kTag, "touch io init failed");

    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = kDisplayWidth,
        .y_max = kDisplayHeight,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = kTouchInterruptGpio,
        .levels = {
            .reset = 0,
            .interrupt = kTouchInterruptActiveLevel,
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
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(stack.touch_io, &touch_cfg, &stack.touch), kTag, "touch driver init failed");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(kTouchInterruptGpio, GPIO_PULLUP_ONLY), kTag, "touch irq pull-up failed");

    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, kTag, "lvgl lock failed");
    stack.touch_indev = lv_indev_create();
    lv_indev_set_type(stack.touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_mode(stack.touch_indev, LV_INDEV_MODE_TIMER);
    lv_indev_set_disp(stack.touch_indev, stack.display);
    lv_indev_set_read_cb(stack.touch_indev, lvgl_touchpad_read);
    lv_indev_set_driver_data(stack.touch_indev, &stack);
    lvgl_port_unlock();

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_register_interrupt_callback_with_data(stack.touch, touch_interrupt_callback, &stack),
        kTag,
        "touch interrupt register failed"
    );

    ESP_LOGI(kTag, "touch ready");
    return ESP_OK;
}

} // namespace

esp_err_t initialize_display(DisplayStack &stack)
{
    ESP_LOGI(kTag, "bootstrapping display");
    esp_rom_printf("display: bootstrapping\r\n");

    ESP_LOGI(kTag, "step 1: panel gpio init");
    ESP_RETURN_ON_ERROR(panel_spi_init_gpios(), kTag, "panel gpio init failed");

    ESP_LOGI(kTag, "step 2: i2c init");
    ESP_RETURN_ON_ERROR(init_i2c_bus(stack), kTag, "i2c init failed");

    ESP_LOGI(kTag, "step 3: expander bootstrap");
    ESP_RETURN_ON_ERROR(bootstrap_expander(stack), kTag, "expander init failed");

    ESP_LOGI(kTag, "step 4: panel init");
    ESP_RETURN_ON_ERROR(init_panel(stack), kTag, "panel init failed");

    ESP_LOGI(kTag, "panel ready handle=%p", static_cast<void *>(stack.panel));
    ESP_RETURN_ON_ERROR(init_lvgl(stack), kTag, "lvgl init failed");
    ESP_RETURN_ON_ERROR(init_touch(stack), kTag, "touch init failed");
    return ESP_OK;
}

esp_err_t play_buzzer_chirp(const DisplayStack &stack, uint32_t duration_ms)
{
    auto &mutable_stack = const_cast<DisplayStack &>(stack);
    ESP_RETURN_ON_FALSE(mutable_stack.expander != nullptr, ESP_ERR_INVALID_STATE, kTag, "expander not ready");

    ESP_RETURN_ON_ERROR(
        expander_set_output(mutable_stack, kExpanderBitBuzzer, kBuzzerActiveLevel),
        kTag,
        "buzzer on failed"
    );
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return expander_set_output(mutable_stack, kExpanderBitBuzzer, kBuzzerIdleLevel);
}

} // namespace app
