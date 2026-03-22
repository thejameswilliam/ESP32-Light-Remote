#include "app/light_transport.hpp"

#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"

namespace app {
namespace {

constexpr const char *kTag = "light_tx";
constexpr uint8_t kBroadcastAddress[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t kWifiChannel = 1;

esp_err_t init_wifi_for_espnow()
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), kTag, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), kTag, "wifi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE), kTag, "wifi channel failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), kTag, "wifi ps failed");
    return ESP_OK;
}

esp_err_t init_broadcast_peer()
{
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastAddress, sizeof(kBroadcastAddress));
    peer.ifidx = WIFI_IF_STA;
    peer.channel = kWifiChannel;
    peer.encrypt = false;

    if (esp_now_is_peer_exist(kBroadcastAddress)) {
        return ESP_OK;
    }

    return esp_now_add_peer(&peer);
}

} // namespace

LightTransport::~LightTransport()
{
    if (task_ != nullptr) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
    if (ready_) {
        esp_now_deinit();
        esp_wifi_stop();
        esp_wifi_deinit();
    }
}

esp_err_t LightTransport::init()
{
    if (ready_) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_wifi_for_espnow(), kTag, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_now_init(), kTag, "esp-now init failed");
    ESP_RETURN_ON_ERROR(init_broadcast_peer(), kTag, "peer init failed");

    queue_ = xQueueCreate(1, sizeof(LightStateMessage));
    if (queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(
        worker_entry, "light_tx", 4096, this, 4, &task_, 0
    );
    if (created != pdPASS) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    ready_ = true;
    return ESP_OK;
}

esp_err_t LightTransport::send_state(const LightState &state)
{
    if (!ready_ || queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const LightStateMessage message = make_light_message(state, ++sequence_);
    return xQueueOverwrite(queue_, &message) == pdTRUE ? ESP_OK : ESP_FAIL;
}

void LightTransport::worker_entry(void *arg)
{
    static_cast<LightTransport *>(arg)->worker_loop();
}

void LightTransport::worker_loop()
{
    LightStateMessage message = {};
    TickType_t last_send = 0;

    while (true) {
        if (xQueueReceive(queue_, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        const TickType_t min_gap = pdMS_TO_TICKS(100);
        if (last_send != 0 && now - last_send < min_gap) {
            vTaskDelay(min_gap - (now - last_send));
        }

        esp_err_t err = esp_now_send(kBroadcastAddress, reinterpret_cast<const uint8_t *>(&message), sizeof(message));
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "esp_now_send failed: %s", esp_err_to_name(err));
        }
        last_send = xTaskGetTickCount();
    }
}

} // namespace app
