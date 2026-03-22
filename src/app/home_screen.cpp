#include "app/home_screen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace app {
namespace {

constexpr const char *kTag = "home_screen";

constexpr size_t kRgbArcCount = 3;
constexpr size_t kArcCount = 4;
constexpr size_t kBrightnessArcIndex = 3;

constexpr int kOuterArcSize = 434;
constexpr int kOuterArcWidth = 22;
constexpr int kBrightnessArcSize = 314;
constexpr int kBrightnessArcWidth = 20;
constexpr int kArcKnobSize = 30;
constexpr int kSegmentGapDegrees = 10;
constexpr int kBrightnessGapDegrees = 20;
constexpr int kArcTouchBandPadding = 16;
constexpr int kBrightnessTouchBandPadding = 24;
constexpr int kPreviewSize = 178;
constexpr int kPresetButtonSize = 56;
constexpr int kPresetButtonRadius = 134;
constexpr int kPresetAngles[kPresetCount] = {150, 120, 90, 60, 30};
constexpr int kPowerOffBorderWidth = 4;
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kTrackColorHex = 0x2B2B2B;
constexpr uint32_t kBrightnessColorHex = 0xF2F2F2;

enum class ControlId : uint8_t {
    Red = 0,
    Green = 1,
    Blue = 2,
    Brightness = 3,
};

struct ArcSpec {
    ControlId id;
    int size;
    int line_width;
    int start_angle;
    int end_angle;
    uint32_t color_hex;
};

struct HomeScreenState {
    DisplayStack *display_stack = nullptr;
    SettingsStore *settings_store = nullptr;
    LightTransport *light_transport = nullptr;
    PersistedSettings settings = {};
    lv_obj_t *screen = nullptr;
    lv_obj_t *preview = nullptr;
    std::array<lv_obj_t *, kArcCount> arcs = {};
    std::array<lv_obj_t *, kPresetCount> preset_buttons = {};
    std::array<bool, kPresetCount> ignore_next_preset_click = {};
    int active_arc_index = -1;
};

constexpr std::array<ArcSpec, kArcCount> kArcSpecs = {{
    {ControlId::Red, kOuterArcSize, kOuterArcWidth, 0 + (kSegmentGapDegrees / 2), 120 - (kSegmentGapDegrees / 2), 0xE84D4D},
    {ControlId::Green, kOuterArcSize, kOuterArcWidth, 120 + (kSegmentGapDegrees / 2), 240 - (kSegmentGapDegrees / 2), 0x67D46A},
    {ControlId::Blue, kOuterArcSize, kOuterArcWidth, 240 + (kSegmentGapDegrees / 2), 360 - (kSegmentGapDegrees / 2), 0x4C8DFF},
    {ControlId::Brightness, kBrightnessArcSize, kBrightnessArcWidth, 180 + (kBrightnessGapDegrees / 2), 360 - (kBrightnessGapDegrees / 2), kBrightnessColorHex},
}};

HomeScreenState s_state;

// Returns the live editable light state shown on screen.
LightState &current_state()
{
    return s_state.settings.current;
}

// Saves the current UI state and presets when persistent storage is available.
void save_settings()
{
    if (s_state.settings_store == nullptr) {
        return;
    }

    const esp_err_t err = s_state.settings_store->save(s_state.settings);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "settings save failed: %s", esp_err_to_name(err));
    }
}

// Sends the current light state to the remote light controller when transport is ready.
void send_current_state()
{
    if ((s_state.light_transport == nullptr) || !s_state.light_transport->is_ready()) {
        return;
    }

    const esp_err_t err = s_state.light_transport->send_state(current_state());
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "state send failed: %s", esp_err_to_name(err));
    }
}

// Small helper to hide or show an LVGL object safely.
void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == nullptr) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Maps a UI control id to the field it edits in the light state.
uint8_t *state_value_ptr(ControlId id)
{
    switch (id) {
        case ControlId::Red:
            return &current_state().red;
        case ControlId::Green:
            return &current_state().green;
        case ControlId::Blue:
            return &current_state().blue;
        case ControlId::Brightness:
            return &current_state().brightness;
    }

    return nullptr;
}

// Maps a control id to its arc widget index.
int arc_index_for_id(ControlId id)
{
    for (size_t i = 0; i < kArcSpecs.size(); ++i) {
        if (kArcSpecs[i].id == id) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

// Refreshes the center preview bubble to match the current light state.
void update_preview_visual()
{
    if (s_state.preview == nullptr) {
        return;
    }

    const LightState &state = current_state();
    lv_obj_set_style_bg_color(s_state.preview, preview_color(state), 0);
    lv_obj_set_style_bg_opa(s_state.preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_state.preview, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_state.preview, state.power_on ? 0 : kPowerOffBorderWidth, 0);
    lv_obj_move_foreground(s_state.preview);
}

// Shows or hides controls based on the power state.
void update_visibility()
{
    const bool controls_visible = current_state().power_on;

    for (lv_obj_t *arc : s_state.arcs) {
        set_hidden(arc, !controls_visible);
    }

    for (lv_obj_t *button : s_state.preset_buttons) {
        set_hidden(button, !controls_visible);
    }
}

// Updates one preset button to reflect its stored color.
void update_preset_button(size_t index)
{
    if ((index >= kPresetCount) || (s_state.preset_buttons[index] == nullptr)) {
        return;
    }

    lv_obj_set_style_bg_color(s_state.preset_buttons[index], preview_color(s_state.settings.presets[index]), 0);
}

// Refreshes all preset button colors from stored preset values.
void update_all_preset_buttons()
{
    for (size_t i = 0; i < kPresetCount; ++i) {
        update_preset_button(i);
    }
}

// Writes a value into an arc widget without going through any built-in interaction path.
void set_arc_widget_value(ControlId id, uint8_t value)
{
    const int index = arc_index_for_id(id);
    if (index < 0) {
        return;
    }

    lv_obj_t *arc = s_state.arcs[static_cast<size_t>(index)];
    if (arc == nullptr) {
        return;
    }

    lv_arc_set_value(arc, value);
}

// Updates a control value in state and keeps its arc widget in sync.
void apply_arc_value(ControlId id, uint8_t value, bool update_preview)
{
    uint8_t *state_value = state_value_ptr(id);
    if (state_value == nullptr) {
        return;
    }

    *state_value = value;
    set_arc_widget_value(id, value);

    if (update_preview) {
        update_preview_visual();
    }
}

// Pushes the full persisted state into the visible widgets.
void apply_state_to_controls()
{
    set_arc_widget_value(ControlId::Red, current_state().red);
    set_arc_widget_value(ControlId::Green, current_state().green);
    set_arc_widget_value(ControlId::Blue, current_state().blue);
    set_arc_widget_value(ControlId::Brightness, current_state().brightness);

    update_preview_visual();
    update_visibility();
}

// Normalizes angles into the 0..359 range used by the ring control logic.
int normalize_angle(int angle)
{
    while (angle < 0) {
        angle += 360;
    }
    while (angle >= 360) {
        angle -= 360;
    }

    return angle;
}

// Measures how far a touch point is from the center of an object.
float point_distance_from_center(lv_obj_t *obj, const lv_point_t &point)
{
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const int center_x = (coords.x1 + coords.x2) / 2;
    const int center_y = (coords.y1 + coords.y2) / 2;
    const int dx = point.x - center_x;
    const int dy = point.y - center_y;
    return std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

// Checks whether the touch point lands inside the active band for one arc.
bool point_hits_arc_band(lv_obj_t *arc, const ArcSpec &spec, const lv_point_t &point)
{
    if (arc == nullptr) {
        return false;
    }

    const float distance = point_distance_from_center(arc, point);
    const int padding = (spec.id == ControlId::Brightness) ? kBrightnessTouchBandPadding : kArcTouchBandPadding;
    const float outer_radius = static_cast<float>(spec.size / 2) + static_cast<float>(padding);
    const float inner_radius = static_cast<float>(spec.size / 2) - static_cast<float>(spec.line_width + padding);
    return distance >= inner_radius && distance <= outer_radius;
}

// Measures how close the touch point is to an arc's centerline.
float distance_to_arc_centerline(lv_obj_t *arc, const ArcSpec &spec, const lv_point_t &point)
{
    const float distance = point_distance_from_center(arc, point);
    const float centerline = static_cast<float>(spec.size / 2) - static_cast<float>(spec.line_width) * 0.5f;
    return std::fabs(distance - centerline);
}

// Converts a touch point into a 0..359 angle around the ring center.
int point_to_angle(lv_obj_t *obj, const lv_point_t &point)
{
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const int center_x = (coords.x1 + coords.x2) / 2;
    const int center_y = (coords.y1 + coords.y2) / 2;
    const float dx = static_cast<float>(point.x - center_x);
    const float dy = static_cast<float>(point.y - center_y);

    float angle = std::atan2(dy, dx) * 180.0f / kPi;
    if (angle < 0.0f) {
        angle += 360.0f;
    }

    return static_cast<int>(angle + 0.5f);
}

// Checks whether an angle falls inside a control's active segment.
bool angle_in_spec(int angle, const ArcSpec &spec)
{
    const int normalized = normalize_angle(angle);
    return normalized >= spec.start_angle && normalized <= spec.end_angle;
}

// Maps an angle inside an arc segment to a 0..255 control value.
uint8_t value_from_angle(int angle, const ArcSpec &spec)
{
    const int normalized = normalize_angle(angle);
    const int clamped = std::max(spec.start_angle, std::min(spec.end_angle, normalized));
    const int span = spec.end_angle - spec.start_angle;
    const int relative = clamped - spec.start_angle;
    return static_cast<uint8_t>((relative * 255) / span);
}

// Selects the control that should own a new touch on the ring.
int pick_active_arc_index(const lv_point_t &point, int angle)
{
    if (
        point_hits_arc_band(s_state.arcs[kBrightnessArcIndex], kArcSpecs[kBrightnessArcIndex], point) &&
        angle_in_spec(angle, kArcSpecs[kBrightnessArcIndex])
    ) {
        return static_cast<int>(kBrightnessArcIndex);
    }

    float best_distance = 1e9f;
    int best_index = -1;

    for (size_t i = 0; i < kRgbArcCount; ++i) {
        if (!point_hits_arc_band(s_state.arcs[i], kArcSpecs[i], point)) {
            continue;
        }
        if (!angle_in_spec(angle, kArcSpecs[i])) {
            continue;
        }

        const float distance = distance_to_arc_centerline(s_state.arcs[i], kArcSpecs[i], point);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = static_cast<int>(i);
        }
    }

    return best_index;
}

// Handles all arc interaction from one shared screen-level touch stream.
void on_touch_arc(lv_event_t *event)
{
    if (!current_state().power_on || s_state.arcs[0] == nullptr) {
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == nullptr) {
        return;
    }

    lv_point_t point = {};
    lv_indev_get_point(indev, &point);
    const int angle = point_to_angle(s_state.arcs[0], point);
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        s_state.active_arc_index = pick_active_arc_index(point, angle);
        if (s_state.active_arc_index < 0) {
            return;
        }
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        if (s_state.active_arc_index < 0) {
            return;
        }

        const ArcSpec &spec = kArcSpecs[static_cast<size_t>(s_state.active_arc_index)];
        apply_arc_value(spec.id, value_from_angle(angle, spec), true);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_state.active_arc_index < 0) {
            return;
        }

        s_state.active_arc_index = -1;
        send_current_state();
        save_settings();
    }
}

// Toggles light power from the center preview bubble.
void on_preview_clicked(lv_event_t *event)
{
    (void)event;
    current_state().power_on = !current_state().power_on;
    update_preview_visual();
    update_visibility();
    send_current_state();
    save_settings();
}

// Handles preset recall on click and preset save on long press.
void on_preset_event(lv_event_t *event)
{
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (index >= kPresetCount) {
        return;
    }

    switch (lv_event_get_code(event)) {
        case LV_EVENT_LONG_PRESSED:
            if (!current_state().power_on) {
                return;
            }

            s_state.ignore_next_preset_click[index] = true;
            s_state.settings.presets[index] = current_state();
            s_state.settings.presets[index].power_on = true;
            update_preset_button(index);
            save_settings();

            if (s_state.display_stack != nullptr) {
                play_buzzer_chirp(*s_state.display_stack);
            }
            break;

        case LV_EVENT_CLICKED:
            if (s_state.ignore_next_preset_click[index]) {
                s_state.ignore_next_preset_click[index] = false;
                return;
            }

            s_state.settings.current = s_state.settings.presets[index];
            apply_state_to_controls();
            send_current_state();
            save_settings();
            break;

        default:
            break;
    }
}

// Creates one visual arc. Touch is handled separately at the screen level.
lv_obj_t *create_arc(const ArcSpec &spec)
{
    lv_obj_t *arc = lv_arc_create(s_state.screen);
    lv_obj_set_size(arc, spec.size, spec.size);
    lv_obj_center(arc);

    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(arc, 0, 255);
    lv_arc_set_change_rate(arc, 0);
    lv_arc_set_rotation(arc, spec.start_angle);
    lv_arc_set_bg_angles(arc, 0, spec.end_angle - spec.start_angle);

    lv_obj_set_style_arc_width(arc, spec.line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, spec.line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(kTrackColorHex), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(spec.color_hex), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc, lv_color_hex(spec.color_hex), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_width(arc, kArcKnobSize, LV_PART_KNOB);
    lv_obj_set_style_height(arc, kArcKnobSize, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_outline_width(arc, 0, 0);
    lv_obj_set_style_shadow_width(arc, 0, 0);
    lv_obj_set_style_pad_all(arc, 6, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

// Positions a widget on a circle around screen center using an angle in degrees.
void position_on_circle(lv_obj_t *obj, int radius, int angle)
{
    const int x = (radius * lv_trigo_cos(angle)) >> LV_TRIGO_SHIFT;
    const int y = (radius * lv_trigo_sin(angle)) >> LV_TRIGO_SHIFT;
    lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
}

// Creates one preset button on the lower inner ring.
lv_obj_t *create_preset_button(size_t index)
{
    lv_obj_t *button = lv_obj_create(s_state.screen);
    lv_obj_set_size(button, kPresetButtonSize, kPresetButtonSize);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_outline_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    position_on_circle(button, kPresetButtonRadius, kPresetAngles[index]);

    lv_obj_add_event_cb(button, on_preset_event, LV_EVENT_CLICKED, reinterpret_cast<void *>(index));
    lv_obj_add_event_cb(button, on_preset_event, LV_EVENT_LONG_PRESSED, reinterpret_cast<void *>(index));
    return button;
}

// Creates the center preview bubble, which also acts as the power button.
lv_obj_t *create_preview()
{
    lv_obj_t *preview = lv_obj_create(s_state.screen);
    lv_obj_set_size(preview, kPreviewSize, kPreviewSize);
    lv_obj_center(preview);
    lv_obj_set_style_radius(preview, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(preview, 0, 0);
    lv_obj_set_style_outline_width(preview, 0, 0);
    lv_obj_set_style_shadow_width(preview, 0, 0);
    lv_obj_set_style_pad_all(preview, 0, 0);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(preview, on_preview_clicked, LV_EVENT_CLICKED, nullptr);
    return preview;
}

} // namespace

esp_err_t create_home_screen(
    DisplayStack &display,
    const PersistedSettings &initial_settings,
    SettingsStore *settings_store,
    LightTransport *light_transport
)
{
    // Build the screen while holding the LVGL lock so widget creation stays thread-safe.
    ESP_RETURN_ON_FALSE(display.display != nullptr, ESP_ERR_INVALID_STATE, kTag, "display not ready");
    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, kTag, "lvgl lock failed");

    s_state = {};
    s_state.display_stack = &display;
    s_state.settings_store = settings_store;
    s_state.light_transport = light_transport;
    s_state.settings = initial_settings;

    s_state.screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_state.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_state.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_state.screen, 0, 0);
    lv_obj_set_style_outline_width(s_state.screen, 0, 0);
    lv_obj_set_style_shadow_width(s_state.screen, 0, 0);
    lv_obj_set_scrollbar_mode(s_state.screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_state.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_state.screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clean(s_state.screen);

    for (size_t i = 0; i < kArcSpecs.size(); ++i) {
        s_state.arcs[i] = create_arc(kArcSpecs[i]);
    }

    for (size_t i = 0; i < kPresetCount; ++i) {
        s_state.preset_buttons[i] = create_preset_button(i);
    }

    s_state.preview = create_preview();

    lv_obj_add_event_cb(s_state.screen, on_touch_arc, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_state.screen, on_touch_arc, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(s_state.screen, on_touch_arc, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_state.screen, on_touch_arc, LV_EVENT_PRESS_LOST, nullptr);

    update_all_preset_buttons();
    apply_state_to_controls();
    lv_obj_move_foreground(s_state.preview);
    lv_obj_invalidate(s_state.screen);
    lv_refr_now(display.display);
    lvgl_port_unlock();

    send_current_state();
    ESP_LOGI(kTag, "mvp ui ready");
    return ESP_OK;
}

} // namespace app
