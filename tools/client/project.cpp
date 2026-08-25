#include "project.hpp"

#include "area_map.hpp"

#include <nw/formats/Dialog.hpp>
#include <nw/formats/Faction.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/profiles/nwn1/gff_propset_component_json.hpp>
#include <nw/profiles/nwn1/propset_gff_policy.hpp>
#include <nw/resources/Container.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/StaticErf.hpp>
#include <nw/resources/assets.hpp>
#include <nw/serialization/Gff.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace nw::toolset {

namespace {

constexpr std::string_view kManifestName = "rollnw.json";
constexpr std::string_view kProjectFormat = "rollnw.module";
constexpr std::string_view kSharedRoot = "shared";
constexpr std::string_view kJsonModulePath = "shared/module.ifo.json";
constexpr std::string_view kLegacyModulePath = "shared/module.ifo";
constexpr std::string_view kProjectTreeLabelCachePath = ".rollnw/cache/project_tree_labels.json";
constexpr int kProjectVersion = 1;

const std::array<std::string_view, 18> kProjectDirectories{
    "shared",
    "shared/areas",
    "shared/blueprints",
    "shared/blueprints/creatures",
    "shared/blueprints/doors",
    "shared/blueprints/encounters",
    "shared/blueprints/items",
    "shared/blueprints/placeables",
    "shared/blueprints/sounds",
    "shared/blueprints/stores",
    "shared/blueprints/triggers",
    "shared/blueprints/waypoints",
    "shared/conversations",
    "shared/factions",
    "shared/journals",
    "shared/palettes",
    "shared/resources",
    "shared/scripts",
};

ProjectResult failure(std::string message)
{
    ProjectResult result;
    result.message = std::move(message);
    return result;
}

ProjectResult success(std::string message)
{
    ProjectResult result;
    result.ok = true;
    result.message = std::move(message);
    return result;
}

fs::path manifest_path(const fs::path& project_dir)
{
    return project_dir / kManifestName;
}

nlohmann::json load_manifest_json(const fs::path& project_dir)
{
    std::ifstream input{manifest_path(project_dir)};
    if (!input) {
        return {};
    }

    try {
        nlohmann::json manifest;
        input >> manifest;
        return manifest;
    } catch (const std::exception&) {
        return {};
    }
}

bool load_valid_manifest(const fs::path& project_dir)
{
    const nlohmann::json manifest = load_manifest_json(project_dir);
    return manifest.is_object()
        && manifest.value("format", "") == kProjectFormat
        && manifest.value("version", 0) == kProjectVersion;
}

nlohmann::json default_manifest(std::string module_name, std::string_view module_path)
{
    nlohmann::json manifest;
    manifest["format"] = kProjectFormat;
    manifest["version"] = kProjectVersion;
    if (!module_name.empty()) {
        manifest["name"] = std::move(module_name);
    }
    manifest["module"] = module_path;
    manifest["roots"] = {
        {"shared", "shared"},
        {"areas", "shared/areas"},
        {"blueprints", "shared/blueprints"},
        {"conversations", "shared/conversations"},
        {"resources", "shared/resources"},
        {"scripts", "shared/scripts"},
    };
    return manifest;
}

bool ensure_directory(const fs::path& dir, std::string& error)
{
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_directory(dir, ec)) {
        error = "Path is not a directory: " + dir.string();
        return false;
    }
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Failed to create directory " + dir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool ensure_project_directories(const fs::path& project_dir, std::string& error)
{
    if (!ensure_directory(project_dir, error)) {
        return false;
    }
    for (const std::string_view relative : kProjectDirectories) {
        if (!ensure_directory(project_dir / relative, error)) {
            return false;
        }
    }
    return ensure_directory(project_dir / ".rollnw" / "cache", error);
}

bool write_manifest(const fs::path& project_dir, const nlohmann::json& manifest, std::string& error)
{
    std::ofstream output{manifest_path(project_dir)};
    if (!output) {
        error = "Failed to write " + manifest_path(project_dir).string();
        return false;
    }
    output << manifest.dump(2) << '\n';
    return true;
}

std::optional<fs::path> valid_project_creature_path(
    const fs::path& project_dir,
    const fs::path& relative_path,
    std::string& error)
{
    if (relative_path.empty() || relative_path.is_absolute()) {
        error = "Preview test actor path must be project-relative";
        return std::nullopt;
    }

    const fs::path normalized = relative_path.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..") {
            error = "Preview test actor path escapes the project";
            return std::nullopt;
        }
    }

    const nw::Resource resource = nw::Resource::from_path(normalized, false);
    if (!resource.valid() || resource.type != nw::ResourceType::utc) {
        error = "Preview test actor must name a creature blueprint";
        return std::nullopt;
    }

    std::error_code ec;
    if (!fs::is_regular_file(project_dir / normalized, ec)) {
        error = "Preview test actor does not exist: " + normalized.generic_string();
        return std::nullopt;
    }
    return normalized;
}

std::string resource_extension(nw::ResourceType::type type)
{
    if (const auto ext = nw::ResourceType::to_string(type); !ext.empty()) {
        return std::string{ext};
    }
    return "misc";
}

bool can_write_json_resource(const nw::Resource& resource)
{
    switch (resource.type) {
    case nw::ResourceType::ifo:
        return resource.resref.view() == "module";
    case nw::ResourceType::utc:
    case nw::ResourceType::utd:
    case nw::ResourceType::ute:
    case nw::ResourceType::uti:
    case nw::ResourceType::utp:
    case nw::ResourceType::uts:
    case nw::ResourceType::utm:
    case nw::ResourceType::utt:
    case nw::ResourceType::utw:
    case nw::ResourceType::dlg:
    case nw::ResourceType::fac:
        return true;
    default:
        return false;
    }
}

bool writes_json_resource(const nw::Resource& resource, const ProjectImportOptions& options)
{
    return options.format == ProjectImportFormat::json && can_write_json_resource(resource);
}

bool is_area_resource(nw::ResourceType::type type)
{
    switch (type) {
    case nw::ResourceType::caf:
    case nw::ResourceType::are:
    case nw::ResourceType::git:
    case nw::ResourceType::gic:
        return true;
    default:
        return false;
    }
}

std::string to_lower_ascii(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool contains_casefold(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }
    const std::string hay = to_lower_ascii(haystack);
    const std::string ndl = to_lower_ascii(needle);
    return hay.find(ndl) != std::string::npos;
}

bool is_blank_ascii(std::string_view value)
{
    for (const unsigned char ch : value) {
        if (!std::isspace(ch)) {
            return false;
        }
    }
    return true;
}

std::string sanitize_project_label(std::string_view text)
{
    std::string out;
    out.reserve(text.size());

    for (const unsigned char ch : text) {
        if (ch == 0 || ch < 0x20 || ch == 0x7f) {
            continue;
        }
        out.push_back(static_cast<char>(ch));
    }

    size_t first = 0;
    while (first < out.size() && std::isspace(static_cast<unsigned char>(out[first]))) {
        ++first;
    }
    size_t last = out.size();
    while (last > first && std::isspace(static_cast<unsigned char>(out[last - 1]))) {
        --last;
    }

    return out.substr(first, last - first);
}

fs::path resource_filename(const nw::Resource& resource, const ProjectImportOptions& options)
{
    if (writes_json_resource(resource, options)) {
        return fs::path{resource.filename() + ".json"};
    }
    return fs::path{resource.filename()};
}

fs::path blueprint_directory(nw::ResourceType::type type)
{
    switch (type) {
    case nw::ResourceType::utc:
        return "blueprints/creatures";
    case nw::ResourceType::utd:
        return "blueprints/doors";
    case nw::ResourceType::ute:
        return "blueprints/encounters";
    case nw::ResourceType::uti:
        return "blueprints/items";
    case nw::ResourceType::utp:
        return "blueprints/placeables";
    case nw::ResourceType::uts:
        return "blueprints/sounds";
    case nw::ResourceType::utm:
        return "blueprints/stores";
    case nw::ResourceType::utt:
        return "blueprints/triggers";
    case nw::ResourceType::utw:
        return "blueprints/waypoints";
    default:
        return {};
    }
}

fs::path project_relative_path(const nw::Resource& resource, const ProjectImportOptions& options)
{
    const fs::path filename = resource_filename(resource, options);
    if (resource.type == nw::ResourceType::ifo && resource.resref.view() == "module") {
        return fs::path{kSharedRoot} / filename;
    }
    if (is_area_resource(resource.type)) {
        return fs::path{kSharedRoot} / "areas" / filename;
    }
    if (auto blueprint_dir = blueprint_directory(resource.type); !blueprint_dir.empty()) {
        return fs::path{kSharedRoot} / blueprint_dir / filename;
    }
    if (resource.type == nw::ResourceType::nss || resource.type == nw::ResourceType::smalls) {
        return fs::path{kSharedRoot} / "scripts" / filename;
    }
    if (resource.type == nw::ResourceType::dlg) {
        return fs::path{kSharedRoot} / "conversations" / filename;
    }
    if (resource.type == nw::ResourceType::itp) {
        return fs::path{kSharedRoot} / "palettes" / filename;
    }
    if (resource.type == nw::ResourceType::jrl) {
        return fs::path{kSharedRoot} / "journals" / filename;
    }
    if (resource.type == nw::ResourceType::fac) {
        return fs::path{kSharedRoot} / "factions" / filename;
    }
    return fs::path{kSharedRoot} / "resources" / resource_extension(resource.type) / filename;
}

bool write_json(const fs::path& target, const nlohmann::json& json, std::string& error)
{
    std::ofstream output{target};
    if (!output) {
        error = "Failed to write resource " + target.string();
        return false;
    }
    output << json.dump(2) << '\n';
    return true;
}

bool write_legacy_resource(const fs::path& target, const nw::ResourceData& data, std::string& error)
{
    if (!data.bytes.write_to(target)) {
        error = "Failed to write resource " + target.string();
        return false;
    }
    return true;
}

bool write_module_json_resource(const fs::path& target, nw::ResourceData data, std::string& error)
{
    const auto resource_name = data.name.filename();
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        error = "Failed to read GFF resource " + resource_name;
        if (!gff.error().empty()) {
            error += ": " + gff.error();
        }
        return false;
    }

    nw::Module module;
    if (!nw::deserialize(&module, gff.toplevel())) {
        error = "Failed to convert module resource " + resource_name;
        return false;
    }

    nlohmann::json json;
    if (!nw::Module::serialize(&module, json)) {
        error = "Failed to serialize module resource " + resource_name;
        return false;
    }
    return write_json(target, json, error);
}

bool write_blueprint_json_resource(const fs::path& target, nw::ResourceData data, std::string& error)
{
    const auto resource_name = data.name.filename();
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        error = "Failed to read GFF resource " + resource_name;
        if (!gff.error().empty()) {
            error += ": " + gff.error();
        }
        return false;
    }

    nlohmann::json json;
    const auto result = nwn1::gff_to_propset_component_json(gff,
        json,
        &nw::kernel::runtime(),
        &nwn1::propset_gff_policy_registry(),
        nw::SerializationProfile::blueprint);
    if (!result) {
        error = "Failed to convert blueprint resource " + resource_name + ": " + result.error;
        return false;
    }
    return write_json(target, json, error);
}

bool write_dialog_json_resource(const fs::path& target, nw::ResourceData data, std::string& error)
{
    const auto resource_name = data.name.filename();
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        error = "Failed to read GFF resource " + resource_name;
        if (!gff.error().empty()) {
            error += ": " + gff.error();
        }
        return false;
    }

    nw::Dialog dialog{gff.toplevel()};
    if (!dialog.valid()) {
        error = "Failed to convert dialog resource " + resource_name;
        return false;
    }

    nlohmann::json json;
    static_cast<void (*)(nlohmann::json&, const nw::Dialog&)>(&nw::serialize)(json, dialog);
    return write_json(target, json, error);
}

bool write_faction_json_resource(const fs::path& target, nw::ResourceData data, std::string& error)
{
    const auto resource_name = data.name.filename();
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        error = "Failed to read GFF resource " + resource_name;
        if (!gff.error().empty()) {
            error += ": " + gff.error();
        }
        return false;
    }

    nw::Faction faction{gff};
    return write_json(target, faction.to_json(), error);
}

bool write_json_resource(const fs::path& target, nw::ResourceData data, std::string& error)
{
    switch (data.name.type) {
    case nw::ResourceType::ifo:
        return write_module_json_resource(target, std::move(data), error);
    case nw::ResourceType::utc:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::utd:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::ute:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::uti:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::utp:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::uts:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::utm:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::utt:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::utw:
        return write_blueprint_json_resource(target, std::move(data), error);
    case nw::ResourceType::dlg:
        return write_dialog_json_resource(target, std::move(data), error);
    case nw::ResourceType::fac:
        return write_faction_json_resource(target, std::move(data), error);
    default:
        error = "No JSON writer for resource " + data.name.filename();
        return false;
    }
}

bool write_resource(const fs::path& project_dir,
    nw::ResourceData data,
    const ProjectImportOptions& options,
    std::string& error)
{
    const fs::path target = project_dir / project_relative_path(data.name, options);
    if (!ensure_directory(target.parent_path(), error)) {
        return false;
    }

    if (writes_json_resource(data.name, options)) {
        return write_json_resource(target, std::move(data), error);
    }
    return write_legacy_resource(target, data, error);
}

struct LegacyAreaResourceGroup {
    nw::Resref resref;
    bool has_are = false;
    bool has_git = false;
};

bool write_json_areas(const fs::path& project_dir,
    std::vector<nw::Resource> resources,
    ProjectResult& result,
    std::string& error)
{
    std::sort(resources.begin(), resources.end(), [](const nw::Resource& lhs, const nw::Resource& rhs) {
        if (lhs.resref != rhs.resref) {
            return lhs.resref.view() < rhs.resref.view();
        }
        return lhs.type < rhs.type;
    });

    std::vector<LegacyAreaResourceGroup> groups;
    groups.reserve(resources.size() / 2);
    for (const auto& resource : resources) {
        if (groups.empty() || groups.back().resref != resource.resref) {
            groups.push_back({.resref = resource.resref});
        }

        auto& group = groups.back();
        switch (resource.type) {
        case nw::ResourceType::are:
            group.has_are = true;
            break;
        case nw::ResourceType::git:
            group.has_git = true;
            break;
        case nw::ResourceType::gic:
            break;
        default:
            error = "Invalid legacy area resource " + resource.filename();
            return false;
        }
    }

    const fs::path area_dir = project_dir / kSharedRoot / "areas";
    std::vector<AreaMapSource> map_sources;
    map_sources.reserve(groups.size());
    for (const auto& group : groups) {
        if (!group.has_are || !group.has_git) {
            error = "Legacy area '" + std::string{group.resref.view()} + "' is missing "
                + (!group.has_are ? "ARE" : "GIT");
            return false;
        }

        auto* area = nw::kernel::objects().make_area(group.resref);
        if (!area) {
            error = "Failed to instantiate legacy area '" + std::string{group.resref.view()} + "' for JSON import";
            return false;
        }

        nlohmann::json json;
        try {
            nw::serialize(area, json);
            const std::array<const nw::Area*, 1> area_batch{area};
            auto source = collect_area_map_sources(area_batch);
            if (!source.empty()) {
                map_sources.push_back(std::move(source.front()));
            }
        } catch (const std::exception& e) {
            area->clear();
            nw::kernel::objects().destroy(area->handle());
            error = "Failed to serialize area '" + std::string{group.resref.view()} + "': " + e.what();
            return false;
        }
        area->clear();
        nw::kernel::objects().destroy(area->handle());

        const fs::path target = area_dir / (std::string{group.resref.view()} + ".caf.json");
        if (!write_json(target, json, error)) {
            return false;
        }
    }

    const AreaMapWriteResult map_result = write_project_area_maps(project_dir, map_sources);
    result.area_map_count = map_result.written;
    result.area_map_degraded_count = map_result.degraded;
    result.area_map_failure_count = map_result.failed;
    if (map_result.degraded > 0) {
        LOG_F(WARNING, "Area map generation marked {} map(s) incomplete: {}",
            map_result.degraded, map_result.first_warning);
    }
    if (map_result.failed > 0) {
        LOG_F(WARNING, "Area map generation dropped {} map(s): {}",
            map_result.failed, map_result.first_error);
    }

    for (const auto& resource : resources) {
        std::error_code ec;
        const fs::path legacy_target = area_dir / resource.filename();
        fs::remove(legacy_target, ec);
        if (ec) {
            error = "Failed to remove legacy area resource " + legacy_target.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

ProjectResult initialize_project_with_module_path(const fs::path& project_dir,
    std::string module_name,
    std::string_view module_path)
{
    std::string error;
    if (!ensure_project_directories(project_dir, error)) {
        return failure(std::move(error));
    }

    const fs::path manifest = manifest_path(project_dir);
    std::error_code ec;
    if (fs::exists(manifest, ec)) {
        if (!load_valid_manifest(project_dir)) {
            return failure("Existing " + manifest.filename().string() + " is not a valid rollnw client project manifest");
        }

        auto result = success("rollnw client project already initialized: " + project_dir.string());
        result.initialized = false;
        return result;
    }

    if (!write_manifest(project_dir, default_manifest(std::move(module_name), module_path), error)) {
        return failure(std::move(error));
    }

    auto result = success("Initialized rollnw client project: " + project_dir.string());
    result.initialized = true;
    return result;
}

void ensure_resource_name_services()
{
    (void)nw::kernel::services().add<nw::kernel::Strings>();
}

bool is_hidden_project_directory(const fs::path& relative_path)
{
    if (relative_path.empty()) {
        return false;
    }
    const fs::path& root = *relative_path.begin();
    return root == ".rollnw";
}

fs::path project_tree_label_cache_path(const fs::path& project_dir)
{
    return project_dir / kProjectTreeLabelCachePath;
}

struct ProjectLabelCacheEntry {
    std::string label;
    uintmax_t size = 0;
    std::int64_t modified = 0;
};

struct ProjectLabelCache {
    std::unordered_map<std::string, ProjectLabelCacheEntry> entries;
    std::unordered_set<std::string> touched;
    bool dirty = false;
};

uintmax_t file_size_for_cache(const fs::path& path)
{
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    return ec ? 0 : size;
}

std::int64_t file_modified_for_cache(const fs::path& path)
{
    std::error_code ec;
    const auto modified = fs::last_write_time(path, ec);
    if (ec) {
        return 0;
    }

    using TickRep = fs::file_time_type::duration::rep;
    const TickRep ticks = modified.time_since_epoch().count();
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (ticks < static_cast<TickRep>(minimum)) {
        return minimum;
    }
    if (ticks > static_cast<TickRep>(maximum)) {
        return maximum;
    }
    return static_cast<std::int64_t>(ticks);
}

ProjectLabelCache load_project_label_cache(const fs::path& project_dir)
{
    ProjectLabelCache cache;
    std::ifstream input{project_tree_label_cache_path(project_dir)};
    if (!input) {
        return cache;
    }

    try {
        nlohmann::json json;
        input >> json;
        if (!json.is_object() || json.value("version", 0) != 1 || !json.contains("entries")) {
            return cache;
        }

        const auto& entries = json.at("entries");
        if (!entries.is_object()) {
            return cache;
        }

        for (const auto& [key, value] : entries.items()) {
            if (!value.is_object()) {
                continue;
            }

            ProjectLabelCacheEntry entry;
            entry.label = sanitize_project_label(value.value("label", std::string{}));
            entry.size = value.value("size", uintmax_t{0});
            entry.modified = value.value("modified", std::int64_t{0});
            cache.entries.emplace(key, std::move(entry));
        }
    } catch (const std::exception&) {
        cache.entries.clear();
    }
    return cache;
}

void prune_project_label_cache(ProjectLabelCache& cache)
{
    for (auto it = cache.entries.begin(); it != cache.entries.end();) {
        if (cache.touched.find(it->first) == cache.touched.end()) {
            it = cache.entries.erase(it);
            cache.dirty = true;
        } else {
            ++it;
        }
    }
}

void write_project_label_cache(const fs::path& project_dir, const ProjectLabelCache& cache)
{
    if (!cache.dirty) {
        return;
    }

    const fs::path cache_path = project_tree_label_cache_path(project_dir);
    std::error_code ec;
    fs::create_directories(cache_path.parent_path(), ec);
    if (ec) {
        return;
    }

    nlohmann::json entries = nlohmann::json::object();
    for (const auto& [key, entry] : cache.entries) {
        entries[key] = {
            {"label", entry.label},
            {"size", entry.size},
            {"modified", entry.modified},
        };
    }

    nlohmann::json json;
    json["version"] = 1;
    json["entries"] = std::move(entries);

    std::ofstream output{cache_path};
    if (output) {
        output << json.dump(2) << '\n';
    }
}

bool can_label_project_resource(nw::ResourceType::type type)
{
    switch (type) {
    case nw::ResourceType::caf:
    case nw::ResourceType::are:
    case nw::ResourceType::utc:
    case nw::ResourceType::utd:
    case nw::ResourceType::ute:
    case nw::ResourceType::uti:
    case nw::ResourceType::utp:
    case nw::ResourceType::uts:
    case nw::ResourceType::utm:
    case nw::ResourceType::utt:
    case nw::ResourceType::utw:
        return true;
    default:
        return false;
    }
}

std::string read_project_resource_label(const fs::path& path, nw::ResourceType::type type)
{
    switch (type) {
    case nw::ResourceType::caf:
    case nw::ResourceType::are:
        return sanitize_project_label(nw::Area::get_name_from_file(path));
    case nw::ResourceType::utc:
        return sanitize_project_label(nw::Creature::get_name_from_file(path));
    case nw::ResourceType::utd:
        return sanitize_project_label(nw::Door::get_name_from_file(path));
    case nw::ResourceType::ute:
        return sanitize_project_label(nw::Encounter::get_name_from_file(path));
    case nw::ResourceType::uti:
        return sanitize_project_label(nw::Item::get_name_from_file(path));
    case nw::ResourceType::utp:
        return sanitize_project_label(nw::Placeable::get_name_from_file(path));
    case nw::ResourceType::uts:
        return sanitize_project_label(nw::Sound::get_name_from_file(path));
    case nw::ResourceType::utm:
        return sanitize_project_label(nw::Store::get_name_from_file(path));
    case nw::ResourceType::utt:
        return sanitize_project_label(nw::Trigger::get_name_from_file(path));
    case nw::ResourceType::utw:
        return sanitize_project_label(nw::Waypoint::get_name_from_file(path));
    default:
        return {};
    }
}

std::string cached_project_resource_label(const fs::path& path,
    const fs::path& relative_path,
    nw::ResourceType::type type,
    ProjectLabelCache& cache)
{
    if (!can_label_project_resource(type)) {
        return {};
    }

    const std::string key = relative_path.generic_string();
    cache.touched.insert(key);

    const uintmax_t size = file_size_for_cache(path);
    const std::int64_t modified = file_modified_for_cache(path);

    if (const auto it = cache.entries.find(key); it != cache.entries.end()
        && it->second.size == size
        && it->second.modified == modified) {
        return it->second.label;
    }

    ProjectLabelCacheEntry entry;
    entry.label = read_project_resource_label(path, type);
    entry.size = size;
    entry.modified = modified;
    cache.entries[key] = entry;
    cache.dirty = true;
    return entry.label;
}

int area_resource_rank(nw::ResourceType::type type)
{
    switch (type) {
    case nw::ResourceType::caf:
        return 0;
    case nw::ResourceType::are:
        return 1;
    case nw::ResourceType::git:
        return 2;
    case nw::ResourceType::gic:
        return 3;
    default:
        return 100;
    }
}

struct AreaResourceInfo {
    std::string key;
    std::string label;
    nw::ResourceType::type type = nw::ResourceType::invalid;
};

fs::path project_resource_stem_path(fs::path relative_path)
{
    const auto type = nw::ResourceType::from_extension(relative_path.extension().string());
    relative_path.replace_extension();
    if (type == nw::ResourceType::json) {
        const auto authored_type = nw::ResourceType::from_extension(relative_path.extension().string());
        if (authored_type != nw::ResourceType::invalid) {
            relative_path.replace_extension();
        }
    }
    return relative_path;
}

std::optional<AreaResourceInfo> area_resource_info(const fs::path& relative_path)
{
    const nw::Resource resource = nw::Resource::from_path(relative_path, false);
    if (!resource.valid() || !is_area_resource(resource.type)) {
        return std::nullopt;
    }

    const fs::path key_path = project_resource_stem_path(relative_path);
    AreaResourceInfo info;
    info.key = key_path.generic_string();
    info.label = key_path.filename().string();
    info.type = resource.type;
    return info;
}

struct AreaGroup {
    std::string key;
    std::string label;
    fs::path primary_path;
    fs::path primary_relative_path;
    std::vector<fs::path> sidecar_relative_paths;
    nw::ResourceType::type primary_type = nw::ResourceType::invalid;
    int primary_rank = 100;
};

void add_area_group_entry(std::unordered_map<std::string, AreaGroup>& groups,
    const AreaResourceInfo& info,
    const fs::directory_entry& entry,
    const fs::path& relative_path)
{
    AreaGroup& group = groups[info.key];
    if (group.key.empty()) {
        group.key = info.key;
        group.label = info.label;
    }

    group.sidecar_relative_paths.push_back(relative_path);
    const int rank = area_resource_rank(info.type);
    if (rank < group.primary_rank) {
        group.primary_rank = rank;
        group.primary_path = entry.path();
        group.primary_relative_path = relative_path;
        group.primary_type = info.type;
    }
}

ProjectTreeNode make_area_project_tree_node(const AreaGroup& group, ProjectLabelCache& cache)
{
    ProjectTreeNode node;
    node.id = "area:" + group.key;
    node.label = group.label;
    node.path = group.primary_path;
    node.relative_path = group.primary_relative_path;
    node.resource_type = "area";
    node.kind = ProjectTreeNodeKind::area;
    if (const auto label = cached_project_resource_label(group.primary_path, group.primary_relative_path, group.primary_type, cache);
        !is_blank_ascii(label)) {
        node.label = label;
        node.detail = group.primary_relative_path.filename().string();
    }
    return node;
}

bool is_script_source_resource(nw::ResourceType::type type)
{
    return type == nw::ResourceType::nss || type == nw::ResourceType::smalls;
}

bool path_contains_component(const fs::path& path, std::string_view component)
{
    for (const auto& part : path) {
        if (part.generic_string() == component) {
            return true;
        }
    }
    return false;
}

bool should_hide_project_tree_file(const fs::path& relative_path)
{
    if (!path_contains_component(relative_path.parent_path(), "scripts")) {
        return false;
    }

    const nw::Resource resource = nw::Resource::from_path(relative_path, false);
    return !resource.valid() || !is_script_source_resource(resource.type);
}

nw::Resource resource_from_project_path(const fs::path& relative_path, std::string& resource_type)
{
    const nw::Resource resource = nw::Resource::from_path(relative_path, false);
    if (resource.valid()) {
        resource_type = std::string(nw::ResourceType::to_string(resource.type));
        return resource;
    }
    resource_type.clear();
    return {};
}

bool project_node_matches(const ProjectTreeNode& node, std::string_view query)
{
    return query.empty()
        || contains_casefold(node.label, query)
        || contains_casefold(node.detail, query)
        || contains_casefold(node.relative_path.generic_string(), query)
        || contains_casefold(node.resource_type, query);
}

bool project_area_group_matches(const ProjectTreeNode& node, const AreaGroup& group, std::string_view query)
{
    if (project_node_matches(node, query)) {
        return true;
    }

    for (const auto& sidecar : group.sidecar_relative_paths) {
        if (contains_casefold(sidecar.generic_string(), query)) {
            return true;
        }
    }
    return false;
}

ProjectTreeNode make_project_tree_node(const fs::path& project_dir,
    const fs::path& relative_path,
    const fs::directory_entry& entry,
    ProjectLabelCache& cache)
{
    ProjectTreeNode node;
    node.id = relative_path.generic_string();
    node.label = entry.path().filename().string();
    node.path = entry.path();
    node.relative_path = relative_path;

    std::error_code ec;
    if (entry.is_directory(ec)) {
        node.kind = ProjectTreeNodeKind::directory;
        node.resource_type = "folder";
    } else {
        const nw::Resource resource = resource_from_project_path(relative_path, node.resource_type);
        node.kind = resource.valid() ? ProjectTreeNodeKind::resource : ProjectTreeNodeKind::file;
        if (resource.valid()) {
            if (const auto label = cached_project_resource_label(entry.path(), relative_path, resource.type, cache);
                !is_blank_ascii(label) && label != node.label) {
                node.label = label;
                node.detail = entry.path().filename().string();
            }
        }
    }

    if (node.label.empty()) {
        node.label = project_dir.filename().string();
    }
    return node;
}

void sort_project_tree_children(std::vector<ProjectTreeNode>& children)
{
    std::sort(children.begin(), children.end(), [](const ProjectTreeNode& lhs, const ProjectTreeNode& rhs) {
        const bool lhs_dir = lhs.is_directory();
        const bool rhs_dir = rhs.is_directory();
        if (lhs_dir != rhs_dir) {
            return lhs_dir;
        }

        const auto lhs_label = to_lower_ascii(lhs.label);
        const auto rhs_label = to_lower_ascii(rhs.label);
        if (lhs_label != rhs_label) {
            return lhs_label < rhs_label;
        }

        return to_lower_ascii(lhs.relative_path.generic_string()) < to_lower_ascii(rhs.relative_path.generic_string());
    });
}

bool scan_project_tree_directory(const fs::path& project_dir,
    const fs::path& directory,
    const fs::path& relative_directory,
    std::string_view query,
    ProjectLabelCache& cache,
    ProjectTreeNode& parent,
    size_t& node_count,
    std::string& error)
{
    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            error = "Failed to read project directory " + directory.string() + ": " + ec.message();
            return false;
        }

        const fs::path relative_path = relative_directory / entry.path().filename();
        std::error_code entry_ec;
        if (entry.is_directory(entry_ec) && is_hidden_project_directory(relative_path)) {
            continue;
        }
        if (entry.is_regular_file(entry_ec) && should_hide_project_tree_file(relative_path)) {
            continue;
        }
        entries.push_back(entry);
    }

    if (ec) {
        error = "Failed to read project directory " + directory.string() + ": " + ec.message();
        return false;
    }

    std::unordered_map<std::string, AreaGroup> area_groups;
    for (const auto& entry : entries) {
        if (entry.is_directory(ec)) {
            continue;
        }

        const fs::path relative_path = relative_directory / entry.path().filename();
        if (const auto area_info = area_resource_info(relative_path)) {
            add_area_group_entry(area_groups, *area_info, entry, relative_path);
        }
    }

    std::unordered_set<std::string> emitted_area_groups;
    for (const auto& entry : entries) {
        const fs::path relative_path = relative_directory / entry.path().filename();
        if (const auto area_info = area_resource_info(relative_path)) {
            const auto group_it = area_groups.find(area_info->key);
            if (group_it == area_groups.end() || !emitted_area_groups.insert(area_info->key).second) {
                continue;
            }

            ProjectTreeNode node = make_area_project_tree_node(group_it->second, cache);
            if (!query.empty() && !project_area_group_matches(node, group_it->second, query)) {
                continue;
            }

            parent.children.push_back(std::move(node));
            ++node_count;
            continue;
        }

        ProjectTreeNode node = make_project_tree_node(project_dir, relative_path, entry, cache);

        if (node.is_directory()) {
            if (!scan_project_tree_directory(project_dir, entry.path(), relative_path, query, cache, node, node_count, error)) {
                return false;
            }
        }

        if (!query.empty() && !project_node_matches(node, query) && node.children.empty()) {
            continue;
        }

        parent.children.push_back(std::move(node));
        ++node_count;
    }

    sort_project_tree_children(parent.children);
    return true;
}

ProjectModuleSummary module_summary_failure(std::string message)
{
    ProjectModuleSummary result;
    result.message = std::move(message);
    return result;
}

std::vector<std::string> copy_module_haks(const nw::Vector<nw::String>& haks)
{
    std::vector<std::string> result;
    result.reserve(haks.size());
    for (const auto& hak : haks) {
        const std::string label = sanitize_project_label(hak);
        if (!label.empty()) {
            result.push_back(label);
        }
    }
    return result;
}

ProjectModuleSummary load_json_module_summary(const fs::path& module_path)
{
    std::ifstream input{module_path};
    if (!input) {
        return module_summary_failure("Failed to read module metadata: " + module_path.string());
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (const std::exception& e) {
        return module_summary_failure("Failed to parse module metadata " + module_path.string() + ": " + e.what());
    }

    if (!json.is_object()) {
        return module_summary_failure("Module metadata is not an object: " + module_path.string());
    }

    ProjectModuleSummary result;
    result.ok = true;
    result.message = "Loaded module metadata: " + module_path.string();

    const auto haks = json.find("haks");
    if (haks == json.end() || !haks->is_array()) {
        return result;
    }

    result.haks.reserve(haks->size());
    for (const auto& hak : *haks) {
        if (!hak.is_string()) {
            continue;
        }
        const std::string label = sanitize_project_label(hak.get<std::string>());
        if (!label.empty()) {
            result.haks.push_back(label);
        }
    }
    return result;
}

ProjectModuleSummary load_legacy_module_summary(const fs::path& module_path)
{
    nw::Gff gff{module_path};
    if (!gff.valid()) {
        std::string message = "Failed to read module metadata: " + module_path.string();
        if (!gff.error().empty()) {
            message += ": " + gff.error();
        }
        return module_summary_failure(std::move(message));
    }

    nw::Module module;
    if (!nw::deserialize(&module, gff.toplevel())) {
        return module_summary_failure("Failed to convert module metadata: " + module_path.string());
    }

    ProjectModuleSummary result;
    result.ok = true;
    result.haks = copy_module_haks(module.haks);
    result.message = "Loaded module metadata: " + module_path.string();
    return result;
}

} // namespace

bool is_project_directory(const fs::path& path)
{
    return load_valid_manifest(path);
}

std::string project_display_name(const fs::path& project_dir)
{
    const nlohmann::json manifest = load_manifest_json(project_dir);
    if (manifest.is_object()) {
        if (const auto name = manifest.value("name", std::string{}); !name.empty()) {
            return name;
        }
    }

    if (const auto filename = project_dir.filename().string(); !filename.empty()) {
        return filename;
    }
    return project_dir.string();
}

bool project_resource_is_area(const fs::path& relative_path)
{
    return area_resource_info(relative_path).has_value();
}

bool project_resource_is_dialog(const fs::path& relative_path)
{
    const nw::Resource resource = nw::Resource::from_path(relative_path, false);
    return resource.valid() && resource.type == nw::ResourceType::dlg;
}

bool project_resource_is_preview_blueprint(const fs::path& relative_path)
{
    const nw::Resource resource = nw::Resource::from_path(relative_path, false);
    return resource.valid()
        && (resource.type == nw::ResourceType::utc
            || resource.type == nw::ResourceType::uti
            || resource.type == nw::ResourceType::utd
            || resource.type == nw::ResourceType::utp
            || resource.type == nw::ResourceType::ute
            || resource.type == nw::ResourceType::uts
            || resource.type == nw::ResourceType::utm
            || resource.type == nw::ResourceType::utt
            || resource.type == nw::ResourceType::utw);
}

std::string project_resource_display_name(const fs::path& project_dir, const fs::path& resource_relative_path)
{
    ensure_resource_name_services();

    std::error_code ec;
    fs::path relative_path = resource_relative_path;
    if (relative_path.is_absolute()) {
        relative_path = fs::relative(relative_path, project_dir, ec);
        if (ec) {
            relative_path = resource_relative_path.filename();
        }
    }

    const fs::path resource_path = project_dir / relative_path;
    const nw::Resource resource = nw::Resource::from_path(relative_path, false);
    if (resource.valid()) {
        ProjectLabelCache label_cache = load_project_label_cache(project_dir);
        if (const auto label = cached_project_resource_label(resource_path, relative_path, resource.type, label_cache);
            !is_blank_ascii(label)) {
            write_project_label_cache(project_dir, label_cache);
            return label;
        }
        write_project_label_cache(project_dir, label_cache);
    }

    fs::path label_path = resource.valid() ? project_resource_stem_path(relative_path) : relative_path;
    if (!resource.valid()) {
        label_path.replace_extension();
    }

    std::string label = sanitize_project_label(label_path.filename().generic_string());
    if (label.empty()) {
        label = sanitize_project_label(relative_path.filename().generic_string());
    }
    if (label.empty()) {
        label = sanitize_project_label(relative_path.generic_string());
    }
    return label;
}

ProjectTreeResult load_project_tree(const fs::path& project_dir, std::string_view query)
{
    ProjectTreeResult result;
    if (!is_project_directory(project_dir)) {
        result.message = "Not a rollnw client project: " + project_dir.string();
        return result;
    }

    ensure_resource_name_services();

    result.root.id = ".";
    result.root.label = project_display_name(project_dir);
    result.root.path = project_dir;
    result.root.kind = ProjectTreeNodeKind::directory;
    result.root.resource_type = "project";

    ProjectLabelCache label_cache = load_project_label_cache(project_dir);

    std::string error;
    if (!scan_project_tree_directory(project_dir, project_dir, {}, query, label_cache, result.root, result.node_count, error)) {
        result.message = std::move(error);
        return result;
    }

    prune_project_label_cache(label_cache);
    write_project_label_cache(project_dir, label_cache);

    result.ok = true;
    result.message = "Loaded project tree: " + project_dir.string();
    return result;
}

ProjectModuleSummary load_project_module_summary(const fs::path& project_dir)
{
    if (!is_project_directory(project_dir)) {
        return module_summary_failure("Not a rollnw client project: " + project_dir.string());
    }

    const nlohmann::json manifest = load_manifest_json(project_dir);
    std::string module_resource = manifest.value("module", std::string{});
    if (module_resource.empty()) {
        return module_summary_failure("Project manifest does not name a module resource: " + project_dir.string());
    }

    fs::path module_path{module_resource};
    if (module_path.is_relative()) {
        module_path = project_dir / module_path;
    }

    std::error_code ec;
    if (!fs::is_regular_file(module_path, ec)) {
        return module_summary_failure("Module metadata not found: " + module_path.string());
    }

    if (module_path.extension() == ".json") {
        return load_json_module_summary(module_path);
    }
    return load_legacy_module_summary(module_path);
}

ProjectPreviewSettings load_project_preview_settings(const fs::path& project_dir)
{
    ProjectPreviewSettings result;
    if (!is_project_directory(project_dir)) {
        result.message = "Not a rollnw client project: " + project_dir.string();
        return result;
    }

    const nlohmann::json manifest = load_manifest_json(project_dir);
    const auto preview = manifest.find("preview");
    if (preview == manifest.end()) {
        result.ok = true;
        result.message = "Project has no preview test actor";
        return result;
    }
    if (!preview->is_object()) {
        result.message = "Project preview settings must be an object";
        return result;
    }

    const auto actor = preview->find("test_actor");
    if (actor == preview->end()) {
        result.ok = true;
        result.message = "Project has no preview test actor";
        return result;
    }
    if (!actor->is_string()) {
        result.message = "Project preview test_actor must be a string";
        return result;
    }

    std::string error;
    auto path = valid_project_creature_path(
        project_dir, fs::path{actor->get<std::string>()}, error);
    if (!path) {
        result.message = std::move(error);
        return result;
    }

    result.ok = true;
    result.test_actor = std::move(*path);
    result.message = "Loaded project preview settings";
    return result;
}

ProjectResult save_project_preview_test_actor(
    const fs::path& project_dir,
    const fs::path& relative_actor_path)
{
    if (!is_project_directory(project_dir)) {
        return failure("Not a rollnw client project: " + project_dir.string());
    }

    std::string error;
    auto path = valid_project_creature_path(
        project_dir, relative_actor_path, error);
    if (!path) {
        return failure(std::move(error));
    }

    nlohmann::json manifest = load_manifest_json(project_dir);
    const auto preview = manifest.find("preview");
    if (preview != manifest.end() && !preview->is_object()) {
        return failure("Project preview settings must be an object");
    }
    manifest["preview"]["test_actor"] = path->generic_string();
    if (!write_manifest(project_dir, manifest, error)) {
        return failure(std::move(error));
    }

    return success("Saved project preview test actor: " + path->generic_string());
}

ProjectResult initialize_project(const fs::path& project_dir, std::string module_name)
{
    return initialize_project_with_module_path(project_dir, std::move(module_name), kLegacyModulePath);
}

ProjectResult import_module_project(const fs::path& module_path,
    const fs::path& project_dir,
    const ProjectImportOptions& options)
{
    ensure_resource_name_services();

    std::error_code ec;
    if (!fs::is_regular_file(module_path, ec)) {
        return failure("Module file does not exist: " + module_path.string());
    }

    nw::Container* source_container = nullptr;
    nw::Erf legacy_module;
    if (options.format == ProjectImportFormat::json) {
        auto* source_module = nw::kernel::load_module(module_path, false);
        if (!source_module) {
            return failure("Failed to open NWN module for JSON import: " + module_path.string());
        }

        auto& resman = nw::kernel::resman();
        source_container = resman.module_container();
        const auto* source_erf = dynamic_cast<const nw::StaticErf*>(source_container);
        if (!source_erf || source_erf->type != nw::ErfType::mod
            || resman.module_format() != nw::ModuleResourceFormat::legacy_gff) {
            return failure("JSON import source is not the opened legacy module container: " + module_path.string());
        }
        if (resman.module_hak_count() != source_module->haks.size()) {
            return failure("Failed to open all module haks for JSON import: loaded "
                + std::to_string(resman.module_hak_count()) + " of "
                + std::to_string(source_module->haks.size()));
        }
    } else {
        legacy_module = nw::Erf{module_path};
        if (!legacy_module.valid() || legacy_module.type != nw::ErfType::mod) {
            return failure("Failed to read NWN module: " + module_path.string());
        }
    }

    bool initialized = false;
    if (!is_project_directory(project_dir)) {
        const auto init = initialize_project_with_module_path(project_dir,
            module_path.stem().string(),
            options.format == ProjectImportFormat::json ? kJsonModulePath : kLegacyModulePath);
        if (!init.ok) {
            return init;
        }
        initialized = init.initialized;
    } else {
        std::string error;
        if (!ensure_project_directories(project_dir, error)) {
            return failure(std::move(error));
        }
    }

    ProjectResult result;
    result.ok = true;
    result.initialized = initialized;

    std::string error;
    std::vector<nw::Resource> legacy_area_resources;
    auto import_resource = [&](const nw::Resource& resource, auto demand) {
        if (!result.ok) {
            return;
        }

        if (options.format == ProjectImportFormat::json
            && (resource.type == nw::ResourceType::are
                || resource.type == nw::ResourceType::git
                || resource.type == nw::ResourceType::gic)) {
            legacy_area_resources.push_back(resource);
            ++result.resource_count;
            return;
        }

        try {
            auto data = demand();
            data.name = resource;
            if (!write_resource(project_dir, std::move(data), options, error)) {
                result.ok = false;
                result.message = error;
                return;
            }
        } catch (const std::exception& e) {
            result.ok = false;
            result.message = "Failed to import resource " + resource.filename() + ": " + e.what();
            return;
        }

        ++result.resource_count;
    };

    if (source_container) {
        source_container->visit([&](const nw::Resource& resource, const nw::ContainerKey* key) {
            import_resource(resource, [&] { return source_container->demand(key); });
        });
    } else {
        legacy_module.visit([&](const nw::Resource& resource) {
            import_resource(resource, [&] { return legacy_module.demand(resource); });
        });
    }

    if (result.ok && options.format == ProjectImportFormat::json
        && !write_json_areas(project_dir, std::move(legacy_area_resources), result, error)) {
        result.ok = false;
        result.message = std::move(error);
    }

    if (result.ok) {
        result.message = "Imported " + std::to_string(result.resource_count)
            + " resources and " + std::to_string(result.area_map_count)
            + " area maps into " + project_dir.string();
        if (result.area_map_degraded_count > 0) {
            result.message += " (" + std::to_string(result.area_map_degraded_count)
                + " area maps contain missing-tile markers)";
        }
        if (result.area_map_failure_count > 0) {
            result.message += " (" + std::to_string(result.area_map_failure_count)
                + " area maps unavailable)";
        }
    }
    return result;
}

} // namespace nw::toolset
