#include "preview_object.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/assets.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/error_context.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <utility>

namespace nw::render::viewer {

namespace {

struct PreviewPlaceableStateLoad {
    bool loaded = false;
    int32_t appearance = -1;
    int32_t light_color = -1;
    std::string error;
};

struct PreviewDoorStateLoad {
    bool loaded = false;
    int32_t appearance = 0;
    int32_t generic_type = 0;
    std::string error;
};

bool is_json_authored_resource_path(const std::filesystem::path& path)
{
    nw::String ext = path.extension().string();
    nw::string::tolower(&ext);
    return ext == ".json";
}

void log_error_context()
{
    if (nw::error_context_stack) {
        LOG_F(ERROR, "\n{}", nw::get_error_context());
    }
}

bool load_json_from_file(const std::filesystem::path& path, nlohmann::json& out)
{
    std::ifstream input{path, std::ifstream::binary};
    if (!input) {
        LOG_F(ERROR, "Failed to open object preview JSON '{}'", path.string());
        log_error_context();
        return false;
    }

    try {
        input >> out;
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Failed to parse object preview JSON '{}': {}", path.string(), e.what());
        log_error_context();
        return false;
    }
    return true;
}

nw::smalls::Value script_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) {
        return {};
    }

    uint32_t index = def->field_index(field);
    if (index == UINT32_MAX) {
        return {};
    }

    const auto& fd = def->fields[index];
    return rt.read_value_field_at_offset(value, fd.offset, fd.type_id);
}

int32_t script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, int32_t fallback = 0)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.int_type() ? field_value.data.ival : fallback;
}

float script_float_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.float_type() ? field_value.data.fval : 0.0f;
}

bool script_bool_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.bool_type() && field_value.data.bval;
}

std::string script_string_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    if (field_value.type_id != rt.string_type()) {
        return {};
    }
    return std::string{nw::smalls::ScriptString{field_value.data.hptr}.view(rt)};
}

nw::Resref script_resref_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    nw::smalls::TypeID resref_type = rt.type_id("core.types.ResRef", false);
    if (field_value.type_id != resref_type) {
        return {};
    }

    const auto* resref = static_cast<const nw::Resref*>(rt.get_value_data_ptr(field_value));
    return resref ? *resref : nw::Resref{};
}

PreviewDoorModelLoad door_model_from_script_result(
    nw::smalls::Runtime& rt,
    const nw::smalls::ExecutionResult& executed,
    std::string_view function)
{
    PreviewDoorModelLoad result;
    if (!executed.ok()) {
        result.error = fmt::format("nwn1.doors.{} failed: {}", function, executed.error_message);
        return result;
    }

    result.model = script_resref_field(rt, executed.value, "model");
    result.error = script_string_field(rt, executed.value, "error");
    result.table = script_string_field(rt, executed.value, "table");
    result.column = script_string_field(rt, executed.value, "column");
    result.selector = script_string_field(rt, executed.value, "selector");
    result.row = script_int_field(rt, executed.value, "row", -1);
    result.generic = script_bool_field(rt, executed.value, "generic");
    result.valid = script_bool_field(rt, executed.value, "valid") && !result.model.empty();

    if (!result.valid && result.error.empty()) {
        result.error = fmt::format("nwn1.doors.{} returned no model", function);
    }
    return result;
}

PreviewDoorStateLoad load_door_state_from_json(const std::filesystem::path& path)
{
    PreviewDoorStateLoad result;
    std::ifstream input{path, std::ifstream::binary};
    if (!input) {
        result.error = fmt::format("failed to open door preview JSON '{}'", path.string());
        return result;
    }

    nlohmann::json archive;
    try {
        archive = nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception& e) {
        result.error = fmt::format("failed to parse door preview JSON '{}': {}", path.string(), e.what());
        return result;
    }

    const auto state = archive.find("core.door.DoorState");
    if (state == archive.end() || !state->is_object()) {
        result.error = fmt::format("door preview JSON '{}' missing core.door.DoorState", path.string());
        return result;
    }

    try {
        auto appearance = state->find("appearance");
        if (appearance == state->end() || !appearance->is_number_integer()) {
            result.error = fmt::format("door preview JSON '{}' missing integer appearance", path.string());
            return result;
        }
        result.appearance = appearance->get<int32_t>();
        if (auto generic_type = state->find("generic_type"); generic_type != state->end()) {
            if (!generic_type->is_number_integer()) {
                result.error = fmt::format("door preview JSON '{}' has non-integer generic_type", path.string());
                return result;
            }
            result.generic_type = generic_type->get<int32_t>();
        }
    } catch (const nlohmann::json::exception& e) {
        result.error = fmt::format("invalid door preview JSON '{}': {}", path.string(), e.what());
        return result;
    }

    result.loaded = true;
    return result;
}

PreviewDoorStateLoad load_door_state_from_gff(const std::filesystem::path& path)
{
    PreviewDoorStateLoad result;

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        result.error = fmt::format("failed to read door preview GFF '{}': empty file", path.string());
        return result;
    }

    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        result.error = fmt::format("failed to read door preview GFF '{}'", path.string());
        return result;
    }

    uint32_t appearance = 0;
    if (!gff.toplevel().get_to("Appearance", appearance)) {
        result.error = fmt::format("door preview GFF '{}' missing Appearance", path.string());
        return result;
    }
    if (appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        result.error = fmt::format("door preview GFF '{}' Appearance {} is out of range",
            path.string(), appearance);
        return result;
    }

    uint32_t generic_type = 0;
    uint8_t legacy_generic_type = 0;
    if (!gff.toplevel().get_to("GenericType_New", generic_type, false)
        && gff.toplevel().get_to("GenericType", legacy_generic_type, false)) {
        generic_type = legacy_generic_type;
    }
    if (generic_type > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        result.error = fmt::format("door preview GFF '{}' generic type {} is out of range",
            path.string(), generic_type);
        return result;
    }

    result.appearance = static_cast<int32_t>(appearance);
    result.generic_type = static_cast<int32_t>(generic_type);
    result.loaded = true;
    return result;
}

PreviewDoorStateLoad load_door_state_from_file(const std::filesystem::path& path)
{
    return is_json_authored_resource_path(path)
        ? load_door_state_from_json(path)
        : load_door_state_from_gff(path);
}

PreviewPlaceableStateLoad load_placeable_state_from_json(const std::filesystem::path& path)
{
    PreviewPlaceableStateLoad result;
    std::ifstream input{path, std::ifstream::binary};
    if (!input) {
        result.error = fmt::format("failed to open placeable preview JSON '{}'", path.string());
        return result;
    }

    nlohmann::json archive;
    try {
        archive = nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception& e) {
        result.error = fmt::format("failed to parse placeable preview JSON '{}': {}", path.string(), e.what());
        return result;
    }

    const auto state = archive.find("core.placeable.PlaceableState");
    if (state == archive.end() || !state->is_object()) {
        result.error = fmt::format("placeable preview JSON '{}' missing core.placeable.PlaceableState", path.string());
        return result;
    }

    try {
        auto appearance = state->find("appearance");
        if (appearance == state->end() || !appearance->is_number_integer()) {
            result.error = fmt::format("placeable preview JSON '{}' missing integer appearance", path.string());
            return result;
        }
        result.appearance = appearance->get<int32_t>();
        if (auto light_color = state->find("light_color"); light_color != state->end()) {
            if (!light_color->is_number_integer()) {
                result.error = fmt::format("placeable preview JSON '{}' has non-integer light_color", path.string());
                return result;
            }
            result.light_color = light_color->get<int32_t>();
        }
    } catch (const nlohmann::json::exception& e) {
        result.error = fmt::format("invalid placeable preview JSON '{}': {}", path.string(), e.what());
        return result;
    }

    result.loaded = true;
    return result;
}

PreviewPlaceableStateLoad load_placeable_state_from_gff(const std::filesystem::path& path)
{
    PreviewPlaceableStateLoad result;

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        result.error = fmt::format("failed to read placeable preview GFF '{}': empty file", path.string());
        return result;
    }

    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        result.error = fmt::format("failed to read placeable preview GFF '{}'", path.string());
        return result;
    }

    uint32_t appearance = 0;
    if (!gff.toplevel().get_to("Appearance", appearance)) {
        result.error = fmt::format("placeable preview GFF '{}' missing Appearance", path.string());
        return result;
    }
    if (appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        result.error = fmt::format("placeable preview GFF '{}' Appearance {} is out of range",
            path.string(), appearance);
        return result;
    }

    result.appearance = static_cast<int32_t>(appearance);
    gff.toplevel().get_to("LightColor", result.light_color, false);
    result.loaded = true;
    return result;
}

PreviewPlaceableStateLoad load_placeable_state_from_file(const std::filesystem::path& path)
{
    return is_json_authored_resource_path(path)
        ? load_placeable_state_from_json(path)
        : load_placeable_state_from_gff(path);
}

} // namespace

bool is_object_preview_path(std::string_view value)
{
    auto path = std::filesystem::path{value};
    if (!std::filesystem::exists(path)) {
        return false;
    }

    const nw::Resource resource = nw::Resource::from_path(path, false);
    return resource.valid()
        && (resource.type == nw::ResourceType::bic
            || resource.type == nw::ResourceType::utc
            || resource.type == nw::ResourceType::uti
            || resource.type == nw::ResourceType::utd
            || resource.type == nw::ResourceType::utp);
}

bool load_player_from_file(const std::filesystem::path& path, nw::Player& out)
{
    if (is_json_authored_resource_path(path)) {
        nlohmann::json json;
        if (!load_json_from_file(path, json)) { return false; }
        if (!nw::deserialize(&out, json)) {
            LOG_F(ERROR, "Failed to deserialize player preview JSON '{}'", path.string());
            log_error_context();
            return false;
        }
        return true;
    }

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        LOG_F(ERROR, "Failed to read player preview GFF '{}': empty file", path.string());
        log_error_context();
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        LOG_F(ERROR, "Failed to read player preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    if (!nw::deserialize(&out, gff.toplevel())) {
        LOG_F(ERROR, "Failed to deserialize player preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    return true;
}

bool load_creature_from_file(const std::filesystem::path& path, nw::Creature& out)
{
    if (is_json_authored_resource_path(path)) {
        nlohmann::json json;
        if (!load_json_from_file(path, json)) { return false; }
        if (!nw::deserialize(&out, json, nw::SerializationProfile::blueprint)) {
            LOG_F(ERROR, "Failed to deserialize creature preview JSON '{}'", path.string());
            log_error_context();
            return false;
        }
        return true;
    }

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        LOG_F(ERROR, "Failed to read creature preview GFF '{}': empty file", path.string());
        log_error_context();
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        LOG_F(ERROR, "Failed to read creature preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    if (!nw::deserialize(&out, gff.toplevel(), nw::SerializationProfile::instance)) {
        LOG_F(ERROR, "Failed to deserialize creature preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    return true;
}

bool load_item_from_file(const std::filesystem::path& path, nw::Item& out)
{
    if (is_json_authored_resource_path(path)) {
        nlohmann::json json;
        if (!load_json_from_file(path, json)) { return false; }
        if (!nw::deserialize(&out, json, nw::SerializationProfile::blueprint)) {
            LOG_F(ERROR, "Failed to deserialize item preview JSON '{}'", path.string());
            log_error_context();
            return false;
        }
        return true;
    }

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        LOG_F(ERROR, "Failed to read item preview GFF '{}': empty file", path.string());
        log_error_context();
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        LOG_F(ERROR, "Failed to read item preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    if (!nw::deserialize(&out, gff.toplevel(), nw::SerializationProfile::instance)) {
        LOG_F(ERROR, "Failed to deserialize item preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    return true;
}

bool load_door_from_file(const std::filesystem::path& path, nw::Door& out)
{
    if (is_json_authored_resource_path(path)) {
        nlohmann::json json;
        if (!load_json_from_file(path, json)) { return false; }
        if (!nw::deserialize(&out, json, nw::SerializationProfile::blueprint)) {
            LOG_F(ERROR, "Failed to deserialize door preview JSON '{}'", path.string());
            log_error_context();
            return false;
        }
        return true;
    }

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        LOG_F(ERROR, "Failed to read door preview GFF '{}': empty file", path.string());
        log_error_context();
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        LOG_F(ERROR, "Failed to read door preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    if (!nw::deserialize(&out, gff.toplevel(), nw::SerializationProfile::instance)) {
        LOG_F(ERROR, "Failed to deserialize door preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    return true;
}

bool load_placeable_from_file(const std::filesystem::path& path, nw::Placeable& out)
{
    if (is_json_authored_resource_path(path)) {
        nlohmann::json json;
        if (!load_json_from_file(path, json)) { return false; }
        if (!nw::deserialize(&out, json, nw::SerializationProfile::blueprint)) {
            LOG_F(ERROR, "Failed to deserialize placeable preview JSON '{}'", path.string());
            log_error_context();
            return false;
        }
        return true;
    }

    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        LOG_F(ERROR, "Failed to read placeable preview GFF '{}': empty file", path.string());
        log_error_context();
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        LOG_F(ERROR, "Failed to read placeable preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    if (!nw::deserialize(&out, gff.toplevel(), nw::SerializationProfile::instance)) {
        LOG_F(ERROR, "Failed to deserialize placeable preview GFF '{}'", path.string());
        log_error_context();
        return false;
    }
    return true;
}

PreviewPlaceableVisualLoad load_placeable_visual_from_file(const std::filesystem::path& path)
{
    PreviewPlaceableStateLoad state = load_placeable_state_from_file(path);
    if (!state.loaded) {
        return PreviewPlaceableVisualLoad{
            .loaded = false,
            .error = std::move(state.error),
        };
    }

    PreviewPlaceableVisualLoad result{
        .loaded = true,
        .visual = nw::ObjectVisualState{.appearance = state.appearance},
    };

    auto& rt = nw::kernel::runtime();
    {
        nw::Vector<nw::smalls::Value> args;
        args.push_back(nw::smalls::Value::make_int(state.appearance));
        auto executed = rt.execute_script("nwn1.placeables", "resolve_placeable_model_by_appearance", args);
        if (!executed.ok()) {
            result.error = fmt::format("nwn1.placeables.resolve_placeable_model_by_appearance failed: {}",
                executed.error_message);
            return result;
        }

        result.error = script_string_field(rt, executed.value, "error");
        nw::Resref model = script_resref_field(rt, executed.value, "model");
        if (script_bool_field(rt, executed.value, "valid") && !model.empty()) {
            result.visual.models.push_back(nw::ObjectVisualModel{
                .model = model,
                .kind = 0,
                .slot = -1,
            });
        } else if (result.error.empty()) {
            result.error = fmt::format("appearance {} produced no model", state.appearance);
        }
    }

    {
        nw::Vector<nw::smalls::Value> args;
        args.push_back(nw::smalls::Value::make_int(state.appearance));
        args.push_back(nw::smalls::Value::make_int(state.light_color));
        auto executed = rt.execute_script("nwn1.placeables", "resolve_placeable_lighting_by_state", args);
        if (!executed.ok() || !script_bool_field(rt, executed.value, "valid")) {
            return result;
        }

        result.visual.lights.push_back(nw::ObjectVisualLight{
            .light_offset = glm::vec3{
                script_float_field(rt, executed.value, "offset_x"),
                script_float_field(rt, executed.value, "offset_y"),
                script_float_field(rt, executed.value, "offset_z"),
            },
            .light_color = script_int_field(rt, executed.value, "color", -1),
        });
    }

    return result;
}

const nw::ObjectVisualState* placeable_visual_state(const nw::Placeable& placeable) noexcept
{
    return object_visual_state(placeable);
}

const nw::ObjectVisualState* object_visual_state(const nw::ObjectBase& object) noexcept
{
    return nw::kernel::objects().components().find_visual(object.handle());
}

bool update_standalone_item_visual(nw::Item& item, bool use_default_fallback, std::string_view origin)
{
    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item.handle()));
    args.push_back(nw::smalls::Value::make_bool(use_default_fallback));

    auto result = rt.execute_script("nwn1.item", "update_standalone_visual", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to update standalone item visual '{}': {}", origin, result.error_message);
        return false;
    }
    if (result.value.type_id != rt.bool_type() || !result.value.data.bval) {
        LOG_F(WARNING, "nwn1: standalone item visual '{}' returned false", origin);
        return false;
    }
    return true;
}

bool visual_row_visible_for_mode(const nw::ObjectVisualModel& row, nw::ObjectVisualRenderMode render_mode) noexcept
{
    return nw::object_visual_model_visible_in_mode(row, render_mode);
}

const nw::ObjectVisualModel* first_valid_visual_model(
    std::span<const nw::ObjectVisualModel> models,
    nw::ObjectVisualRenderMode render_mode) noexcept
{
    auto it = std::find_if(models.begin(), models.end(),
        [render_mode](const nw::ObjectVisualModel& model) {
            return visual_row_visible_for_mode(model, render_mode)
                && !model.model.empty();
        });
    return it != models.end() ? &*it : nullptr;
}

const nw::ObjectVisualModel* first_valid_visual_model(
    const nw::ObjectVisualState* visual,
    nw::ObjectVisualRenderMode render_mode) noexcept
{
    return visual ? first_valid_visual_model(std::span<const nw::ObjectVisualModel>{visual->models}, render_mode) : nullptr;
}

const nw::ObjectVisualModel* first_valid_visual_model(std::span<const nw::ObjectVisualModel> models) noexcept
{
    return first_valid_visual_model(models, nw::ObjectVisualRenderMode::toolset);
}

const nw::ObjectVisualModel* first_valid_visual_model(const nw::ObjectVisualState* visual) noexcept
{
    return first_valid_visual_model(visual, nw::ObjectVisualRenderMode::toolset);
}

std::string placeable_visual_error(const nw::ObjectVisualState* visual)
{
    if (!visual) {
        return "no visual state was registered";
    }
    if (visual->models.empty()) {
        return fmt::format("appearance {} produced no visual models", visual->appearance);
    }
    return fmt::format("appearance {} produced no valid model resources", visual->appearance);
}

PreviewDoorModelLoad resolve_door_model_from_file(const std::filesystem::path& path)
{
    PreviewDoorStateLoad state = load_door_state_from_file(path);
    if (!state.loaded) {
        return PreviewDoorModelLoad{.error = std::move(state.error)};
    }

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(state.appearance));
    args.push_back(nw::smalls::Value::make_int(state.generic_type));
    auto executed = rt.execute_script("nwn1.doors", "resolve_door_model_by_state", args);
    return door_model_from_script_result(rt, executed, "resolve_door_model_by_state");
}

PreviewDoorModelLoad resolve_door_model_for_object(const nw::Door& door)
{
    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, door.handle()));
    auto executed = rt.execute_script("nwn1.doors", "resolve_door_model", args);
    return door_model_from_script_result(rt, executed, "resolve_door_model");
}

std::string door_model_lookup_context(const PreviewDoorModelLoad& lookup)
{
    return fmt::format("{} {} in {}.2da", lookup.selector, lookup.row, lookup.table);
}

std::string area_object_origin(
    std::string_view area,
    std::string_view kind,
    size_t index,
    const nw::ObjectBase& object)
{
    if (object.tag) {
        return fmt::format("{}:{}:{}:{}", area, kind, index, object.tag.view());
    }
    const auto resref = object.resref;
    if (!resref.empty()) {
        return fmt::format("{}:{}:{}:{}", area, kind, index, resref.view());
    }
    return fmt::format("{}:{}:{}", area, kind, index);
}

glm::mat4 area_object_placement_transform(const nw::Location& location)
{
    constexpr float k_epsilon = 1.0e-5f;
    float angle = 0.0f;
    if (std::abs(location.orientation.x) > k_epsilon || std::abs(location.orientation.y) > k_epsilon) {
        angle = std::atan2(location.orientation.y, location.orientation.x);
    }

    glm::mat4 placement = glm::translate(glm::mat4{1.0f}, location.position);
    placement *= glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
    return placement;
}

} // namespace nw::render::viewer
