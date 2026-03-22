#pragma once

#include <array>
#include "app/light_state.hpp"

namespace app {

constexpr size_t kPresetCount = 5;

std::array<LightState, kPresetCount> make_default_presets();

} // namespace app

