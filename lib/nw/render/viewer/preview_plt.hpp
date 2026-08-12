#pragma once

#include <string>
#include <string_view>

namespace nw::render::viewer {

std::string preferred_plt_bitmap(
    std::string_view model_resref, std::string_view bitmap_name = {}, std::string_view explicit_plt = {});

} // namespace nw::render::viewer
