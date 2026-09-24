#include "area_creation.hpp"

#include "blueprint_edits.hpp"
#include "object_document.hpp"
#include "resource_document.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace nw::toolset {
namespace {

bool inside(const fs::path& outer, const fs::path& inner)
{
    const auto relative = inner.lexically_relative(outer);
    return !relative.empty() && !relative.is_absolute()
        && *relative.begin() != "..";
}

std::string trim_ascii(std::string_view value)
{
    size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return std::string{value.substr(first, last - first)};
}

bool read_file(const fs::path& path, std::string& output)
{
    std::ifstream input{path, std::ios::binary};
    output.assign(std::istreambuf_iterator<char>{input}, {});
    return input.is_open() && !input.bad();
}

bool is_area_ground_tile_candidate(
    const Tileset& tileset, size_t index) noexcept
{
    if (index >= tileset.tile_topologies.size()
        || index >= tileset.grouped_tiles.size()) {
        return false;
    }
    const auto& topology = tileset.tile_topologies[index];
    return topology.valid && tileset.grouped_tiles[index] == 0
        && std::ranges::all_of(topology.terrain,
            [&](int32_t value) { return value == tileset.default_terrain; })
        && std::ranges::all_of(topology.crosser,
            [](int32_t value) { return value == -1; })
        && std::ranges::all_of(topology.height,
            [&](int32_t value) { return value == topology.height[0]; });
}

bool model_has_authored_shadow_caster(StringView model)
{
    if (model.empty()) { return false; }
    auto data = kernel::resman().demand(
        Resource{Resref{model}, ResourceType::mdl});
    if (data.bytes.size() == 0) { return false; }
    const nw::model::Mdl mdl{std::move(data)};
    if (!mdl.valid()) { return false; }
    return std::ranges::any_of(mdl.model.nodes, [](const auto& node) {
        const auto* mesh = dynamic_cast<const nw::model::TrimeshNode*>(
            node.get());
        return mesh && mesh->render && mesh->shadow
            && !mesh->vertices.empty() && !mesh->indices.empty();
    });
}

void rollback_created_areas(std::span<const PreparedNewArea> rows,
    std::span<const ResourceFileWriteResult> writes, std::string& error)
{
    for (size_t index = 0; index < rows.size(); ++index) {
        if (!writes[index].written) { continue; }
        std::string current;
        std::error_code ec;
        if (!read_file(rows[index].target, current)
            || current != rows[index].bytes
            || !fs::remove(rows[index].target, ec)) {
            error += "; could not remove " + rows[index].target.string();
        }
    }
}

} // namespace

bool canonical_area_ground_tile(
    const Tileset& tileset, AreaTile& output) noexcept
{
    if (tileset.default_terrain < 0
        || tileset.tile_topologies.size() != tileset.tiles.size()
        || tileset.grouped_tiles.size() != tileset.tiles.size()) {
        return false;
    }
    for (size_t index = 0; index < tileset.tile_topologies.size(); ++index) {
        if (!is_area_ground_tile_candidate(tileset, index)) { continue; }
        output = AreaTile{.id = static_cast<int32_t>(index)};
        return true;
    }
    return false;
}

bool preferred_area_ground_tile(
    const Tileset& tileset, AreaTile& output)
{
    AreaTile fallback;
    bool has_fallback = false;
    for (size_t index = 0; index < tileset.tiles.size(); ++index) {
        if (!is_area_ground_tile_candidate(tileset, index)) { continue; }
        if (!has_fallback) {
            fallback = AreaTile{.id = static_cast<int32_t>(index)};
            has_fallback = true;
        }
        if (model_has_authored_shadow_caster(tileset.tiles[index].model)) {
            output = AreaTile{.id = static_cast<int32_t>(index)};
            return true;
        }
    }
    if (has_fallback) {
        output = fallback;
        LOG_F(WARNING,
            "Tileset has no flat default ground tile with authored shadow-caster geometry; using SET row {}",
            fallback.id);
    }
    return has_fallback;
}

PreparedNewAreas prepare_new_areas(const fs::path& project,
    std::span<const NewAreaRequest> requests)
{
    PreparedNewAreas result;
    if (requests.empty()) { return result; }
    auto& resources = kernel::resman();
    if (project.empty() || !resources.module_container()
        || resources.module_format() != ModuleResourceFormat::native_json) {
        result.error = "Area creation requires an active native project";
        return result;
    }

    try {
        result.project = fs::canonical(project);
        const auto root = fs::canonical(
            fs::path{resources.module_container()->path()});
        if (fs::exists(root / "package.json")) {
            result.error = "Area creation requires a flat module resource namespace";
            return result;
        }
        if (!inside(result.project, root)) {
            result.error = "Active module resources are outside this project";
            return result;
        }
        result.resource_generation = resources.generation();

        std::unordered_set<std::string> existing;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) { continue; }
            const auto resource = Resource::from_path(entry.path(), false);
            if (resource.type == ResourceType::caf) {
                existing.insert(resource.filename());
            }
        }
        std::unordered_set<std::string> destinations;
        std::unordered_map<std::string, AreaTile> ground_tiles;
        result.rows.reserve(requests.size());
        result.map_sources.reserve(requests.size());
        ground_tiles.reserve(requests.size());
        for (const auto& request : requests) {
            std::string normalized;
            if (!validate_blueprint_resref(
                    request.resref, normalized, result.error)) {
                break;
            }
            const Resource resource{normalized, ResourceType::caf};
            if (!destinations.insert(resource.filename()).second) {
                result.error = "Duplicate area ResRef in creation batch: "
                    + resource.filename();
                break;
            }
            if (existing.contains(resource.filename())
                || resources.contains(resource)) {
                result.error = "Area ResRef already exists: "
                    + resource.filename();
                break;
            }
            if (request.width < kMinimumNewAreaDimension
                || request.width > kMaximumNewAreaDimension
                || request.height < kMinimumNewAreaDimension
                || request.height > kMaximumNewAreaDimension) {
                result.error = "Area width and height must be between 2 and 32 tiles";
                break;
            }
            const std::string name = trim_ascii(request.name);
            if (name.empty()) {
                result.error = "Area name is required";
                break;
            }
            const auto directory = fs::canonical(
                request.directory.is_absolute()
                    ? request.directory
                    : result.project / request.directory);
            if (!inside(root, directory) || !fs::is_directory(directory)) {
                result.error = "Area directory must be inside the loaded project resource root";
                break;
            }
            const auto target = directory / (resource.filename() + ".json");
            if (fs::exists(target) || fs::is_symlink(target)) {
                result.error = "Area destination already exists: "
                    + target.string();
                break;
            }

            AreaTile ground;
            const auto tileset_key = request.tileset.string();
            if (const auto found = ground_tiles.find(tileset_key);
                found != ground_tiles.end()) {
                ground = found->second;
            } else {
                auto* tileset = kernel::tilesets().get(
                    request.tileset.view());
                if (!resources.contains(
                        Resource{request.tileset, ResourceType::set})
                    || !tileset
                    || !preferred_area_ground_tile(*tileset, ground)) {
                    result.error = "Tileset cannot provide a flat default ground tile: "
                        + request.tileset.string();
                    break;
                }
                ground_tiles.emplace(tileset_key, ground);
            }

            ObjectDocument owner;
            auto* area = kernel::objects().make<Area>();
            if (!area || !owner.adopt(area->handle())) {
                if (area) { kernel::objects().destroy(area->handle()); }
                result.error = "Area allocation failed";
                break;
            }
            area->resref = resource.resref;
            area->tag = kernel::strings().intern(resource.resref.view());
            if (!area->name.add(LanguageID::english, name)) {
                result.error = "Area name initialization failed";
                break;
            }
            area->tileset_resref = request.tileset;
            area->width = request.width;
            area->height = request.height;
            area->shadow_opacity = 50;
            area->weather.day_night_cycle = 1;
            area->weather.is_night = 0;
            area->weather.sun_shadows = 1;
            area->weather.moon_shadows = 1;
            area->weather.color_moon_diffuse = 0x00c86464u;
            area->weather.color_moon_fog = 0x00643232u;
            area->weather.color_sun_ambient = 0x00643232u;
            area->weather.color_sun_diffuse = 0x00ffffffu;
            area->weather.color_sun_fog = 0x00917e68u;
            area->weather.fog_clip_distance = 45.0f;
            area->tiles.assign(
                static_cast<size_t>(request.width)
                    * static_cast<size_t>(request.height),
                ground);
            if (!area->instantiate()) {
                result.error = "Area initialization failed";
                break;
            }
            nlohmann::json serialized;
            serialize(area, serialized);
            const std::array<const Area*, 1> area_batch{area};
            auto map_sources = collect_area_map_sources(area_batch);
            if (map_sources.size() != 1) {
                result.error = "Area map source preparation failed";
                break;
            }
            PreparedNewArea row;
            row.request = request;
            row.request.resref = normalized;
            row.request.name = name;
            row.resource = resource;
            row.target = target;
            row.relative_path = target.lexically_relative(result.project);
            row.bytes = serialized.dump(2) + "\n";
            result.rows.push_back(std::move(row));
            result.map_sources.push_back(std::move(map_sources.front()));
        }
    } catch (const std::exception& ex) {
        result.error = "Area preparation failed: " + std::string{ex.what()};
    }
    if (!result.error.empty()) {
        result.rows.clear();
        result.map_sources.clear();
    }
    return result;
}

NewAreaPublishResult publish_new_areas(
    const PreparedNewAreas& prepared)
{
    NewAreaPublishResult result;
    result.rows.resize(prepared.rows.size());
    if (!prepared.ok() || prepared.rows.empty()) { return result; }
    if (prepared.map_sources.size() != prepared.rows.size()) {
        const std::string error
            = "Prepared area map sources do not match the area batch";
        for (size_t index = 0; index < result.rows.size(); ++index) {
            result.rows[index].relative_path
                = prepared.rows[index].relative_path;
            result.rows[index].error = error;
        }
        return result;
    }

    std::string error;
    std::vector<ResourceFileWriteResult> written(prepared.rows.size());
    try {
        auto& resources = kernel::resman();
        if (resources.generation() != prepared.resource_generation
            || !resources.module_container()
            || resources.module_format() != ModuleResourceFormat::native_json) {
            error = "Project resources changed since area preparation";
        } else {
            const auto root = fs::canonical(
                fs::path{resources.module_container()->path()});
            for (const auto& row : prepared.rows) {
                if (!inside(prepared.project, root)
                    || !inside(root, row.target)
                    || fs::weakly_canonical(row.target) != row.target
                    || !fs::is_directory(row.target.parent_path())
                    || fs::exists(row.target) || fs::is_symlink(row.target)
                    || resources.contains(row.resource)) {
                    error = "Area destination changed since preparation";
                    break;
                }
            }
        }
        if (error.empty()) {
            std::vector<ResourceFileWrite> writes;
            writes.reserve(prepared.rows.size());
            for (const auto& row : prepared.rows) {
                writes.push_back({row.target, row.bytes,
                    ResourceFileWriteMode::create, {}});
            }
            written = write_resource_files_atomic(writes);
            const auto failed = std::ranges::find_if(written,
                [](const auto& row) { return !row.written; });
            if (failed != written.end()) {
                error = failed->error.empty()
                    ? "Area file creation failed"
                    : failed->error;
            }
        }
        if (!error.empty()) {
            rollback_created_areas(prepared.rows, written, error);
        } else {
            String refresh_error;
            if (!resources.refresh_module_resources(refresh_error)) {
                error = "Area files were created, but resource refresh failed: "
                    + refresh_error;
                rollback_created_areas(prepared.rows, written, error);
            }
        }
    } catch (const std::exception& ex) {
        error = "Area publication failed: " + std::string{ex.what()};
        rollback_created_areas(prepared.rows, written, error);
    }

    for (size_t index = 0; index < result.rows.size(); ++index) {
        result.rows[index].relative_path = prepared.rows[index].relative_path;
        result.rows[index].saved = error.empty();
        result.rows[index].published = error.empty();
        result.rows[index].error = error;
    }
    if (!error.empty()) { return result; }

    try {
        result.maps = write_project_area_maps(
            prepared.project, prepared.map_sources);
    } catch (const std::exception& ex) {
        result.maps.failed = prepared.map_sources.size();
        result.maps.first_error
            = "Area map generation failed: " + std::string{ex.what()};
    }
    return result;
}

} // namespace nw::toolset
