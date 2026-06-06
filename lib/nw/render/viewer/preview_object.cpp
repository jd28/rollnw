#include "preview_object.hpp"

#include <nw/log.hpp>
#include <nw/resources/assets.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/util/error_context.hpp>
#include <nw/util/string.hpp>

#include <fstream>
#include <nlohmann/json.hpp>

namespace nw::render::viewer {

namespace {

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

} // namespace nw::render::viewer
