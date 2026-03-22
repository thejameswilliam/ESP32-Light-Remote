#include "app/preset_store.hpp"

namespace app {

std::array<LightState, kPresetCount> make_default_presets()
{
    return {{
        LightState{255, 90, 30, 255, true},
        LightState{255, 180, 80, 255, true},
        LightState{120, 255, 120, 255, true},
        LightState{70, 160, 255, 255, true},
        LightState{220, 120, 255, 255, true},
    }};
}

} // namespace app

