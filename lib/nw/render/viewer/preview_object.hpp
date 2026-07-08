#pragma once

#include <nw/objects/Common.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Location.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Player.hpp>

#include <filesystem>
#include <glm/mat4x4.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace nw::render::viewer {

bool is_object_preview_path(std::string_view value);
bool load_player_from_file(const std::filesystem::path& path, nw::Player& out);
bool load_creature_from_file(const std::filesystem::path& path, nw::Creature& out);
bool load_item_from_file(const std::filesystem::path& path, nw::Item& out);
bool load_door_from_file(const std::filesystem::path& path, nw::Door& out);
bool load_placeable_from_file(const std::filesystem::path& path, nw::Placeable& out);
std::optional<nw::DoorModelReference> resolve_door_model_reference(const nw::Door& door, const std::filesystem::path& path);
std::string door_model_lookup_context(const nw::DoorModelReference& lookup);
std::string area_object_origin(std::string_view area, std::string_view kind, size_t index, const nw::Common& common);
glm::mat4 area_object_placement_transform(const nw::Location& location);

} // namespace nw::render::viewer
