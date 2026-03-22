#pragma once

#include <stdint.h>
#include "app/light_state.hpp"

namespace app {

struct __attribute__((packed)) LightStateMessage {
    uint32_t magic = 0x544C4954; // TLIT
    uint8_t version = 2;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t brightness = 0;
    uint8_t power_on = 0;
    uint8_t reserved[2] = {0, 0};
    uint32_t sequence = 0;
};

inline LightStateMessage make_light_message(const LightState &state, uint32_t sequence)
{
    LightStateMessage message;
    message.red = state.red;
    message.green = state.green;
    message.blue = state.blue;
    message.brightness = state.brightness;
    message.power_on = state.power_on ? 1 : 0;
    message.sequence = sequence;
    return message;
}

} // namespace app

