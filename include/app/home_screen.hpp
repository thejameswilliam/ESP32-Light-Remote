#pragma once

#include "esp_err.h"
#include "app/display_stack.hpp"

namespace app {

esp_err_t create_home_screen(DisplayStack &display);

} // namespace app
