#include "app/home_screen.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace app {
namespace {

constexpr const char *kTag = "home_screen";
constexpr int kCircleSize = 140;

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

    lv_obj_t *circle = lv_obj_create(screen);
    lv_obj_set_size(circle, kCircleSize, kCircleSize);
    lv_obj_center(circle);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_outline_width(circle, 0, 0);
    lv_obj_set_style_shadow_width(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_invalidate(screen);
    lv_refr_now(display.display);
    lvgl_port_unlock();

    ESP_LOGI(kTag, "centered circle ready");
    return ESP_OK;
}

} // namespace app
