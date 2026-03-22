#pragma once

#include <stdint.h>
#include "lvgl.h"

namespace app {

struct LightState {
    uint8_t red = 255;
    uint8_t green = 180;
    uint8_t blue = 120;
    uint8_t brightness = 255;
    bool power_on = true;
};

inline uint8_t scale_channel(uint8_t channel, uint8_t brightness)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(channel) * brightness + 127U) / 255U);
}

inline lv_color_t preview_color(const LightState &state)
{
    if (!state.power_on) {
        return lv_color_black();
    }

    return lv_color_make(
        scale_channel(state.red, state.brightness),
        scale_channel(state.green, state.brightness),
        scale_channel(state.blue, state.brightness)
    );
}

} // namespace app

