#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app/light_message.hpp"

namespace app {

// Minimal ESP-NOW sender used by the remote to broadcast the latest light state.
class LightTransport {
public:
    LightTransport() = default;
    ~LightTransport();

    esp_err_t init();
    esp_err_t send_state(const LightState &state);
    bool is_ready() const { return ready_; }

private:
    static void worker_entry(void *arg);
    void worker_loop();

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool ready_ = false;
    uint32_t sequence_ = 0;
};

} // namespace app
