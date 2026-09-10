#include "blueprint_operations.hpp"

#include "resource_document.hpp"

#include <absl/container/flat_hash_set.h>
#include <nw/resources/ResourceManager.hpp>
#include <xxhash/xxh3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;
namespace nw::toolset {
namespace {
using Json = nlohmann::json;
constexpr int protocol_version = 1;

std::string read_bytes(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    std::string result{std::istreambuf_iterator<char>{input}, {}};
    if (!input.is_open() || input.bad()) { throw std::runtime_error("Cannot read " + path.string()); }
    return result;
}

void write_bytes(const fs::path& path, std::string_view bytes)
{
    std::optional<std::string> expected;
    if (fs::exists(path)) { expected = read_bytes(path); }
    const std::array rows{ResourceFileWrite{path, bytes, expected ? ResourceFileWriteMode::replace : ResourceFileWriteMode::create,
        expected ? std::optional<std::string_view>{*expected} : std::nullopt}};
    const auto results = write_resource_files_atomic(rows);
    if (!results[0].written) { throw std::runtime_error(results[0].error); }
}

void write_json(const fs::path& path, const Json& value) { write_bytes(path, value.dump(2) + "\n"); }

Json read_json(const fs::path& path)
{
    auto value = Json::parse(read_bytes(path));
    if (value.at("version") != protocol_version) { throw std::runtime_error("Unsupported blueprint operation version"); }
    return value;
}

bool inside(const fs::path& root, const fs::path& path)
{
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute()
        && std::none_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; });
}

Json fingerprint(std::string_view bytes)
{
    const auto hash = XXH3_128bits(bytes.data(), bytes.size());
    return Json::array({bytes.size(), hash.low64, hash.high64});
}

fs::path document_path(const Json& request, std::string_view relative)
{
    const fs::path project = fs::canonical(request.at("project").get<std::string>());
    const fs::path root = fs::canonical(project / request.at("root").get<std::string>());
    const fs::path path{relative};
    const auto target = fs::weakly_canonical(project / path);
    if (path.empty() || path.is_absolute() || !inside(project, root) || !inside(root, target)
        || fs::is_symlink(project / path) || target.extension() != ".json"
        || blueprint_document_object_type(
               ResourceType::from_extension(target.stem().extension().string()))
            == ObjectType::invalid) {
        throw std::runtime_error("Invalid authored document path: " + path.string());
    }
    return target;
}

Json request_for(const fs::path& operation)
{
    auto request = read_json(operation / "request.json");
    const auto project = fs::canonical(request.at("project").get<std::string>());
    const auto operations = fs::weakly_canonical(project / ".rollnw/operations/blueprints");
    if (fs::canonical(operation).parent_path() != operations) { throw std::runtime_error("Operation does not belong to this project"); }
    return request;
}

Resource source_for(const Json& request)
{
    const auto type = ResourceType::from_extension(request.at("type").get<std::string>());
    if (blueprint_object_type(type) == ObjectType::invalid) { throw std::runtime_error("Unsupported blueprint source"); }
    return Resource{request.at("resref").get<std::string>(), type};
}

std::vector<fs::path> discover_documents(const fs::path& operation, const Json& request)
{
    std::vector<fs::path> paths;
    const fs::path project{request.at("project").get<std::string>()};
    const auto area = request.at("area").get<std::string>();
    const auto live_area = request.value("live_area", std::string{});
    const auto excluded = live_area.empty() ? fs::path{} : document_path(request, live_area);
    if (!area.empty()) {
        const auto path = document_path(request, area);
        if (path != excluded) { paths.push_back(path); }
    } else {
        absl::flat_hash_set<Resource> keys;
        const auto root = project / request.at("root").get<std::string>();
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (fs::exists(operation / "cancel")) { throw std::runtime_error("Cancelled"); }
            const auto resource = Resource::from_path(entry.path(), false);
            if (!entry.is_regular_file()
                || blueprint_document_object_type(resource.type)
                    == ObjectType::invalid) {
                continue;
            }
            if (!keys.insert(resource).second) { throw std::runtime_error("Ambiguous module resource: " + resource.filename()); }
            const auto path = document_path(request, entry.path().lexically_relative(project).generic_string());
            if (path != excluded) { paths.push_back(path); }
        }
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) { throw std::runtime_error("Aliased authored document paths"); }
    }
    return paths;
}

void progress(const fs::path& operation, std::string_view stage, size_t complete, size_t total,
    std::string_view detail = {}, const Json& counts = Json::object(), std::string_view error = {})
{
    Json value = counts;
    value["version"] = protocol_version;
    value["stage"] = stage;
    value["completed"] = complete;
    value["total"] = total;
    value["detail"] = detail;
    value["error"] = error;
    write_json(operation / "progress.json", value);
}

void check_cancelled(const fs::path& operation)
{
    if (fs::exists(operation / "cancel")) { throw std::runtime_error("Cancelled"); }
}

Json frozen_json(const absl::flat_hash_map<Resource, Json>& snapshots)
{
    // Hash lookup owns identity; sorted keys define deterministic file output.
    std::vector<Resource> keys;
    for (const auto& [key, value] : snapshots) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    Json rows = Json::array();
    for (const auto key : keys) {
        rows.push_back({{"resref", key.resref.string()}, {"type", ResourceType::to_string(key.type)}, {"value", snapshots.at(key)}});
    }
    return Json{{"version", protocol_version}, {"rows", std::move(rows)}};
}

void check_source(const Json& request)
{
    const auto data = kernel::resman().demand(source_for(request));
    if (data.bytes.string_view() != request.at("source_bytes").get_ref<const std::string&>()) {
        throw std::runtime_error("The source blueprint changed; start the update again");
    }
}

void prepare(const fs::path& operation, const Json& request)
{
    progress(operation, "resolving", 0, 0, "Loading blueprint and item dependencies");
    check_cancelled(operation);
    check_source(request);
    const auto source = source_for(request);
    absl::flat_hash_map<Resource, Json> blueprints;
    std::string error;
    const std::array sources{source};
    if (!snapshot_blueprints(sources, blueprints, error)) { throw std::runtime_error(error); }
    write_json(operation / "blueprints.json", frozen_json(blueprints));

    progress(operation, "discovering", 0, 0, "Finding authored documents");
    const fs::path project{request.at("project").get<std::string>()};
    const auto paths = discover_documents(operation, request);
    Json manifest{{"version", protocol_version}, {"stage", "preparing"}, {"rows", Json::array()}, {"documents", Json::array()}};
    Json counts{{"instances", 0u}, {"covered", 0u}, {"reference_uses", 0u}};
    std::vector<ResourceType::type> types;
    size_t completed = 0;
    for (const auto& path : paths) {
        check_cancelled(operation);
        const auto relative = path.lexically_relative(project).generic_string();
        progress(operation, "scanning", completed, paths.size(), relative, counts);
        const auto bytes = read_bytes(path);
        manifest["documents"].push_back({{"path", relative}, {"fingerprint", fingerprint(bytes)}});
        const auto value = Json::parse(bytes);
        const auto type = Resource::from_path(path, false).type;
        auto references = collect_blueprint_references(value, type, source);
        if (!references.error.empty()) { throw std::runtime_error(relative + ": " + references.error); }
        counts["reference_uses"] = counts["reference_uses"].get<size_t>() + references.reference_uses;
        size_t matches = 0;
        size_t covered = 0;
        for (const auto& row : references.rows) {
            row.covered ? ++covered : ++matches;
        }
        counts["instances"] = counts["instances"].get<size_t>() + matches;
        counts["covered"] = counts["covered"].get<size_t>() + covered;
        if (matches) {
            const auto index = manifest["rows"].size();
            write_bytes(operation / (std::to_string(index) + ".before"), bytes);
            manifest["rows"].push_back({{"path", relative}, {"instances", matches}, {"covered", covered}, {"changed", false}, {"before", fingerprint(bytes)}});
            types.push_back(type);
        }
        ++completed;
    }
    progress(operation, "scanning", completed, paths.size(), {}, counts);
    completed = 0;
    for (auto& row : manifest["rows"]) {
        check_cancelled(operation);
        const auto path = row.at("path").get<std::string>();
        progress(operation, "preparing", completed, types.size(), path, counts);
        const auto before = read_bytes(operation / (std::to_string(completed) + ".before"));
        auto value = Json::parse(before);
        auto references = collect_blueprint_references(value, types[completed], source);
        std::array documents{BlueprintUpdateDocument{types[completed], std::move(value), std::move(references)}};
        if (!prepare_blueprint_updates(documents, source, blueprints, error)) { throw std::runtime_error(path + ": " + error); }
        const bool changed = documents[0].value != Json::parse(before);
        row["changed"] = changed;
        const auto after = changed ? documents[0].value.dump(2) + "\n" : before;
        row["after"] = fingerprint(after);
        write_bytes(operation / (std::to_string(completed) + ".after"), after);
        ++completed;
    }
    manifest["stage"] = "ready";
    manifest["counts"] = counts;
    write_json(operation / "manifest.json", manifest);
    progress(operation, "ready", completed, types.size(), "Review the instance replacements before applying", counts);
}

bool restore(const fs::path& operation, const Json& request, Json& manifest, bool require_clean, std::string& error)
{
    const auto& rows = manifest.at("rows");
    const auto inspect = [&](size_t index) {
        if (fingerprint(read_bytes(operation / (std::to_string(index) + ".before"))) != rows[index].at("before")
            || fingerprint(read_bytes(operation / (std::to_string(index) + ".after"))) != rows[index].at("after")) {
            throw std::runtime_error("Recovery copy was modified; original files were not overwritten");
        }
        const auto target = document_path(request, rows[index].at("path").get<std::string>());
        const auto current = read_bytes(target);
        if (current != read_bytes(operation / (std::to_string(index) + ".before"))
            && current != read_bytes(operation / (std::to_string(index) + ".after"))) {
            throw std::runtime_error("Recovery conflict: file has subsequent edits: " + target.string());
        }
    };
    if (require_clean) {
        for (size_t index = 0; index < rows.size(); ++index) {
            inspect(index);
        }
    }
    manifest["stage"] = "restoring";
    write_json(operation / "manifest.json", manifest);
    for (size_t index = 0; index < rows.size(); ++index) {
        try {
            inspect(index);
            const auto path = rows[index].at("path").get<std::string>();
            progress(operation, "restoring", index, rows.size(), path);
            const auto target = document_path(request, path);
            const auto before = read_bytes(operation / (std::to_string(index) + ".before"));
            const auto after = read_bytes(operation / (std::to_string(index) + ".after"));
            if (read_bytes(target) == before) { continue; }
            const std::array writes{ResourceFileWrite{target, before, ResourceFileWriteMode::replace, after}};
            const auto results = write_resource_files_atomic(writes);
            if (!results[0].written) { throw std::runtime_error(results[0].error); }
        } catch (const std::exception& ex) {
            if (!error.empty()) { error += "\n"; }
            error += ex.what();
        }
    }
    manifest["stage"] = error.empty() ? "restored" : "recovery";
    write_json(operation / "manifest.json", manifest);
    progress(operation, manifest["stage"].get<std::string>(), rows.size(), rows.size(), {}, {}, error);
    return error.empty();
}

void commit(const fs::path& operation, const Json& request)
{
    auto manifest = read_json(operation / "manifest.json");
    if (manifest.at("stage") != "ready") { throw std::runtime_error("Operation is not ready to apply"); }
    check_cancelled(operation);
    progress(operation, "checking", 0, 0, "Checking source and staged document versions");
    std::string error;
    if (!kernel::resman().refresh_module_resources(error)) { throw std::runtime_error(error); }
    check_source(request);
    absl::flat_hash_map<Resource, Json> blueprints;
    const std::array sources{source_for(request)};
    if (!snapshot_blueprints(sources, blueprints, error)) { throw std::runtime_error(error); }
    if (frozen_json(blueprints) != read_json(operation / "blueprints.json")) { throw std::runtime_error("Blueprint dependencies changed; prepare the update again"); }
    const auto paths = discover_documents(operation, request);
    if (paths.size() != manifest.at("documents").size()) { throw std::runtime_error("The scoped document set changed; prepare the update again"); }
    for (size_t index = 0; index < paths.size(); ++index) {
        const auto& expected = manifest.at("documents")[index];
        if (paths[index] != document_path(request, expected.at("path").get<std::string>())
            || fingerprint(read_bytes(paths[index])) != expected.at("fingerprint")) {
            throw std::runtime_error("A scoped document changed; prepare the update again: " + paths[index].string());
        }
    }
    const auto& rows = manifest.at("rows");
    for (size_t index = 0; index < rows.size(); ++index) {
        const auto before = read_bytes(operation / (std::to_string(index) + ".before"));
        const auto after = read_bytes(operation / (std::to_string(index) + ".after"));
        if (fingerprint(before) != rows[index].at("before") || fingerprint(after) != rows[index].at("after")) {
            throw std::runtime_error("Staged blueprint document bytes changed");
        }
        const auto target = document_path(request, rows[index].at("path").get<std::string>());
        if (read_bytes(target) != read_bytes(operation / (std::to_string(index) + ".before"))) {
            throw std::runtime_error("Document changed since preparation: " + target.string());
        }
    }
    manifest["stage"] = "saving";
    write_json(operation / "manifest.json", manifest);
    try {
        for (size_t index = 0; index < rows.size(); ++index) {
            check_cancelled(operation);
            const auto path = rows[index].at("path").get<std::string>();
            progress(operation, "saving", index, rows.size(), path, manifest.at("counts"));
            if (!rows[index].at("changed").get<bool>()) { continue; }
            const auto before = read_bytes(operation / (std::to_string(index) + ".before"));
            const auto after = read_bytes(operation / (std::to_string(index) + ".after"));
            const std::array writes{ResourceFileWrite{document_path(request, path), after, ResourceFileWriteMode::replace, before}};
            const auto results = write_resource_files_atomic(writes);
            if (!results[0].written) { throw std::runtime_error(results[0].error); }
        }
        manifest["stage"] = "saved";
        write_json(operation / "manifest.json", manifest);
        progress(operation, "saved", rows.size(), rows.size(), "Reloading affected open documents", manifest.at("counts"));
    } catch (const std::exception& ex) {
        std::string recovery_error;
        restore(operation, request, manifest, false, recovery_error);
        throw std::runtime_error(std::string{ex.what()} + (recovery_error.empty() ? "; original files restored" : "\n" + recovery_error));
    }
}
} // namespace

fs::path create_blueprint_update_operation(const fs::path& project, Resource source, const fs::path& current_area, std::string& error, const fs::path& live_area)
{
    error.clear();
    try {
        auto& resources = kernel::resman();
        if (!resources.module_container() || resources.module_format() != ModuleResourceFormat::native_json
            || blueprint_object_type(source.type) == ObjectType::invalid) { throw std::runtime_error("Reference updates require a supported blueprint in a native project"); }
        const auto root = fs::canonical(resources.module_container()->path());
        const auto project_root = fs::canonical(project);
        if (!inside(project_root, root) || fs::exists(root / "package.json")) { throw std::runtime_error("Reference updates require a flat module resource namespace"); }
        const auto source_data = resources.demand(source);
        if (source_data.bytes.size() == 0) { throw std::runtime_error("The source blueprint is missing"); }
        if (!find_unfinished_blueprint_operations(project_root).empty()) { throw std::runtime_error("Restore the interrupted blueprint operation before starting another update"); }
        Json request{{"version", protocol_version}, {"project", project_root.generic_string()},
            {"install", fs::absolute(kernel::config().install_path()).generic_string()},
            {"user", fs::absolute(kernel::config().user_path()).generic_string()},
            {"profile", kernel::config().profile().value_or("")},
            {"root", root.lexically_relative(project_root).generic_string()}, {"area", current_area.generic_string()},
            {"live_area", live_area.generic_string()},
            {"type", ResourceType::to_string(source.type)}, {"resref", source.resref.string()},
            {"source_bytes", source_data.bytes.string_view()}};
        if (!current_area.empty() && Resource::from_path(document_path(request, current_area.generic_string()), false).type != ResourceType::caf) {
            throw std::runtime_error("Current Area must identify a native area document");
        }
        if (!live_area.empty() && Resource::from_path(document_path(request, live_area.generic_string()), false).type != ResourceType::caf) {
            throw std::runtime_error("Live Area must identify a native area document");
        }
        const auto base = project_root / ".rollnw/operations/blueprints";
        fs::create_directories(base);
        static std::atomic<uint64_t> sequence{0};
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto tick = std::chrono::system_clock::now().time_since_epoch().count();
            const auto operation = base / (std::to_string(tick) + "-" + std::to_string(sequence.fetch_add(1)));
            if (!fs::create_directory(operation)) { continue; }
            write_json(operation / "request.json", request);
            progress(operation, "starting", 0, 0, "Starting blueprint preparation");
            return operation;
        }
        throw std::runtime_error("Cannot reserve a blueprint operation directory");
    } catch (const std::exception& ex) {
        error = ex.what();
        return {};
    }
}

BlueprintOperationProgress read_blueprint_operation_progress(const fs::path& operation)
{
    BlueprintOperationProgress result;
    const auto count = [](const Json& value, const char* key) -> size_t {
        const auto it = value.find(key);
        if (it == value.end()) { return 0; }
        if (!it->is_number_integer() || (!it->is_number_unsigned() && it->get<int64_t>() < 0)
            || it->get<uint64_t>() > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error("Invalid blueprint progress count");
        }
        return it->get<size_t>();
    };
    try {
        const auto value = read_json(operation / "progress.json");
        result.stage = value.at("stage").get<std::string>();
        result.completed = count(value, "completed");
        result.total = count(value, "total");
        if (result.total && result.completed > result.total) { throw std::runtime_error("Invalid blueprint progress counts"); }
        result.instances = count(value, "instances");
        result.covered = count(value, "covered");
        result.reference_uses = count(value, "reference_uses");
        result.detail = value.value("detail", std::string{});
        result.error = value.value("error", std::string{});
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }
    return result;
}

std::vector<fs::path> blueprint_operation_documents(const fs::path& operation)
{
    const auto request = request_for(operation);
    const auto manifest = read_json(operation / "manifest.json");
    std::vector<fs::path> result;
    for (const auto& row : manifest.at("rows")) {
        if (row.at("changed").get<bool>()) { result.push_back(document_path(request, row.at("path").get<std::string>())); }
    }
    return result;
}

std::vector<fs::path> find_unfinished_blueprint_operations(const fs::path& project)
{
    std::vector<fs::path> result;
    const auto base = project / ".rollnw/operations/blueprints";
    if (!fs::exists(base)) { return result; }
    for (const auto& entry : fs::directory_iterator(base)) {
        if (!entry.is_directory() || !fs::exists(entry.path() / "manifest.json")) { continue; }
        const auto manifest = read_json(entry.path() / "manifest.json");
        const auto stage = manifest.at("stage").get<std::string>();
        if (stage == "saving" || stage == "restoring" || stage == "recovery" || stage == "saved") { result.push_back(entry.path()); }
    }
    std::sort(result.begin(), result.end());
    return result;
}

fs::path latest_blueprint_operation(const fs::path& project)
{
    const auto base = project / ".rollnw/operations/blueprints";
    fs::path result;
    if (!fs::exists(base)) { return result; }
    for (const auto& entry : fs::directory_iterator(base)) {
        if (!entry.is_directory() || !fs::exists(entry.path() / "manifest.json")) { continue; }
        const auto manifest = read_json(entry.path() / "manifest.json");
        if (manifest.at("stage") == "complete" && entry.path() > result) { result = entry.path(); }
    }
    return result;
}

bool run_blueprint_update_operation(const fs::path& operation, std::string_view phase, std::string& error)
{
    error.clear();
    try {
        const auto request = request_for(operation);
        if (phase == "prepare") {
            prepare(operation, request);
        } else if (phase == "commit") {
            commit(operation, request);
        } else if (phase == "restore") {
            auto manifest = read_json(operation / "manifest.json");
            return restore(operation, request, manifest, true, error);
        } else {
            throw std::runtime_error("Unknown blueprint operation phase");
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        try {
            progress(operation, "failed", 0, 0, {}, {}, error);
        } catch (...) { /* The durable manifest remains authoritative. */
        }
        return false;
    }
}

bool cancel_blueprint_update_operation(const fs::path& operation, std::string& error)
{
    try {
        write_bytes(operation / "cancel", "cancel\n");
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool finish_blueprint_update_operation(const fs::path& operation, std::string& error)
{
    try {
        auto manifest = read_json(operation / "manifest.json");
        if (manifest.at("stage") != "saved") { throw std::runtime_error("Operation has not finished saving"); }
        manifest["stage"] = "complete";
        write_json(operation / "manifest.json", manifest);
        progress(operation, "complete", manifest.at("rows").size(), manifest.at("rows").size(), "Blueprint instances updated", manifest.at("counts"));
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}
} // namespace nw::toolset
