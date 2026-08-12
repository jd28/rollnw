#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nw {
struct Area;
}

namespace nw::toolset {

struct PlacedAreaObjectRow {
    ObjectHandle object{};
    std::string name;
};

[[nodiscard]] std::string_view placed_area_object_type_label(ObjectType type) noexcept;

// Builds one flat UI row batch in the area's stored category and member order.
// Null members and invalid handles are dropped explicitly.
void build_placed_area_object_rows(
    const Area& area, std::vector<PlacedAreaObjectRow>& rows);

// The active object is an explicit toolset singleton, so this cold label
// transform is singular rather than a separate batch path.
[[nodiscard]] std::string live_object_display_name(ObjectHandle object);

[[nodiscard]] bool save_live_blueprint_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error);

[[nodiscard]] bool save_live_area_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error);

} // namespace nw::toolset
