#pragma once

#include "esp_err.h"
#include "app/display_stack.hpp"
#include "app/light_transport.hpp"
#include "app/settings_store.hpp"

namespace app {

esp_err_t create_home_screen(
    DisplayStack &display,
    const PersistedSettings &initial_settings,
    SettingsStore *settings_store = nullptr,
    LightTransport *light_transport = nullptr
);

} // namespace app
