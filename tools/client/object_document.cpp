#include "object_document.hpp"

#include "area_map.hpp"
#include "resource_document.hpp"
#include "workspace.hpp"

#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace nw::toolset {

ObjectDocument::~ObjectDocument() { reset(); }

ObjectDocument::ObjectDocument(ObjectDocument&& other) noexcept
    : object_(std::exchange(other.object_, ObjectHandle{}))
{
}

ObjectDocument& ObjectDocument::operator=(ObjectDocument&& other) noexcept
{
    if (this != &other) {
        reset();
        object_ = std::exchange(other.object_, ObjectHandle{});
    }
    return *this;
}

bool ObjectDocument::adopt(ObjectHandle object)
{
    const auto* objects = kernel::services().get<ObjectManager>();
    if (object_.type != ObjectType::invalid || !objects || !objects->valid(object)
        || object.type == ObjectType::module) {
        return false;
    }
    object_ = object;
    return true;
}

void ObjectDocument::reset() noexcept
{
    const auto object = std::exchange(object_, ObjectHandle{});
    auto* objects = kernel::services().get_mut<ObjectManager>();
    if (!objects || !objects->valid(object)) { return; }
    if (object.type == ObjectType::area) {
        objects->get<Area>(object)->clear();
    }
    objects->destroy(object);
}

namespace {

std::optional<std::filesystem::path> validated_project_file(
    const std::filesystem::path& project_dir, std::string_view relative_text, std::string& error)
{
    namespace fs = std::filesystem;
    const fs::path relative{relative_text};
    if (project_dir.empty() || relative.empty() || relative.is_absolute()) {
        error = "Document path is not project-relative";
        return std::nullopt;
    }
    std::error_code ec;
    const auto root = fs::weakly_canonical(project_dir, ec);
    if (ec) {
        error = "Failed to resolve project directory: " + ec.message();
        return std::nullopt;
    }
    const auto target = fs::weakly_canonical(project_dir / relative, ec);
    if (ec) {
        error = "Failed to resolve document: " + ec.message();
        return std::nullopt;
    }
    const auto from_root = fs::relative(target, root, ec);
    if (ec || from_root.empty() || std::any_of(from_root.begin(), from_root.end(), [](const auto& part) { return part == ".."; })) {
        error = "Document is outside the active project";
        return std::nullopt;
    }
    return target;
}

bool save_workspace_document(WorkspaceTab& tab, const std::filesystem::path& project_dir,
    std::string& diagnostic, bool& warning)
{
    if (tab.kind != WorkspaceTabKind::area && tab.kind != WorkspaceTabKind::preview) {
        diagnostic = "Only blueprint and area documents support saving";
        return false;
    }
    if (std::filesystem::path{tab.detail}.extension() != ".json") {
        diagnostic = "Live document saving requires a JSON project resource; binary resources are not overwritten";
        return false;
    }
    const auto object = tab.document.object();
    const auto* objects = kernel::services().get<ObjectManager>();
    if (!objects || !objects->valid(object)) {
        diagnostic = "Live document is unavailable or stale; edits were not saved";
        return false;
    }
    const auto target = validated_project_file(project_dir, tab.detail, diagnostic);
    if (!target) { return false; }
    const bool saved = tab.kind == WorkspaceTabKind::area
        ? save_live_area_json_atomic(object, *target, diagnostic)
        : save_live_blueprint_json_atomic(object, *target, diagnostic);
    if (!saved) { return false; }
    tab.dirty = false;

    // Maps are derived data: a map failure must not turn a successful CAF save
    // into a reported document failure or discard the authored file.
    if (tab.kind == WorkspaceTabKind::area) {
        try {
            const std::array<const Area*, 1> areas{kernel::objects().get<Area>(object)};
            const auto sources = collect_area_map_sources(areas);
            const auto maps = write_project_area_maps(project_dir, sources);
            warning = maps.failed > 0 || maps.degraded > 0;
            if (warning) {
                diagnostic = maps.failed > 0 ? "Area map unavailable: " + maps.first_error
                                             : "Area map contains missing-tile markers: " + maps.first_warning;
            }
        } catch (const std::exception& ex) {
            warning = true;
            diagnostic = "Area saved, but map generation failed: " + std::string{ex.what()};
        }
    }
    return true;
}

template <typename T>
void append_placed_area_object_rows(
    std::vector<PlacedAreaObjectRow>& rows, const Vector<T*>& members)
{
    for (const auto* member : members) {
        if (!member || !kernel::objects().valid(member->handle())) {
            continue;
        }
        rows.push_back({
            .object = member->handle(),
            .name = live_object_display_name(member->handle()),
        });
    }
}

template <typename T>
bool serialize_json_blueprint(const T* object, nlohmann::json& output)
{
    bool (*serializer)(const T*, nlohmann::json&, SerializationProfile) = serialize;
    return serializer && serializer(object, output, SerializationProfile::blueprint);
}

bool serialize_blueprint(ObjectBase* object, nlohmann::json& output)
{
    if (!object) {
        return false;
    }

    switch (object->handle().type) {
    case ObjectType::creature:
        return serialize_json_blueprint(object->as_creature(), output);
    case ObjectType::door:
        return serialize_json_blueprint(object->as_door(), output);
    case ObjectType::encounter:
        return serialize_json_blueprint(object->as_encounter(), output);
    case ObjectType::item:
        return serialize_json_blueprint(object->as_item(), output);
    case ObjectType::placeable:
        return serialize_json_blueprint(object->as_placeable(), output);
    case ObjectType::player: {
        bool (*serializer)(const Player*, nlohmann::json&) = serialize;
        return serializer && serializer(object->as_player(), output);
    }
    case ObjectType::sound:
        return serialize_json_blueprint(object->as_sound(), output);
    case ObjectType::store:
        return serialize_json_blueprint(object->as_store(), output);
    case ObjectType::trigger:
        return serialize_json_blueprint(object->as_trigger(), output);
    case ObjectType::waypoint:
        return serialize_json_blueprint(object->as_waypoint(), output);
    default:
        return false;
    }
}

std::string creature_name_field(smalls::Runtime& runtime,
    const smalls::Value& propset,
    const smalls::StructDef& definition,
    smalls::TypeID text_ref_type,
    std::string_view field_name)
{
    const uint32_t field_index = definition.field_index(field_name);
    if (field_index == UINT32_MAX) {
        return {};
    }
    const auto& field = definition.fields[field_index];
    if (field.type_id != text_ref_type) {
        return {};
    }
    const auto value = runtime.read_value_field_at_offset(
        propset, field.offset, field.type_id);
    const auto* text_ref = static_cast<const TextRef*>(runtime.get_value_data_ptr(value));
    return text_ref ? kernel::strings().get(*text_ref) : std::string{};
}

std::string live_creature_name(ObjectHandle object)
{
    auto& runtime = kernel::runtime();
    const auto descriptor_type = runtime.type_id(
        "nwn1.propsets.CreatureDescriptor", false);
    const auto* definition = runtime.get_struct_def(descriptor_type);
    if (!definition) {
        return {};
    }
    const auto propset = runtime.find_propset_ref(descriptor_type, object);
    if (propset.type_id == smalls::invalid_type_id) {
        return {};
    }
    const auto text_ref_type = runtime.type_id("core.types.TextRef", false);
    if (text_ref_type == smalls::invalid_type_id) {
        return {};
    }
    std::string result = creature_name_field(
        runtime, propset, *definition, text_ref_type, "name_first");
    const std::string last = creature_name_field(
        runtime, propset, *definition, text_ref_type, "name_last");
    if (!result.empty() && !last.empty()) {
        result.push_back(' ');
    }
    result += last;
    return result;
}

} // namespace

std::string_view placed_area_object_type_label(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::creature:
        return "Creature";
    case ObjectType::door:
        return "Door";
    case ObjectType::encounter:
        return "Encounter";
    case ObjectType::item:
        return "Item";
    case ObjectType::placeable:
        return "Placeable";
    case ObjectType::sound:
        return "Sound";
    case ObjectType::store:
        return "Store";
    case ObjectType::trigger:
        return "Trigger";
    case ObjectType::waypoint:
        return "Waypoint";
    default:
        return "Object";
    }
}

void build_placed_area_object_rows(
    const Area& area, std::vector<PlacedAreaObjectRow>& rows)
{
    rows.clear();
    rows.reserve(area.creatures.size()
        + area.doors.size()
        + area.encounters.size()
        + area.items.size()
        + area.placeables.size()
        + area.sounds.size()
        + area.stores.size()
        + area.triggers.size()
        + area.waypoints.size());
    append_placed_area_object_rows(rows, area.creatures);
    append_placed_area_object_rows(rows, area.doors);
    append_placed_area_object_rows(rows, area.encounters);
    append_placed_area_object_rows(rows, area.items);
    append_placed_area_object_rows(rows, area.placeables);
    append_placed_area_object_rows(rows, area.sounds);
    append_placed_area_object_rows(rows, area.stores);
    append_placed_area_object_rows(rows, area.triggers);
    append_placed_area_object_rows(rows, area.waypoints);
}

std::string live_object_display_name(ObjectHandle object)
{
    const auto* live_object = kernel::objects().get_object_base(object);
    if (!live_object) {
        return {};
    }

    if (object.type == ObjectType::creature) {
        if (auto name = live_creature_name(object); !name.empty()) {
            return name;
        }
    }
    if (auto name = kernel::strings().get(live_object->name); !name.empty()) {
        return name;
    }
    if (!live_object->tag.view().empty()) {
        return std::string{live_object->tag.view()};
    }
    if (!live_object->resref.empty()) {
        return std::string{live_object->resref.view()};
    }
    return std::string{placed_area_object_type_label(object.type)};
}

CommandResult save_workspace_documents(WorkspaceState& workspace,
    const std::filesystem::path& project_dir, std::span<const std::string_view> tab_ids)
{
    CommandResult result;
    if (tab_ids.empty()) {
        result.status = CommandStatus::noop;
        result.message = "No documents to save";
        return result;
    }
    for (size_t index = 0; index < tab_ids.size(); ++index) {
        if (tab_ids[index].empty()
            || std::find(tab_ids.begin(), tab_ids.begin() + index, tab_ids[index]) != tab_ids.begin() + index) {
            result.status = CommandStatus::rejected;
            result.output_channel = CommandOutputChannel::error;
            result.message = "Save batch contains an empty or duplicate tab ID";
            return result;
        }
    }
    size_t saved = 0;
    bool any_warning = false;
    std::string details;
    for (const auto id : tab_ids) {
        auto* tab = workspace.find_tab(id);
        bool success = false;
        bool warning = false;
        std::string diagnostic;
        try {
            if (tab) {
                success = save_workspace_document(*tab, project_dir, diagnostic, warning);
            } else {
                diagnostic = "Document tab no longer exists";
            }
        } catch (const std::exception& ex) {
            diagnostic = ex.what();
        }
        if (success) {
            ++saved;
        }
        any_warning |= warning;
        if (!success || warning) {
            details += "\n" + std::string{id} + ": " + diagnostic;
        }
    }
    const auto failed = tab_ids.size() - saved;
    result.status = failed ? CommandStatus::failed : CommandStatus::success;
    result.output_channel = failed ? CommandOutputChannel::error
        : any_warning              ? CommandOutputChannel::warn
                                   : CommandOutputChannel::info;
    result.message = "Saved " + std::to_string(saved) + " of " + std::to_string(tab_ids.size()) + " documents";
    if (failed) { result.message += "; " + std::to_string(failed) + " failed and remain unsaved"; }
    result.message += details;
    return result;
}

bool save_live_blueprint_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error)
{
    error.clear();
    auto* live_object = kernel::objects().get_object_base(object);
    if (!live_object) {
        error = "Live object is invalid or stale";
        return false;
    }

    nlohmann::json serialized;
    if (!serialize_blueprint(live_object, serialized)) {
        error = "Live object type does not support blueprint JSON serialization";
        return false;
    }
    return save_json_resource_document_atomic(target, serialized, error);
}

bool save_live_area_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error)
{
    error.clear();
    if (object.type != ObjectType::area) {
        error = "Live area is invalid or stale";
        return false;
    }
    auto* area = kernel::objects().get<Area>(object);
    if (!area) {
        error = "Live area is invalid or stale";
        return false;
    }

    nlohmann::json serialized;
    try {
        serialize(area, serialized);
    } catch (const std::exception& e) {
        error = "Failed to serialize live area: " + std::string{e.what()};
        return false;
    }
    return save_json_resource_document_atomic(target, serialized, error);
}

} // namespace nw::toolset
