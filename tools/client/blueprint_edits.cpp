#include "blueprint_edits.hpp"

#include "resource_document.hpp"
#include "workspace.hpp"

#include <nw/kernel/Strings.hpp>
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
#include <nw/profiles/nwn1/item_materialization.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/component_propset_json.hpp>
#include <nw/smalls/runtime.hpp>

#include <absl/container/flat_hash_set.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace nw::toolset {
namespace {

constexpr std::array blueprint_type_catalog{
    BlueprintTypeDefinition{ObjectType::creature, ResourceType::utc, "Creature", "blueprints/creatures", "creatures"},
    BlueprintTypeDefinition{ObjectType::door, ResourceType::utd, "Door", "blueprints/doors", "doors"},
    BlueprintTypeDefinition{ObjectType::encounter, ResourceType::ute, "Encounter", "blueprints/encounters", "encounters"},
    BlueprintTypeDefinition{ObjectType::item, ResourceType::uti, "Item", "blueprints/items", "items"},
    BlueprintTypeDefinition{ObjectType::placeable, ResourceType::utp, "Placeable", "blueprints/placeables", "placeables"},
    BlueprintTypeDefinition{ObjectType::sound, ResourceType::uts, "Sound", "blueprints/sounds", "sounds"},
    BlueprintTypeDefinition{ObjectType::store, ResourceType::utm, "Store", "blueprints/stores", "stores"},
    BlueprintTypeDefinition{ObjectType::trigger, ResourceType::utt, "Trigger", "blueprints/triggers", "triggers"},
    BlueprintTypeDefinition{ObjectType::waypoint, ResourceType::utw, "Waypoint", "blueprints/waypoints", "waypoints"},
};

bool inside(const fs::path& root, const fs::path& target)
{
    const auto relative = target.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute()
        && std::none_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; });
}

bool read_file(const fs::path& path, std::string& bytes, std::string& error)
{
    std::ifstream input{path, std::ios::binary};
    bytes.assign(std::istreambuf_iterator<char>{input}, {});
    if (!input.is_open() || input.bad()) {
        error = "Failed to read blueprint: " + path.string();
        return false;
    }
    return true;
}

bool snapshot(ObjectHandle handle, SerializationProfile profile, nlohmann::json& value, std::string& error)
{
    auto* object = kernel::objects().get_object_base(handle);
    if (!object || blueprint_resource_type(handle.type) == ResourceType::invalid) {
        error = "Blueprint source is unavailable, stale, or unsupported";
        return false;
    }
    const auto result = object_to_component_propset_json(object, value, &kernel::runtime(), profile);
    error = result.error;
    return result.ok;
}

void strip_blueprint_identity(nlohmann::json& value)
{
    value.at("object").erase("uuid");
}

template <typename T>
bool load_resource(Resource resource, ObjectDocument& document, std::string& error)
{
    auto data = kernel::resman().demand(resource);
    if (data.bytes.size() == 0) {
        error = "Missing blueprint: " + resource.filename();
        return false;
    }
    auto* object = kernel::objects().make<T>();
    if (!object || !document.adopt(object->handle())) {
        if (object) { kernel::objects().destroy(object->handle()); }
        error = "Failed to allocate blueprint validation object";
        return false;
    }
    bool loaded = false;
    if (string::startswith(data.bytes.string_view(), T::serial_id)) {
        Gff archive{std::move(data)};
        loaded = archive.valid() && deserialize(object, archive.toplevel(), SerializationProfile::blueprint);
    } else {
        const auto value = nlohmann::json::parse(data.bytes.string_view());
        loaded = deserialize(object, value, SerializationProfile::blueprint);
    }
    if (!loaded) {
        error = "Failed to load blueprint: " + resource.filename();
        return false;
    }
    if constexpr (std::is_same_v<T, Item>) {
        const auto result = nwn1::materialize_item_native_components(*object, kernel::runtime());
        if (!result) {
            error = result.error;
            return false;
        }
    }
    return true;
}

std::vector<Resource> item_dependencies(const nlohmann::json& value)
{
    std::vector<Resource> result;
    const auto& components = value.at("components");
    if (const auto inventory = components.find("inventory"); inventory != components.end()) {
        if (!inventory->is_array()) { throw std::runtime_error("Blueprint inventory must be an array"); }
        for (const auto& row : *inventory) {
            const auto reference = row.at("item").get<std::string>();
            if (reference.empty()) { throw std::runtime_error("Empty inventory blueprint reference"); }
            result.emplace_back(reference, ResourceType::uti);
        }
    }
    if (const auto equipment = components.find("equipment"); equipment != components.end() && !equipment->is_null()) {
        if (!equipment->is_object()) { throw std::runtime_error("Blueprint equipment must be an object"); }
        for (const auto& reference : *equipment) {
            const auto text = reference.get<std::string>();
            if (text.empty()) { throw std::runtime_error("Empty equipment blueprint reference"); }
            result.emplace_back(text, ResourceType::uti);
        }
    }
    return result;
}

bool validate_dependencies(Resource root, const nlohmann::json& value,
    absl::flat_hash_map<Resource, nlohmann::json>& snapshots, std::string& error)
{
    struct Frame {
        Resource resource;
        std::vector<Resource> children;
        size_t next = 0;
    };
    std::vector<Frame> pending{{root, item_dependencies(value), 0}};
    absl::flat_hash_set<Resource> active{root};
    absl::flat_hash_set<Resource> complete;
    while (!pending.empty()) {
        auto& frame = pending.back();
        if (frame.next == frame.children.size()) {
            active.erase(frame.resource);
            complete.insert(frame.resource);
            pending.pop_back();
            continue;
        }
        const Resource resource = frame.children[frame.next++];
        if (active.contains(resource)) {
            error = "Cyclic blueprint dependency: " + resource.filename();
            return false;
        }
        if (complete.contains(resource)) { continue; }
        if (!snapshots.contains(resource)) {
            ObjectDocument loaded;
            nlohmann::json child;
            if (!load_resource<Item>(resource, loaded, error)
                || !snapshot(loaded.object(), SerializationProfile::blueprint, child, error)) { return false; }
            if (child.at("object").at("resref").get<Resref>() != resource.resref) {
                error = "Dependency resource key and internal ResRef disagree: " + resource.filename();
                return false;
            }
            strip_blueprint_identity(child);
            snapshots.emplace(resource, std::move(child));
        }
        active.insert(resource);
        pending.push_back({resource, item_dependencies(snapshots.at(resource)), 0});
    }
    return true;
}

bool validate_nested_items(ObjectHandle source, absl::flat_hash_map<Resource, nlohmann::json>& saved_items, std::string& error)
{
    std::vector<ObjectHandle> pending{source};
    absl::flat_hash_set<uint64_t> visited;
    for (size_t index = 0; index < pending.size(); ++index) {
        const auto handle = pending[index];
        if (!visited.insert(handle.to_ull()).second) {
            error = "Repeated or cyclic item ownership in blueprint source";
            return false;
        }
        auto* object = kernel::objects().get_object_base(handle);
        if (!object) {
            error = "Stale nested blueprint object";
            return false;
        }
        if (index != 0) {
            const Resource resource{object->resref, ResourceType::uti};
            if (!resource.valid()) {
                error = "Nested item has no blueprint reference; save the item as a blueprint first";
                return false;
            }
            if (!saved_items.contains(resource)) {
                ObjectDocument saved;
                nlohmann::json value;
                if (!load_resource<Item>(resource, saved, error)
                    || !snapshot(saved.object(), SerializationProfile::blueprint, value, error)) {
                    return false;
                }
                strip_blueprint_identity(value);
                saved_items.emplace(resource, std::move(value));
            }
            nlohmann::json actual;
            if (!snapshot(handle, SerializationProfile::blueprint, actual, error)) { return false; }
            strip_blueprint_identity(actual);
            if (actual != saved_items.at(resource)) {
                error = "Nested item " + resource.filename() + " has edits that blueprint references cannot preserve; update or save that item first";
                return false;
            }
        }
        const auto append = [&](const auto& item) {
            if (item.template is<ObjectHandle>()) {
                pending.push_back(item.template as<ObjectHandle>());
            } else if (item.template is<Resref>() && !item.template as<Resref>().empty()) {
                const Resource resource{item.template as<Resref>(), ResourceType::uti};
                if (!kernel::resman().contains(resource)) {
                    error = "Missing nested item blueprint: " + resource.filename();
                }
            }
        };
        if (const auto* inventory = kernel::objects().components().find_inventory(*object)) {
            for (const auto& item : inventory->items) {
                append(item.item);
            }
        }
        if (auto* creature = object->as_creature()) {
            for (const auto& item : creature->equipment.equips) {
                append(item);
            }
        }
        if (!error.empty()) { return false; }
    }
    return true;
}

bool load_blueprint_copy(ObjectType type, const nlohmann::json& value,
    ObjectDocument& owner, std::string& error)
{
    ObjectBase* object = nullptr;
    switch (type) {
    case ObjectType::creature:
        object = kernel::objects().make<Creature>();
        break;
    case ObjectType::door:
        object = kernel::objects().make<Door>();
        break;
    case ObjectType::encounter:
        object = kernel::objects().make<Encounter>();
        break;
    case ObjectType::placeable:
        object = kernel::objects().make<Placeable>();
        break;
    case ObjectType::item:
        object = kernel::objects().make<Item>();
        break;
    case ObjectType::sound:
        object = kernel::objects().make<Sound>();
        break;
    case ObjectType::store:
        object = kernel::objects().make<Store>();
        break;
    case ObjectType::trigger:
        object = kernel::objects().make<Trigger>();
        break;
    case ObjectType::waypoint:
        object = kernel::objects().make<Waypoint>();
        break;
    default:
        break;
    }
    if (!object || !owner.adopt(object->handle())) {
        if (object) { kernel::objects().destroy(object->handle()); }
        error = "Failed to allocate blueprint validation root";
        return false;
    }
    const auto result = object_from_component_propset_json(object, value, &kernel::runtime(), SerializationProfile::instance);
    error = result.error;
    return result.ok;
}

} // namespace

bool snapshot_blueprints(std::span<const Resource> resources,
    absl::flat_hash_map<Resource, nlohmann::json>& output, std::string& error)
{
    output.clear();
    error.clear();
    try {
        for (const auto resource : resources) {
            ObjectDocument owner;
            bool loaded = false;
            switch (resource.type) {
            case ResourceType::utc:
                loaded = load_resource<Creature>(resource, owner, error);
                break;
            case ResourceType::utd:
                loaded = load_resource<Door>(resource, owner, error);
                break;
            case ResourceType::ute:
                loaded = load_resource<Encounter>(resource, owner, error);
                break;
            case ResourceType::utp:
                loaded = load_resource<Placeable>(resource, owner, error);
                break;
            case ResourceType::uti:
                loaded = load_resource<Item>(resource, owner, error);
                break;
            case ResourceType::uts:
                loaded = load_resource<Sound>(resource, owner, error);
                break;
            case ResourceType::utm:
                loaded = load_resource<Store>(resource, owner, error);
                break;
            case ResourceType::utt:
                loaded = load_resource<Trigger>(resource, owner, error);
                break;
            case ResourceType::utw:
                loaded = load_resource<Waypoint>(resource, owner, error);
                break;
            default:
                error = "Unsupported blueprint type";
                break;
            }
            nlohmann::json value;
            if (!loaded || !snapshot(owner.object(), SerializationProfile::blueprint, value, error)) { break; }
            if (value.at("object").at("resref").get<Resref>() != resource.resref) {
                error = "Blueprint key and internal ResRef disagree: " + resource.filename();
                break;
            }
            strip_blueprint_identity(value);
            output[resource] = std::move(value);
            if (!validate_dependencies(resource, output.at(resource), output, error)) { break; }
        }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    if (!error.empty()) { output.clear(); }
    return error.empty();
}

InitializedBlueprints initialize_blueprints(std::span<const BlueprintCreationRequest> rows)
{
    InitializedBlueprints result;
    if (rows.empty()) { return result; }
    try {
        result.roots.reserve(rows.size());
        absl::flat_hash_set<Resource> destinations;
        for (const auto& row : rows) {
            std::string normalized;
            if (!validate_blueprint_resref(row.destination.resref.view(), normalized, result.error)) { break; }
            if (!row.name.empty()
                && std::ranges::all_of(row.name, [](const unsigned char ch) {
                       return std::isspace(ch);
                   })) {
                result.error = "Blueprint Name must contain non-whitespace text";
                break;
            }
            if (!row.last_name.empty()
                && std::ranges::all_of(row.last_name, [](const unsigned char ch) {
                       return std::isspace(ch);
                   })) {
                result.error = "Creature Last Name must contain non-whitespace text or be empty";
                break;
            }
            if (!destinations.insert(row.destination).second || kernel::resman().contains(row.destination)) {
                result.error = "Blueprint ResRef already exists: " + row.destination.filename();
                break;
            }
            const auto object_type = blueprint_object_type(row.destination.type);
            if (object_type != ObjectType::item && row.base_item != -1) {
                result.error = "Only Item blueprints accept a base-item type";
                break;
            }
            if (object_type != ObjectType::creature
                && (row.race != -1 || row.class_id != -1
                    || !row.last_name.empty())) {
                result.error = "Only Creature blueprints accept a last name, race, and class selections";
                break;
            }
            if (object_type == ObjectType::creature
                && (row.race < 0 || row.class_id < 0)) {
                result.error = "Choose a race and class for the Creature blueprint";
                break;
            }
            ObjectBase* object = nullptr;
            switch (blueprint_object_type(row.destination.type)) {
            case ObjectType::creature:
                object = kernel::objects().make<Creature>();
                break;
            case ObjectType::door:
                object = kernel::objects().make<Door>();
                break;
            case ObjectType::encounter:
                object = kernel::objects().make<Encounter>();
                break;
            case ObjectType::placeable:
                object = kernel::objects().make<Placeable>();
                break;
            case ObjectType::item:
                object = kernel::objects().make<Item>();
                break;
            case ObjectType::sound:
                object = kernel::objects().make<Sound>();
                break;
            case ObjectType::store:
                object = kernel::objects().make<Store>();
                break;
            case ObjectType::trigger:
                object = kernel::objects().make<Trigger>();
                break;
            case ObjectType::waypoint:
                object = kernel::objects().make<Waypoint>();
                break;
            default:
                break;
            }
            ObjectDocument owner;
            if (!object || !owner.adopt(object->handle())) {
                if (object) { kernel::objects().destroy(object->handle()); }
                result.error = "Unsupported blueprint type or allocation failure";
                break;
            }
            result.roots.push_back(std::move(owner));
        }
        if (result.error.empty()) {
            std::vector<smalls::ProfileBlueprintInitialization> initializations;
            initializations.reserve(rows.size());
            for (size_t index = 0; index < rows.size(); ++index) {
                const std::string_view name = rows[index].name.empty()
                    ? rows[index].destination.resref.view()
                    : std::string_view{rows[index].name};
                initializations.push_back({result.roots[index].object(), name,
                    rows[index].last_name,
                    rows[index].race, rows[index].class_id,
                    rows[index].base_item});
            }
            String diagnostic;
            if (!kernel::runtime().profile_initialize_blueprints(
                    initializations, diagnostic)) {
                result.error = diagnostic.empty()
                    ? "Selected profile could not initialize the blueprint batch"
                    : diagnostic.c_str();
            }
        }
        if (result.error.empty()) {
            for (size_t index = 0; index < rows.size(); ++index) {
                auto* object = kernel::objects().get_object_base(result.roots[index].object());
                const auto name = rows[index].name.empty()
                    ? rows[index].destination.resref.view()
                    : std::string_view{rows[index].name};
                if (object->handle().type != ObjectType::creature
                    && !object->name.add(LanguageID::english, name)) {
                    result.error = "Blueprint name initialization failed";
                    break;
                }
                object->resref = rows[index].destination.resref;
                object->tag = kernel::strings().intern(object->resref.view());
                if (!object->instantiate()) {
                    result.error = "Blueprint initialization failed";
                    break;
                }
            }
        }
    } catch (const std::exception& ex) {
        result.error = "Blueprint creation failed: " + std::string{ex.what()};
    }
    if (!result.error.empty()) { result.roots.clear(); }
    return result;
}

std::span<const BlueprintTypeDefinition> blueprint_types() noexcept
{
    return blueprint_type_catalog;
}

ResourceType::type blueprint_resource_type(ObjectType type) noexcept
{
    const auto found = std::ranges::find(blueprint_type_catalog, type,
        &BlueprintTypeDefinition::object_type);
    return found == blueprint_type_catalog.end()
        ? ResourceType::invalid
        : found->resource_type;
}

ObjectType blueprint_object_type(ResourceType::type type) noexcept
{
    const auto found = std::ranges::find(blueprint_type_catalog, type,
        &BlueprintTypeDefinition::resource_type);
    return found == blueprint_type_catalog.end()
        ? ObjectType::invalid
        : found->object_type;
}

fs::path default_blueprint_directory(ResourceType::type type)
{
    const auto found = std::ranges::find(blueprint_type_catalog, type,
        &BlueprintTypeDefinition::resource_type);
    return found == blueprint_type_catalog.end()
        ? fs::path{}
        : fs::path{found->directory};
}

bool validate_blueprint_resref(std::string_view text, std::string& normalized, std::string& error)
{
    normalized.clear();
    error.clear();
    // Reserve the authored suffix within the common 255-byte filename boundary.
    if (text.empty() || text.size() > std::min<size_t>(Resref::maximum_size, 246)) {
        error = "ResRef must contain between 1 and 246 letters, digits, or underscores";
        return false;
    }
    for (const unsigned char ch : text) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) {
            error = "ResRef must contain only letters, digits, or underscores, without a path or extension";
            return false;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    const bool numbered_device = normalized.size() == 4
        && (normalized.starts_with("com") || normalized.starts_with("lpt"))
        && normalized[3] >= '1' && normalized[3] <= '9';
    if (numbered_device || normalized == "con" || normalized == "prn" || normalized == "aux" || normalized == "nul") {
        error = "ResRef is a reserved filesystem name";
        normalized.clear();
        return false;
    }
    return true;
}

PreparedBlueprintWrites prepare_blueprint_writes(const fs::path& project,
    WorkspaceState& workspace, std::span<const BlueprintWriteRequest> requests)
{
    PreparedBlueprintWrites result;
    if (requests.empty()) { return result; }
    auto& resources = kernel::resman();
    if (project.empty() || resources.module_format() != ModuleResourceFormat::native_json || !resources.module_container()) {
        result.error = "Blueprint authoring requires an active native project";
        return result;
    }
    try {
        result.project = fs::canonical(project);
        const auto root = fs::canonical(fs::path{resources.module_container()->path()});
        if (fs::exists(root / "package.json")) {
            result.error = "Blueprint authoring requires a flat module resource namespace";
            return result;
        }
        if (!inside(result.project, root)) {
            result.error = "Active module resources are outside this project";
            return result;
        }
        result.resource_generation = resources.generation();
        absl::flat_hash_map<Resource, std::vector<fs::path>> paths;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) { continue; }
            const auto resource = Resource::from_path(entry.path(), false);
            if (blueprint_object_type(resource.type) != ObjectType::invalid) {
                paths[resource].push_back(entry.path());
            }
        }
        absl::flat_hash_set<Resource> destinations;
        auto& saved_items = result.dependency_snapshots;
        for (const auto& request : requests) {
            const auto type = blueprint_object_type(request.destination.type);
            if ((request.kind != BlueprintWriteKind::create && request.kind != BlueprintWriteKind::save_as && request.kind != BlueprintWriteKind::update)
                || type == ObjectType::invalid || request.source.type != type
                || !destinations.insert(request.destination).second) {
                result.error = "Invalid, unsupported, or duplicate blueprint request";
                break;
            }
            std::string normalized;
            if (!validate_blueprint_resref(request.destination.resref.view(), normalized, result.error)) { break; }
            PreparedBlueprintWrite row;
            row.request = request;
            if (!snapshot(request.source, SerializationProfile::instance, row.source_snapshot, result.error)
                || !validate_nested_items(request.source, saved_items, result.error)) { break; }
            const auto& matches = paths[request.destination];
            if (matches.size() > 1) {
                result.error = "Ambiguous duplicate project blueprint: " + request.destination.filename();
                break;
            }
            if (request.kind == BlueprintWriteKind::update) {
                const auto* source = kernel::objects().get_object_base(request.source);
                if (source->resref != request.destination.resref || !resources.contains(request.destination)) {
                    result.error = "Update requires the source's existing blueprint reference; use Save as New Blueprint";
                    break;
                }
                const auto original = resources.demand(request.destination);
                if (original.bytes.size() == 0) {
                    result.error = "The destination blueprint is unavailable";
                    break;
                }
                row.previous_source_bytes = std::string{original.bytes.string_view()};
                if (!matches.empty()) {
                    row.target = fs::canonical(matches.front());
                    if (resources.resource_container(request.destination) != resources.module_container()) {
                        result.error = "This project blueprint is shadowed by a higher-priority resource";
                        break;
                    }
                } else {
                    row.target = root / default_blueprint_directory(request.destination.type) / (request.destination.filename() + ".json");
                    // A base source may be overridden by the module. A hak/custom
                    // override source cannot; preserve resource precedence.
                    if (!resources.module_can_override(request.destination)) {
                        result.error = "Inherited blueprint has higher priority than module resources; a module copy would remain hidden";
                        break;
                    }
                }
            } else {
                if (!matches.empty() || resources.contains(request.destination)) {
                    result.error = "ResRef already exists in the module resource namespace: " + request.destination.filename();
                    break;
                }
                const std::array destination_rows{BlueprintDestination{request.destination, request.directory}};
                const auto validated = validate_blueprint_destinations(result.project, destination_rows);
                if (!validated[0].error.empty()) {
                    result.error = validated[0].error;
                    break;
                }
                row.target = validated[0].target;
            }
            if (!inside(root, row.target) || row.target.extension() != ".json" || fs::is_symlink(row.target)) {
                result.error = "Blueprint destination must be a native JSON file inside the loaded project resource directory";
                break;
            }
            if (!fs::is_directory(row.target.parent_path())) {
                result.error = "Blueprint destination directory does not exist";
                break;
            }
            for (const auto& tab : workspace.tabs()) {
                if (!tab.detail.empty() && fs::weakly_canonical(result.project / tab.detail) == row.target && tab.dirty) {
                    result.error = "Save or discard changes in the destination blueprint tab first: " + tab.title;
                    break;
                }
            }
            if (!result.error.empty()) { break; }
            nlohmann::json value;
            if (!snapshot(request.source, SerializationProfile::blueprint, value, result.error)
                || !validate_dependencies(request.destination, value, saved_items, result.error)) { break; }
            // Copy through the existing instance archive, then edit the detached
            // object's identity. Blueprint serialization owns the export policy.
            if (!load_blueprint_copy(type, row.source_snapshot, row.document, result.error)) { break; }
            auto* copy = kernel::objects().get_object_base(row.document.object());
            copy->resref = request.destination.resref;
            copy->uuid = {};
            if (request.kind == BlueprintWriteKind::update && fs::exists(row.target)) {
                row.expected_bytes.emplace();
                if (!read_file(row.target, *row.expected_bytes, result.error)) { break; }
                const auto previous = nlohmann::json::parse(*row.expected_bytes);
                if (previous.at("object").at("resref").get<Resref>() != request.destination.resref) {
                    result.error = "Blueprint filename and internal reference disagree";
                    break;
                }
            }
            if (!snapshot(copy->handle(), SerializationProfile::blueprint, value, result.error)) { break; }
            strip_blueprint_identity(value);
            row.bytes = value.dump(2) + "\n";
            // A save-only copy needs no runtime activation. Activation can reset
            // authored values (for example Creature current HP), so serialize
            // before preparing copies that will become live editor documents.
            if (request.kind != BlueprintWriteKind::save_as && !copy->instantiate()) {
                result.error = "Blueprint editor object initialization failed";
                break;
            }
            result.rows.push_back(std::move(row));
        }
    } catch (const std::exception& ex) {
        result.error = "Blueprint preparation failed: " + std::string{ex.what()};
    }
    if (!result.error.empty()) { result.rows.clear(); }
    return result;
}

std::vector<BlueprintDestinationResult> validate_blueprint_destinations(
    const fs::path& project, std::span<const BlueprintDestination> rows)
{
    std::vector<BlueprintDestinationResult> results(rows.size());
    for (size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        auto& result = results[index];
        try {
            std::string normalized;
            if (!validate_blueprint_resref(row.resource.resref.view(), normalized, result.error)) { continue; }
            auto& resources = kernel::resman();
            if (project.empty() || !resources.module_container() || resources.module_format() != ModuleResourceFormat::native_json
                || blueprint_object_type(row.resource.type) == ObjectType::invalid || row.directory.empty()) {
                result.error = "Choose a blueprint directory in an active native project";
                continue;
            }
            const auto root = fs::canonical(fs::path{resources.module_container()->path()});
            if (fs::exists(root / "package.json")) {
                result.error = "Blueprint authoring requires a flat module resource namespace";
                continue;
            }
            const auto project_root = fs::canonical(project);
            const auto directory = fs::canonical(row.directory.is_absolute() ? row.directory : project_root / row.directory);
            if (!inside(project_root, root) || !inside(root, directory) || !fs::is_directory(directory)) {
                result.error = "Directory must be inside the loaded project resource root";
                continue;
            }
            result.target = directory / (row.resource.filename() + ".json");
            if (resources.contains(row.resource)) {
                const auto* source = resources.resource_container(row.resource);
                result.error = "ResRef already exists: " + row.resource.filename();
                if (source) { result.error += " in " + source->path(); }
            } else if (fs::exists(result.target) || fs::is_symlink(result.target)) {
                result.error = "Destination already exists: " + result.target.string();
            }
        } catch (const std::exception& ex) {
            result.error = "Invalid blueprint destination: " + std::string{ex.what()};
        }
    }
    return results;
}

std::vector<BlueprintWriteResult> publish_blueprint_writes(WorkspaceState& workspace, PreparedBlueprintWrites& prepared)
{
    std::vector<BlueprintWriteResult> results(prepared.rows.size());
    if (!prepared.ok() || prepared.rows.empty()) { return results; }
    std::string error;
    try {
        auto& resources = kernel::resman();
        if (resources.generation() != prepared.resource_generation || !resources.module_container()
            || resources.module_format() != ModuleResourceFormat::native_json
            || !inside(prepared.project, fs::canonical(fs::path{resources.module_container()->path()}))) {
            throw std::runtime_error("Project resources changed since blueprint preparation");
        }
        const auto root = fs::canonical(fs::path{resources.module_container()->path()});
        absl::flat_hash_map<Resource, size_t> counts;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) { ++counts[Resource::from_path(entry.path(), false)]; }
        }
        // Check the facts captured during preparation without allocating another
        // copy of every source object and its contained objects.
        for (const auto& row : prepared.rows) {
            nlohmann::json source;
            if (!snapshot(row.request.source, SerializationProfile::instance, source, error)) { break; }
            if (source != row.source_snapshot) {
                error = "Blueprint source changed since preparation";
                break;
            }
            const auto expected_count = row.expected_bytes ? 1u : 0u;
            if (counts[row.request.destination] != expected_count || fs::is_symlink(row.target)
                || fs::weakly_canonical(row.target) != row.target || !fs::is_directory(row.target.parent_path())) {
                error = "Blueprint destination changed since preparation";
                break;
            }
            if (row.previous_source_bytes) {
                if (resources.demand(row.request.destination).bytes.string_view() != *row.previous_source_bytes) {
                    error = "Destination blueprint changed since preparation";
                    break;
                }
            } else if (resources.contains(row.request.destination)) {
                error = "Blueprint ResRef is no longer available";
                break;
            }
            if (row.expected_bytes) {
                std::string bytes;
                if (!read_file(row.target, bytes, error)) { break; }
                if (bytes != *row.expected_bytes) {
                    error = "Destination file changed since preparation";
                    break;
                }
            }
            for (const auto& tab : workspace.tabs()) {
                if (tab.dirty && !tab.detail.empty() && fs::weakly_canonical(prepared.project / tab.detail) == row.target) {
                    error = "Save or discard changes in the destination blueprint tab first: " + tab.title;
                    break;
                }
            }
            if (!error.empty()) { break; }
        }
        if (error.empty() && !prepared.dependency_snapshots.empty()) {
            std::vector<Resource> dependencies;
            for (const auto& [resource, value] : prepared.dependency_snapshots) {
                dependencies.push_back(resource);
            }
            absl::flat_hash_map<Resource, nlohmann::json> current;
            if (snapshot_blueprints(dependencies, current, error) && current != prepared.dependency_snapshots) {
                error = "Blueprint item dependencies changed since preparation";
            }
        }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    if (!error.empty()) {
        for (auto& result : results) {
            result.error = error;
        }
        return results;
    }
    std::vector<ResourceFileWrite> writes;
    for (const auto& row : prepared.rows) {
        writes.push_back({row.target, row.bytes,
            row.expected_bytes ? ResourceFileWriteMode::replace : ResourceFileWriteMode::create,
            row.expected_bytes ? std::optional<std::string_view>{*row.expected_bytes} : std::nullopt});
    }
    const auto written = write_resource_files_atomic(writes);
    bool any_written = false;
    for (size_t i = 0; i < results.size(); ++i) {
        results[i].relative_path = prepared.rows[i].target.lexically_relative(prepared.project);
        results[i].saved = written[i].written;
        results[i].error = written[i].error;
        any_written |= written[i].written;
        if (prepared.rows[i].request.kind == BlueprintWriteKind::save_as) {
            prepared.rows[i].document.reset();
        }
    }
    if (!any_written) { return results; }
    refresh_blueprint_writes(workspace, prepared, results);
    return results;
}

void refresh_blueprint_writes(WorkspaceState& workspace, PreparedBlueprintWrites& prepared,
    std::span<BlueprintWriteResult> results)
{
    if (results.size() != prepared.rows.size()) { return; }
    if (std::none_of(results.begin(), results.end(), [](const auto& row) { return row.saved && !row.published; })) { return; }
    String refresh_error;
    const bool refreshed = kernel::resman().refresh_module_resources(refresh_error);
    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].saved || results[i].published) { continue; }
        if (!refreshed) {
            results[i].error = "Blueprint saved; resource refresh failed: " + refresh_error;
            continue;
        }
        try {
            std::string current;
            if (!read_file(prepared.rows[i].target, current, results[i].error)) { continue; }
            if (current != prepared.rows[i].bytes) {
                results[i].error = "Blueprint saved, but the file changed before publication";
                continue;
            }
            bool dirty = false;
            for (const auto& tab : workspace.tabs()) {
                if (!tab.detail.empty() && fs::weakly_canonical(prepared.project / tab.detail) == prepared.rows[i].target && tab.dirty) {
                    dirty = true;
                    break;
                }
            }
            if (dirty) {
                results[i].error = "Blueprint saved, but its destination tab has subsequent edits";
                continue;
            }
            results[i].error.clear();
            if (prepared.rows[i].request.kind == BlueprintWriteKind::save_as) {
                results[i].published = true;
                continue;
            }
            if (prepared.rows[i].request.kind == BlueprintWriteKind::create) {
                const auto relative = results[i].relative_path.generic_string();
                auto& tab = workspace.open_or_replace_tab("preview:" + relative,
                    live_object_display_name(prepared.rows[i].document.object()), WorkspaceTabKind::preview, relative);
                tab.undo_stack.clear();
                tab.redo_stack.clear();
                tab.document = std::move(prepared.rows[i].document);
                tab.dirty = false;
                results[i].published = true;
                continue;
            }
            for (const auto& tab : workspace.tabs()) {
                if (tab.detail != results[i].relative_path.generic_string()) { continue; }
                auto* destination = workspace.find_tab(tab.id);
                destination->undo_stack.clear();
                destination->redo_stack.clear();
                destination->document = std::move(prepared.rows[i].document);
                destination->dirty = false;
                break;
            }
            results[i].published = true;
        } catch (const std::exception& ex) {
            results[i].error = "Blueprint saved; document publication failed: " + std::string{ex.what()};
        }
    }
}

} // namespace nw::toolset
