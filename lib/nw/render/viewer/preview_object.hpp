#pragma once

#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Location.hpp>
#include <nw/objects/ObjectBase.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Player.hpp>

#include <filesystem>
#include <glm/mat4x4.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nw::render::viewer {

struct PreviewPlaceableVisualLoad {
    bool loaded = false;
    nw::ObjectVisualState visual;
    std::string error;
};

struct PreviewDoorModelLoad {
    bool valid = false;
    nw::Resref model;
    std::string error;
    std::string table;
    std::string column;
    std::string selector;
    int32_t row = -1;
    bool generic = false;
};

bool is_object_preview_path(std::string_view value);
bool load_player_from_file(const std::filesystem::path& path, nw::Player& out);
bool load_creature_from_file(const std::filesystem::path& path, nw::Creature& out);
bool load_item_from_file(const std::filesystem::path& path, nw::Item& out);
bool load_door_from_file(const std::filesystem::path& path, nw::Door& out);
bool load_placeable_from_file(const std::filesystem::path& path, nw::Placeable& out);
bool update_standalone_item_visual(nw::Item& item, bool use_default_fallback, std::string_view origin);
PreviewDoorModelLoad resolve_door_model_from_file(const std::filesystem::path& path);
PreviewDoorModelLoad resolve_door_model_for_object(const nw::Door& door);
std::string door_model_lookup_context(const PreviewDoorModelLoad& lookup);
PreviewPlaceableVisualLoad load_placeable_visual_from_file(const std::filesystem::path& path);
const nw::ObjectVisualState* object_visual_state(const nw::ObjectBase& object) noexcept;
const nw::ObjectVisualState* placeable_visual_state(const nw::Placeable& placeable) noexcept;
bool visual_row_visible_for_mode(
    const nw::ObjectVisualModel& row,
    nw::ObjectVisualRenderMode render_mode) noexcept;
const nw::ObjectVisualModel* first_valid_visual_model(
    std::span<const nw::ObjectVisualModel> models,
    nw::ObjectVisualRenderMode render_mode) noexcept;
const nw::ObjectVisualModel* first_valid_visual_model(
    const nw::ObjectVisualState* visual,
    nw::ObjectVisualRenderMode render_mode) noexcept;
const nw::ObjectVisualModel* first_valid_visual_model(std::span<const nw::ObjectVisualModel> models) noexcept;
const nw::ObjectVisualModel* first_valid_visual_model(const nw::ObjectVisualState* visual) noexcept;
std::string placeable_visual_error(const nw::ObjectVisualState* visual);
std::string area_object_origin(
    std::string_view area,
    std::string_view kind,
    size_t index,
    const nw::ObjectBase& object);
glm::mat4 area_object_placement_transform(const nw::Location& location);

} // namespace nw::render::viewer
