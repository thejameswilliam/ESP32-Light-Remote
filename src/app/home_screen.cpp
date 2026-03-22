#include "app/home_screen.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <stdint.h>
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "app/preset_store.hpp"

namespace app {
namespace {

constexpr const char *kTag = "home_screen";

constexpr int kArcSize = 430;
constexpr int kArcLineWidth = 22;
constexpr int kBrightnessArcSize = 315;
constexpr int kBrightnessArcLineWidth = 22;
constexpr int kPresetButtonSize = 62;
constexpr int kPresetButtonRadius = 134;
constexpr int kCenterBubbleSize = 138;
constexpr uint32_t kPersistDelayMs = 750;

enum class Channel : uint8_t {
    Red,
    Green,
    Blue,
    Brightness,
};

class HomeScreen {
public:
    HomeScreen(DisplayStack &display, SettingsStore &settings_store, LightTransport *transport):
        display_(display),
        settings_store_(settings_store),
        transport_(transport)
    {
    }

    esp_err_t build(const PersistedSettings &initial_settings)
    {
        settings_ = initial_settings;
        root_ = lv_screen_active();
        lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

        create_rgb_arc(Channel::Red, 0, 120, lv_color_make(255, 60, 60));
        create_rgb_arc(Channel::Green, 120, 240, lv_color_make(70, 255, 120));
        create_rgb_arc(Channel::Blue, 240, 360, lv_color_make(70, 150, 255));
        create_brightness_arc();
        create_center_bubble();
        create_preset_buttons();
        create_persist_timer();

        apply_state_to_ui();
        refresh_preset_colors();
        update_visibility();
        transmit_current_state();
        return ESP_OK;
    }

private:
    void create_rgb_arc(Channel channel, int start_angle, int end_angle, lv_color_t indicator_color)
    {
        lv_obj_t *arc = lv_arc_create(root_);
        lv_obj_set_size(arc, kArcSize, kArcSize);
        lv_obj_center(arc);
        lv_obj_add_flag(arc, LV_OBJ_FLAG_ADV_HITTEST);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_bg_angles(arc, start_angle, end_angle);
        lv_arc_set_range(arc, 0, 255);
        lv_obj_set_style_arc_width(arc, kArcLineWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, kArcLineWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x262626), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, indicator_color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_outline_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_height(arc, 0, LV_PART_KNOB);
        lv_obj_add_event_cb(arc, on_arc_changed, LV_EVENT_VALUE_CHANGED, to_user_data(channel));
        lv_obj_add_event_cb(arc, on_arc_released, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(arc, on_arc_released, LV_EVENT_PRESS_LOST, this);

        switch (channel) {
        case Channel::Red:
            red_arc_ = arc;
            break;
        case Channel::Green:
            green_arc_ = arc;
            break;
        case Channel::Blue:
            blue_arc_ = arc;
            break;
        case Channel::Brightness:
            break;
        }
    }

    void create_brightness_arc()
    {
        brightness_arc_ = lv_arc_create(root_);
        lv_obj_set_size(brightness_arc_, kBrightnessArcSize, kBrightnessArcSize);
        lv_obj_center(brightness_arc_);
        lv_obj_add_flag(brightness_arc_, LV_OBJ_FLAG_ADV_HITTEST);
        lv_arc_set_rotation(brightness_arc_, 0);
        lv_arc_set_bg_angles(brightness_arc_, 180, 360);
        lv_arc_set_range(brightness_arc_, 0, 255);
        lv_obj_set_style_arc_width(brightness_arc_, kBrightnessArcLineWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_width(brightness_arc_, kBrightnessArcLineWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(brightness_arc_, lv_color_hex(0x262626), LV_PART_MAIN);
        lv_obj_set_style_arc_color(brightness_arc_, lv_color_white(), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(brightness_arc_, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(brightness_arc_, true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(brightness_arc_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(brightness_arc_, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(brightness_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(brightness_arc_, 0, LV_PART_KNOB);
        lv_obj_set_style_outline_width(brightness_arc_, 0, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(brightness_arc_, 0, LV_PART_KNOB);
        lv_obj_set_style_width(brightness_arc_, 0, LV_PART_KNOB);
        lv_obj_set_style_height(brightness_arc_, 0, LV_PART_KNOB);
        lv_obj_add_event_cb(brightness_arc_, on_arc_changed, LV_EVENT_VALUE_CHANGED, to_user_data(Channel::Brightness));
        lv_obj_add_event_cb(brightness_arc_, on_arc_released, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(brightness_arc_, on_arc_released, LV_EVENT_PRESS_LOST, this);
    }

    void create_center_bubble()
    {
        center_bubble_ = lv_obj_create(root_);
        lv_obj_set_size(center_bubble_, kCenterBubbleSize, kCenterBubbleSize);
        lv_obj_center(center_bubble_);
        lv_obj_set_style_radius(center_bubble_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(center_bubble_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(center_bubble_, 0, 0);
        lv_obj_set_style_outline_width(center_bubble_, 0, 0);
        lv_obj_set_style_shadow_width(center_bubble_, 0, 0);
        lv_obj_add_flag(center_bubble_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(center_bubble_, on_power_toggled, LV_EVENT_CLICKED, this);
    }

    void create_preset_buttons()
    {
        static constexpr std::array<int, kPresetCount> kAngles = {150, 120, 90, 60, 30};
        const int center_x = 240;
        const int center_y = 240;

        for (size_t i = 0; i < kPresetCount; ++i) {
            lv_obj_t *button = lv_obj_create(root_);
            lv_obj_set_size(button, kPresetButtonSize, kPresetButtonSize);
            lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_set_style_outline_width(button, 0, 0);
            lv_obj_set_style_shadow_width(button, 0, 0);
            lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);

            const float radians = static_cast<float>(kAngles[i]) * 3.14159265f / 180.0f;
            const int x = center_x + static_cast<int>(kPresetButtonRadius * std::cos(radians));
            const int y = center_y + static_cast<int>(kPresetButtonRadius * std::sin(radians));
            lv_obj_set_pos(button, x - (kPresetButtonSize / 2), y - (kPresetButtonSize / 2));

            lv_obj_add_event_cb(button, on_preset_recalled, LV_EVENT_SHORT_CLICKED, to_user_data(i));
            lv_obj_add_event_cb(button, on_preset_saved, LV_EVENT_LONG_PRESSED, to_user_data(i));
            preset_buttons_[i] = button;
        }
    }

    static void *to_user_data(Channel channel)
    {
        return reinterpret_cast<void *>(static_cast<uintptr_t>(channel));
    }

    static void *to_user_data(size_t value)
    {
        return reinterpret_cast<void *>(static_cast<uintptr_t>(value));
    }

    void create_persist_timer()
    {
        persist_timer_ = lv_timer_create(on_persist_timer, kPersistDelayMs, this);
        lv_timer_set_repeat_count(persist_timer_, 1);
        lv_timer_set_auto_delete(persist_timer_, false);
        lv_timer_pause(persist_timer_);
    }

    void schedule_persist()
    {
        if (persist_timer_ == nullptr) {
            return;
        }
        lv_timer_pause(persist_timer_);
        lv_timer_set_period(persist_timer_, kPersistDelayMs);
        lv_timer_set_repeat_count(persist_timer_, 1);
        lv_timer_reset(persist_timer_);
        lv_timer_resume(persist_timer_);
    }

    void persist_now()
    {
        settings_store_.save(settings_);
    }

    void transmit_current_state()
    {
        if (transport_ != nullptr && transport_->is_ready()) {
            transport_->send_state(settings_.current);
        }
    }

    void apply_state_to_ui()
    {
        suppress_events_ = true;
        lv_arc_set_value(red_arc_, settings_.current.red);
        lv_arc_set_value(green_arc_, settings_.current.green);
        lv_arc_set_value(blue_arc_, settings_.current.blue);
        lv_arc_set_value(brightness_arc_, settings_.current.brightness);
        suppress_events_ = false;
        refresh_center_bubble();
    }

    void refresh_center_bubble()
    {
        if (!settings_.current.power_on) {
            lv_obj_set_style_bg_color(center_bubble_, lv_color_black(), 0);
            lv_obj_set_style_border_color(center_bubble_, lv_color_white(), 0);
            lv_obj_set_style_border_width(center_bubble_, 3, 0);
            return;
        }

        lv_obj_set_style_bg_color(center_bubble_, preview_color(settings_.current), 0);
        lv_obj_set_style_border_width(center_bubble_, 0, 0);
    }

    void refresh_preset_colors()
    {
        for (size_t i = 0; i < preset_buttons_.size(); ++i) {
            lv_obj_set_style_bg_color(preset_buttons_[i], preview_color(settings_.presets[i]), 0);
        }
    }

    void update_visibility()
    {
        const bool powered = settings_.current.power_on;
        set_hidden(red_arc_, !powered);
        set_hidden(green_arc_, !powered);
        set_hidden(blue_arc_, !powered);
        set_hidden(brightness_arc_, !powered);
        for (lv_obj_t *button : preset_buttons_) {
            set_hidden(button, !powered);
        }
    }

    static void set_hidden(lv_obj_t *object, bool hidden)
    {
        if (hidden) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void on_channel_changed(Channel channel, int value)
    {
        switch (channel) {
        case Channel::Red:
            settings_.current.red = static_cast<uint8_t>(value);
            break;
        case Channel::Green:
            settings_.current.green = static_cast<uint8_t>(value);
            break;
        case Channel::Blue:
            settings_.current.blue = static_cast<uint8_t>(value);
            break;
        case Channel::Brightness:
            settings_.current.brightness = static_cast<uint8_t>(value);
            break;
        }

        refresh_center_bubble();
        transmit_current_state();
        schedule_persist();
    }

    void recall_preset(size_t index)
    {
        if (!settings_.current.power_on || index >= settings_.presets.size()) {
            return;
        }
        settings_.current.red = settings_.presets[index].red;
        settings_.current.green = settings_.presets[index].green;
        settings_.current.blue = settings_.presets[index].blue;
        settings_.current.brightness = settings_.presets[index].brightness;
        apply_state_to_ui();
        transmit_current_state();
        persist_now();
    }

    void save_preset(size_t index)
    {
        if (!settings_.current.power_on || index >= settings_.presets.size()) {
            return;
        }
        settings_.presets[index] = settings_.current;
        refresh_preset_colors();
        persist_now();
        play_buzzer_chirp(display_);
        ESP_LOGI(kTag, "preset %u saved", static_cast<unsigned>(index));
    }

    void toggle_power()
    {
        settings_.current.power_on = !settings_.current.power_on;
        refresh_center_bubble();
        update_visibility();
        transmit_current_state();
        persist_now();
    }

    static HomeScreen &from_event(lv_event_t *event)
    {
        return *static_cast<HomeScreen *>(lv_event_get_user_data(event));
    }

    static void on_arc_changed(lv_event_t *event)
    {
        auto *arc = static_cast<lv_obj_t *>(lv_event_get_target(event));
        HomeScreen &screen = instance();
        if (screen.suppress_events_) {
            return;
        }
        const auto channel = static_cast<Channel>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
        screen.on_channel_changed(channel, lv_arc_get_value(arc));
    }

    static void on_arc_released(lv_event_t *event)
    {
        from_event(event).schedule_persist();
    }

    static void on_power_toggled(lv_event_t *event)
    {
        from_event(event).toggle_power();
    }

    static void on_preset_recalled(lv_event_t *event)
    {
        HomeScreen &screen = instance();
        const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
        screen.recall_preset(index);
    }

    static void on_preset_saved(lv_event_t *event)
    {
        HomeScreen &screen = instance();
        const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
        screen.save_preset(index);
    }

    static void on_persist_timer(lv_timer_t *timer)
    {
        auto *screen = static_cast<HomeScreen *>(lv_timer_get_user_data(timer));
        if (screen != nullptr) {
            screen->persist_now();
            lv_timer_pause(timer);
        }
    }

    static HomeScreen &instance()
    {
        return *instance_;
    }

public:
    static HomeScreen *instance_;

private:
    DisplayStack &display_;
    SettingsStore &settings_store_;
    LightTransport *transport_ = nullptr;
    PersistedSettings settings_ = {};
    lv_obj_t *root_ = nullptr;
    lv_obj_t *red_arc_ = nullptr;
    lv_obj_t *green_arc_ = nullptr;
    lv_obj_t *blue_arc_ = nullptr;
    lv_obj_t *brightness_arc_ = nullptr;
    lv_obj_t *center_bubble_ = nullptr;
    std::array<lv_obj_t *, kPresetCount> preset_buttons_ = {};
    lv_timer_t *persist_timer_ = nullptr;
    bool suppress_events_ = false;
};

HomeScreen *HomeScreen::instance_ = nullptr;
std::unique_ptr<HomeScreen> s_screen;

} // namespace

esp_err_t create_home_screen(
    DisplayStack &display,
    SettingsStore &settings_store,
    LightTransport *transport,
    const PersistedSettings &initial_settings
)
{
    s_screen = std::make_unique<HomeScreen>(display, settings_store, transport);
    HomeScreen::instance_ = s_screen.get();

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t err = s_screen->build(initial_settings);
    lvgl_port_unlock();
    return err;
}

} // namespace app
