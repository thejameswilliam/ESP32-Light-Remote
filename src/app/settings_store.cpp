#include "app/settings_store.hpp"

#include <string.h>
#include "esp_log.h"

namespace app {
namespace {

constexpr const char *kNamespace = "settings";
constexpr const char *kBlobKey = "state";
constexpr uint32_t kMagic = 0x534C4954; // SLIT
constexpr uint32_t kVersion = 1;

struct PersistedBlob {
    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    LightState current = {};
    std::array<LightState, kPresetCount> presets = {};
};

PersistedSettings make_default_settings()
{
    PersistedSettings settings;
    settings.current = LightState{};
    settings.presets = make_default_presets();
    return settings;
}

} // namespace

SettingsStore::~SettingsStore()
{
    if (handle_ != 0) {
        nvs_close(handle_);
        handle_ = 0;
    }
}

esp_err_t SettingsStore::init()
{
    return nvs_open(kNamespace, NVS_READWRITE, &handle_);
}

PersistedSettings SettingsStore::load() const
{
    PersistedSettings defaults = make_default_settings();
    if (handle_ == 0) {
        return defaults;
    }

    PersistedBlob blob{};
    size_t required_size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle_, kBlobKey, &blob, &required_size);
    if (err != ESP_OK || required_size != sizeof(blob) || blob.magic != kMagic || blob.version != kVersion) {
        return defaults;
    }

    PersistedSettings settings;
    settings.current = blob.current;
    settings.presets = blob.presets;
    return settings;
}

esp_err_t SettingsStore::save(const PersistedSettings &settings) const
{
    if (handle_ == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    PersistedBlob blob;
    blob.current = settings.current;
    blob.presets = settings.presets;

    esp_err_t err = nvs_set_blob(handle_, kBlobKey, &blob, sizeof(blob));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle_);
}

} // namespace app

