#include "app/home_screen.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace app {
namespace {

constexpr const char *kTag = "home_screen";
constexpr int kDotSize = 24;
constexpr int kScreenSize = 480;

struct HomeScreenState {
    lv_obj_t *dot = nullptr;
};

HomeScreenState s_state;

void move_dot_to_active_point()
{
    if (s_state.dot == nullptr) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }

    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    const int dot_x = point.x - (kDotSize / 2);
    const int dot_y = point.y - (kDotSize / 2);
    lv_obj_set_pos(s_state.dot, dot_x, dot_y);
    lv_obj_move_foreground(s_state.dot);
    lv_obj_invalidate(s_state.dot);
    ESP_LOGI(
        kTag,
        "touch x=%ld y=%ld dot_x=%ld dot_y=%ld",
        static_cast<long>(point.x),
        static_cast<long>(point.y),
        static_cast<long>(dot_x),
        static_cast<long>(dot_y)
    );
}

void on_screen_pressed(lv_event_t *event)
{
    (void)event;
    move_dot_to_active_point();
}

void on_screen_pressing(lv_event_t *event)
{
    (void)event;
    move_dot_to_active_point();
}

} // namespace

esp_err_t create_home_screen(DisplayStack &display)
{
    ESP_RETURN_ON_FALSE(display.display != nullptr, ESP_ERR_INVALID_STATE, kTag, "display not ready");
    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, kTag, "lvgl lock failed");

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
    lv_obj_set_style_shadow_width(screen, 0, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, on_screen_pressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(screen, on_screen_pressing, LV_EVENT_PRESSING, nullptr);

    lv_obj_t *dot = lv_obj_create(screen);
    lv_obj_set_size(dot, kDotSize, kDotSize);
    lv_obj_set_align(dot, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(dot, (kScreenSize - kDotSize) / 2, (kScreenSize - kDotSize) / 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_outline_width(dot, 0, 0);
    lv_obj_set_style_shadow_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    s_state.dot = dot;

    lv_obj_invalidate(screen);
    lv_refr_now(display.display);
    lvgl_port_unlock();

    ESP_LOGI(kTag, "touch test ready");
    return ESP_OK;
}

} // namespace app
