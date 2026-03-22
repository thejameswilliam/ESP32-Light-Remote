#include <memory>
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
app::LightTransport s_transport;

esp_err_t init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs erase failed");
        return nvs_flash_init();
    }
    return err;
}

} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "starting app");

    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(s_settings_store.init());

    const app::PersistedSettings settings = s_settings_store.load();

    ESP_ERROR_CHECK(app::initialize_display(s_display));

    esp_err_t transport_err = s_transport.init();
    if (transport_err != ESP_OK) {
        ESP_LOGW(kTag, "light transport disabled: %s", esp_err_to_name(transport_err));
    }

    ESP_ERROR_CHECK(app::create_home_screen(
        s_display,
        s_settings_store,
        transport_err == ESP_OK ? &s_transport : nullptr,
        settings
    ));

    ESP_LOGI(kTag, "ready");
}
