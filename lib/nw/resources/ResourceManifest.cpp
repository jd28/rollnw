#include "ResourceManifest.hpp"

#include "../formats/Ini.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

namespace nw {
namespace {

using Json = nlohmann::json;
using namespace std::literals;
namespace fs = std::filesystem;

enum class ManifestRoot : uint8_t {
    install,
    user,
};

struct ManifestVersion {
    std::optional<GameVersion> value;
};

bool has_only_fields(const Json& row, std::span<const std::string_view> fields,
    size_t row_index, String& diagnostic)
{
    for (const auto& [name, _] : row.items()) {
        if (std::ranges::find(fields, name) == fields.end()) {
            diagnostic = fmt::format(
                "resource manifest row {} has unknown field '{}'", row_index, name);
            return false;
        }
    }
    return true;
}

bool read_required_string(const Json& row, std::string_view field,
    size_t row_index, String& output, String& diagnostic, bool allow_empty = false)
{
    const auto found = row.find(field);
    if (found == row.end() || !found->is_string()) {
        diagnostic = fmt::format(
            "resource manifest row {} requires string field '{}'", row_index, field);
        return false;
    }
    output = found->get<std::string>();
    if (!allow_empty && output.empty()) {
        diagnostic = fmt::format(
            "resource manifest row {} field '{}' cannot be empty", row_index, field);
        return false;
    }
    return true;
}

bool parse_root(StringView value, ManifestRoot& output)
{
    if (value == "install") {
        output = ManifestRoot::install;
    } else if (value == "user") {
        output = ManifestRoot::user;
    } else {
        return false;
    }
    return true;
}

bool parse_version(const Json& row, size_t row_index,
    ManifestVersion& output, String& diagnostic)
{
    const auto found = row.find("version");
    if (found == row.end()) {
        return true;
    }
    if (!found->is_string()) {
        diagnostic = fmt::format(
            "resource manifest row {} field 'version' must be a string", row_index);
        return false;
    }

    const auto version = found->get<std::string>();
    if (version == "ee") {
        output.value = GameVersion::vEE;
    } else if (version == "1.69") {
        output.value = GameVersion::v1_69;
    } else {
        diagnostic = fmt::format(
            "resource manifest row {} has unsupported version '{}'", row_index, version);
        return false;
    }
    return true;
}

bool parse_precedence(StringView value, ResourceManifestPrecedence& output)
{
    if (value == "base") {
        output = ResourceManifestPrecedence::base;
    } else if (value == "override") {
        output = ResourceManifestPrecedence::override;
    } else {
        return false;
    }
    return true;
}

bool parse_resource_type(StringView value, ResourceType::type& output)
{
    if (value == "any") {
        output = ResourceType::invalid;
    } else if (value == "texture") {
        output = ResourceType::texture;
    } else if (value == "sound") {
        output = ResourceType::sound;
    } else {
        return false;
    }
    return true;
}

bool valid_relative_path(StringView value, bool allow_empty,
    bool allow_language, size_t row_index, StringView field, String& diagnostic)
{
    if (value.empty()) {
        if (allow_empty) { return true; }
        diagnostic = fmt::format(
            "resource manifest row {} field '{}' cannot be empty", row_index, field);
        return false;
    }

    const fs::path path{value};
    if (path.is_absolute() || path.has_root_path()) {
        diagnostic = fmt::format(
            "resource manifest row {} field '{}' must be relative", row_index, field);
        return false;
    }

    size_t language_components = 0;
    for (const auto& component : path) {
        const auto text = component.string();
        if (text == "..") {
            diagnostic = fmt::format(
                "resource manifest row {} field '{}' cannot contain '..'", row_index, field);
            return false;
        }
        if (text == "$language") {
            ++language_components;
        } else if (text.find('$') != std::string::npos) {
            diagnostic = fmt::format(
                "resource manifest row {} field '{}' has unsupported path substitution '{}'",
                row_index, field, text);
            return false;
        }
    }
    if ((!allow_language && language_components != 0) || language_components > 1) {
        diagnostic = fmt::format(
            "resource manifest row {} field '{}' has invalid $language substitution",
            row_index, field);
        return false;
    }
    return true;
}

bool row_enabled(ManifestRoot root, const ManifestVersion& version,
    const ResourceManifestContext& context)
{
    const bool root_enabled = root == ManifestRoot::install
        ? context.include_install
        : context.include_user;
    return root_enabled && (!version.value || *version.value == context.version);
}

const fs::path& root_path(ManifestRoot root, const ResourceManifestContext& context)
{
    return root == ManifestRoot::install
        ? context.install_root
        : context.user_root;
}

bool resolve_directory(StringView authored, const fs::path& root,
    LanguageID language, fs::path& output)
{
    output = root;
    for (const auto& component : fs::path{authored}) {
        if (component == "$language") {
            if (language == LanguageID::english) {
                return false;
            }
            output /= Language::to_string(language);
        } else {
            output /= component;
        }
    }
    return true;
}

bool append_container(ResourceManifestContainer container,
    size_t row_index, Vector<ResourceManifestContainer>& output,
    std::set<std::pair<fs::path, String>>& seen, String& diagnostic)
{
    auto key = std::make_pair(
        (container.directory / container.name).lexically_normal(), container.name);
    if (!seen.insert(std::move(key)).second) {
        diagnostic = fmt::format(
            "resource manifest row {} declares duplicate container '{}/{}'",
            row_index, container.directory.string(), container.name);
        return false;
    }
    output.push_back(std::move(container));
    return true;
}

bool parse_container_row(const Json& row, size_t row_index,
    const ResourceManifestContext& context,
    Vector<ResourceManifestContainer>& output,
    std::set<String>& declarations,
    std::set<std::pair<fs::path, String>>& seen, String& diagnostic)
{
    static constexpr std::array fields{
        "kind"sv,
        "version"sv,
        "root"sv,
        "directory"sv,
        "name"sv,
        "precedence"sv,
        "resource_type"sv,
    };
    if (!has_only_fields(row, fields, row_index, diagnostic)) { return false; }

    String root_name;
    String directory;
    String name;
    String precedence_name;
    String resource_type_name;
    if (!read_required_string(row, "root", row_index, root_name, diagnostic)
        || !read_required_string(row, "directory", row_index, directory,
            diagnostic, true)
        || !read_required_string(row, "name", row_index, name, diagnostic)
        || !read_required_string(row, "precedence", row_index,
            precedence_name, diagnostic)
        || !read_required_string(row, "resource_type", row_index,
            resource_type_name, diagnostic)) {
        return false;
    }

    ManifestRoot root;
    ManifestVersion version;
    ResourceManifestPrecedence precedence;
    ResourceType::type resource_type;
    if (!parse_root(root_name, root)) {
        diagnostic = fmt::format(
            "resource manifest row {} has invalid root '{}'", row_index, root_name);
        return false;
    }
    if (!parse_version(row, row_index, version, diagnostic)) { return false; }
    if (!parse_precedence(precedence_name, precedence)) {
        diagnostic = fmt::format(
            "resource manifest row {} has invalid precedence '{}'",
            row_index, precedence_name);
        return false;
    }
    if (!parse_resource_type(resource_type_name, resource_type)) {
        diagnostic = fmt::format(
            "resource manifest row {} has invalid resource_type '{}'",
            row_index, resource_type_name);
        return false;
    }
    if (!valid_relative_path(directory, true, true, row_index,
            "directory", diagnostic)
        || !valid_relative_path(name, false, false, row_index,
            "name", diagnostic)) {
        return false;
    }
    const auto declaration = fmt::format(
        "{}/{}/{}", root_name, directory, name);
    if (!declarations.insert(declaration).second) {
        diagnostic = fmt::format(
            "resource manifest row {} declares duplicate container '{}'",
            row_index, declaration);
        return false;
    }
    if (!row_enabled(root, version, context)) { return true; }

    fs::path resolved_directory;
    if (!resolve_directory(directory, root_path(root, context),
            context.language, resolved_directory)) {
        return true;
    }
    return append_container({
                                .directory = std::move(resolved_directory),
                                .name = std::move(name),
                                .precedence = precedence,
                                .resource_type = resource_type,
                            },
        row_index, output, seen, diagnostic);
}

bool parse_ini_series_row(const Json& row, size_t row_index,
    const ResourceManifestContext& context,
    Vector<ResourceManifestContainer>& output,
    std::set<std::pair<fs::path, String>>& seen, String& diagnostic)
{
    static constexpr std::array fields{
        "kind"sv,
        "version"sv,
        "root"sv,
        "file"sv,
        "section"sv,
        "key_prefix"sv,
        "digits"sv,
        "container_directory"sv,
        "precedence"sv,
    };
    if (!has_only_fields(row, fields, row_index, diagnostic)) { return false; }

    String root_name;
    String file;
    String section;
    String key_prefix;
    String container_directory;
    String precedence_name;
    if (!read_required_string(row, "root", row_index, root_name, diagnostic)
        || !read_required_string(row, "file", row_index, file, diagnostic)
        || !read_required_string(row, "section", row_index, section, diagnostic)
        || !read_required_string(row, "key_prefix", row_index,
            key_prefix, diagnostic)
        || !read_required_string(row, "container_directory", row_index,
            container_directory, diagnostic, true)
        || !read_required_string(row, "precedence", row_index,
            precedence_name, diagnostic)) {
        return false;
    }

    const auto digits_found = row.find("digits");
    if (digits_found == row.end() || !digits_found->is_number_integer()) {
        diagnostic = fmt::format(
            "resource manifest row {} requires integer field 'digits'", row_index);
        return false;
    }
    const int32_t digits = digits_found->get<int32_t>();
    if (digits < 1 || digits > 9) {
        diagnostic = fmt::format(
            "resource manifest row {} field 'digits' must be in [1, 9]", row_index);
        return false;
    }

    ManifestRoot root;
    ManifestVersion version;
    ResourceManifestPrecedence precedence;
    if (!parse_root(root_name, root)) {
        diagnostic = fmt::format(
            "resource manifest row {} has invalid root '{}'", row_index, root_name);
        return false;
    }
    if (!parse_version(row, row_index, version, diagnostic)) { return false; }
    if (!parse_precedence(precedence_name, precedence)) {
        diagnostic = fmt::format(
            "resource manifest row {} has invalid precedence '{}'",
            row_index, precedence_name);
        return false;
    }
    if (!valid_relative_path(file, false, false, row_index, "file", diagnostic)
        || !valid_relative_path(container_directory, true, false, row_index,
            "container_directory", diagnostic)) {
        return false;
    }
    if (!row_enabled(root, version, context)) { return true; }

    const auto& selected_root = root_path(root, context);
    const fs::path ini_path = selected_root / file;
    std::error_code ec;
    if (!fs::exists(ini_path, ec)) { return true; }
    if (ec || !fs::is_regular_file(ini_path, ec)) {
        diagnostic = fmt::format(
            "resource manifest row {} cannot read ini series '{}'",
            row_index, ini_path.string());
        return false;
    }

    Ini ini{ini_path};
    if (!ini.valid()) {
        diagnostic = fmt::format(
            "resource manifest row {} has malformed ini series '{}'",
            row_index, ini_path.string());
        return false;
    }

    for (int32_t index = 0; index < std::numeric_limits<int32_t>::max(); ++index) {
        String container_name;
        const auto key = fmt::format(
            "{}/{}{:0{}}", section, key_prefix, index, digits);
        if (!ini.get_to(key, container_name) || container_name.empty()) {
            return true;
        }
        if (!valid_relative_path(container_name, false, false, row_index,
                "ini container name", diagnostic)) {
            return false;
        }
        if (!append_container({
                                  .directory = selected_root / container_directory,
                                  .name = std::move(container_name),
                                  .precedence = precedence,
                                  .resource_type = ResourceType::invalid,
                              },
                row_index, output, seen, diagnostic)) {
            return false;
        }
    }

    diagnostic = fmt::format(
        "resource manifest row {} ini series exhausted its index range", row_index);
    return false;
}

} // namespace

bool load_resource_manifest(const fs::path& package_directory,
    const ResourceManifestContext& context,
    Vector<ResourceManifestContainer>& output,
    String& diagnostic)
{
    output.clear();
    diagnostic.clear();
    if (context.version != GameVersion::vEE
        && context.version != GameVersion::v1_69) {
        diagnostic = "resource manifest supports only EE and 1.69 game versions";
        return false;
    }

    const fs::path manifest_path = package_directory / "resources.json";
    std::ifstream input{manifest_path};
    if (!input) {
        diagnostic = fmt::format(
            "required resource manifest '{}' is missing", manifest_path.string());
        return false;
    }

    Json document;
    try {
        input >> document;
    } catch (const std::exception& error) {
        diagnostic = fmt::format(
            "failed to parse resource manifest '{}': {}",
            manifest_path.string(), error.what());
        return false;
    }
    if (!document.is_array()) {
        diagnostic = "resource manifest root must be an array";
        return false;
    }

    Vector<ResourceManifestContainer> next;
    std::set<String> declarations;
    std::set<std::pair<fs::path, String>> seen;
    bool has_ini_series = false;
    for (size_t row_index = 0; row_index < document.size(); ++row_index) {
        const auto& row = document[row_index];
        if (!row.is_object()) {
            diagnostic = fmt::format(
                "resource manifest row {} must be an object", row_index);
            return false;
        }

        String kind;
        if (!read_required_string(row, "kind", row_index, kind, diagnostic)) {
            return false;
        }
        if (kind == "container") {
            if (!parse_container_row(row, row_index, context,
                    next, declarations, seen, diagnostic)) {
                return false;
            }
        } else if (kind == "ini_series") {
            if (has_ini_series) {
                diagnostic = "resource manifest contains multiple ini_series rows";
                return false;
            }
            has_ini_series = true;
            if (!parse_ini_series_row(row, row_index, context,
                    next, seen, diagnostic)) {
                return false;
            }
        } else {
            diagnostic = fmt::format(
                "resource manifest row {} has unknown kind '{}'", row_index, kind);
            return false;
        }
    }

    output = std::move(next);
    return true;
}

} // namespace nw
