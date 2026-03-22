#pragma once

#include <array>
#include "esp_err.h"
#include "nvs.h"
#include "app/light_state.hpp"
#include "app/preset_store.hpp"

namespace app {

struct PersistedSettings {
    LightState current = {};
    std::array<LightState, kPresetCount> presets = {};
};

class SettingsStore {
public:
    SettingsStore() = default;
    ~SettingsStore();

    esp_err_t init();
    PersistedSettings load() const;
    esp_err_t save(const PersistedSettings &settings) const;

private:
    nvs_handle_t handle_ = 0;
};

} // namespace app

