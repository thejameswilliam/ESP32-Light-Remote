#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "app/display_stack.hpp"
#include "app/home_screen.hpp"
#include "app/light_transport.hpp"
#include "app/settings_store.hpp"

namespace {

constexpr const char *kTag = "app";

app::DisplayStack s_display;
app::SettingsStore s_settings_store;
app::LightTransport s_light_transport;

esp_err_t init_nvs_flash_storage()
{
    // The first boot after layout changes may require an erase before NVS can reopen cleanly.
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs erase failed");
        err = nvs_flash_init();
    }

    return err;
}

} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "starting app");

    // Bring up persistence first so the UI can start from the last saved state.
    ESP_ERROR_CHECK(init_nvs_flash_storage());
    ESP_ERROR_CHECK(s_settings_store.init());
    const app::PersistedSettings settings = s_settings_store.load();

    // Display initialization also brings up LVGL and touch input.
    ESP_ERROR_CHECK(app::initialize_display(s_display));

    // Transport is optional. The UI still works locally if ESP-NOW is unavailable.
    app::LightTransport *transport = nullptr;
    const esp_err_t transport_err = s_light_transport.init();
    if (transport_err == ESP_OK) {
        transport = &s_light_transport;
    } else {
        ESP_LOGW(kTag, "light transport unavailable: %s", esp_err_to_name(transport_err));
    }

    // Build the main UI once the platform stack is ready.
    ESP_ERROR_CHECK(app::create_home_screen(s_display, settings, &s_settings_store, transport));
    ESP_LOGI(kTag, "ready");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
