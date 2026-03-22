#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cinttypes>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <cstdio>
#include "app/display_stack.hpp"
#include "app/home_screen.hpp"

namespace {

constexpr const char *kTag = "app";
app::DisplayStack s_display;

} // namespace

extern "C" void app_main(void)
{
    esp_rom_printf("hello from app_main\r\n");
    std::printf("printf: app_main entered\n");
    std::fflush(stdout);
    ESP_LOGI(kTag, "starting app");
    ESP_LOGI(kTag, "display init begins in 2000 ms");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_rom_printf("app: initializing display\r\n");
    ESP_ERROR_CHECK(app::initialize_display(s_display));
    ESP_LOGI(kTag, "building home screen");
    ESP_ERROR_CHECK(app::create_home_screen(s_display));
    ESP_LOGI(kTag, "ui ready");

    uint32_t heartbeat = 0;
    while (true) {
        ESP_LOGI(kTag, "heartbeat=%" PRIu32, heartbeat++);
        std::printf("printf heartbeat=%" PRIu32 "\n", heartbeat);
        std::fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
