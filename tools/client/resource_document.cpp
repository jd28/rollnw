#include "resource_document.hpp"

#include "project.hpp"

#include <nw/resources/assets.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace nw::toolset {

namespace {

ResourceDocument failure(std::string message)
{
    ResourceDocument document;
    document.message = std::move(message);
    document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::error, document.message});
    return document;
}

void remove_temporary_file(const fs::path& path)
{
    std::error_code ignored;
    fs::remove(path, ignored);
}

bool relative_path_escapes_root(const fs::path& relative_path)
{
    for (const auto& part : relative_path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
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

std::string format_file_size(uintmax_t size)
{
    constexpr uintmax_t kb = 1024;
    constexpr uintmax_t mb = kb * 1024;
    if (size >= mb) {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(1);
        out << static_cast<double>(size) / static_cast<double>(mb) << " MiB";
        return out.str();
    }
    if (size >= kb) {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(1);
        out << static_cast<double>(size) / static_cast<double>(kb) << " KiB";
        return out.str();
    }
    return std::to_string(size) + " bytes";
}

void add_property(ResourceDocument& document, std::string group, std::string name, std::string value)
{
    if (value.empty()) {
        return;
    }
    document.properties.push_back({std::move(group), std::move(name), std::move(value)});
}

void add_property(ResourceDocument& document, std::string group, std::string name, uintmax_t value)
{
    add_property(document, std::move(group), std::move(name), std::to_string(value));
}

std::string bool_label(bool value)
{
    return value ? "yes" : "no";
}

std::string json_scalar_value(const nlohmann::json& value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        std::ostringstream out;
        out << value.get<double>();
        return out.str();
    }
    if (value.is_null()) {
        return "null";
    }
    return {};
}

std::string locstring_value(const nlohmann::json& value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (!value.is_object()) {
        return {};
    }

    const auto strings = value.find("strings");
    if (strings != value.end() && strings->is_array()) {
        for (const auto& entry : *strings) {
            if (!entry.is_object()) {
                continue;
            }
            const auto text = entry.find("string");
            if (text != entry.end() && text->is_string() && !text->get<std::string>().empty()) {
                return text->get<std::string>();
            }
        }
    }

    const auto strref = value.find("strref");
    if (strref != value.end() && strref->is_number_unsigned()) {
        const auto id = strref->get<unsigned long long>();
        if (id != 4294967295ULL) {
            return "StrRef " + std::to_string(id);
        }
    }
    if (strref != value.end() && strref->is_number_integer()) {
        const auto id = strref->get<long long>();
        if (id >= 0) {
            return "StrRef " + std::to_string(id);
        }
    }
    return {};
}

std::string json_path_value(const nlohmann::json& json, std::initializer_list<std::string_view> keys)
{
    const nlohmann::json* current = &json;
    for (const std::string_view key : keys) {
        if (!current->is_object()) {
            return {};
        }
        const auto it = current->find(std::string{key});
        if (it == current->end()) {
            return {};
        }
        current = &*it;
    }

    if (auto localized = locstring_value(*current); !localized.empty()) {
        return localized;
    }
    return json_scalar_value(*current);
}

size_t json_array_size(const nlohmann::json& json, std::initializer_list<std::string_view> keys)
{
    const nlohmann::json* current = &json;
    for (const std::string_view key : keys) {
        if (!current->is_object()) {
            return 0;
        }
        const auto it = current->find(std::string{key});
        if (it == current->end()) {
            return 0;
        }
        current = &*it;
    }
    return current->is_array() ? current->size() : 0;
}

std::string json_string_array_value(const nlohmann::json& json, std::initializer_list<std::string_view> keys)
{
    const nlohmann::json* current = &json;
    for (const std::string_view key : keys) {
        if (!current->is_object()) {
            return {};
        }
        const auto it = current->find(std::string{key});
        if (it == current->end()) {
            return {};
        }
        current = &*it;
    }
    if (!current->is_array()) {
        return {};
    }

    std::string out;
    for (const auto& entry : *current) {
        const std::string value = json_scalar_value(entry);
        if (value.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += value;
    }
    return out;
}

std::string format_from_path(const fs::path& path, bool is_directory)
{
    if (is_directory) {
        return "Directory";
    }

    const std::string extension = to_lower_ascii(path.extension().generic_string());
    if (extension == ".json") {
        return "JSON";
    }
    if (extension == ".nss" || extension == ".smalls" || extension == ".txt" || extension == ".ini" || extension == ".2da") {
        return "Text";
    }
    if (extension.empty()) {
        return "File";
    }
    std::string out = extension.substr(1);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return out;
}

std::string json_type_label(const nlohmann::json& json)
{
    if (const auto value = json_path_value(json, {"$type"}); !value.empty()) {
        return value;
    }
    return {};
}

void add_resource_identity(ResourceDocument& document)
{
    add_property(document, "Resource", "Path", document.detail);
    add_property(document, "Resource", "Title", document.title);
    add_property(document, "Resource", "Kind", resource_document_kind_label(document.kind));
    add_property(document, "Resource", "Format", document.format);
    add_property(document, "Resource", "Type", document.resource_type);
    add_property(document, "Resource", "Size", format_file_size(document.file_size));
    add_property(document, "Resource", "Previewable", bool_label(document.previewable));
    add_property(document, "Resource", "Area", bool_label(document.area));
}

void add_json_summary(ResourceDocument& document, const nlohmann::json& json)
{
    add_property(document, "JSON", "$type", json_type_label(json));
    add_property(document, "JSON", "$version", json_path_value(json, {"$version"}));

    add_property(document, "Common", "ResRef", json_path_value(json, {"common", "resref"}));
    add_property(document, "Common", "Tag", json_path_value(json, {"common", "tag"}));
    add_property(document, "Common", "Object Type", json_path_value(json, {"common", "object_type"}));
    add_property(document, "Common", "Comment", json_path_value(json, {"common", "comment"}));

    add_property(document, "Names", "First Name", json_path_value(json, {"name_first"}));
    add_property(document, "Names", "Last Name", json_path_value(json, {"name_last"}));
    add_property(document, "Names", "Localized Name", json_path_value(json, {"localized_name"}));
    add_property(document, "Names", "Name", json_path_value(json, {"name"}));
    add_property(document, "Names", "Description", json_path_value(json, {"description"}));

    const std::string type = to_lower_ascii(json_type_label(json));
    if (type == "utc") {
        add_property(document, "Creature", "CR", json_path_value(json, {"cr"}));
        add_property(document, "Creature", "Race", json_path_value(json, {"race"}));
        add_property(document, "Creature", "Gender", json_path_value(json, {"gender"}));
        add_property(document, "Creature", "Faction", json_path_value(json, {"faction_id"}));
        add_property(document, "Creature", "Conversation", json_path_value(json, {"conversation"}));
        add_property(document, "Appearance", "Appearance", json_path_value(json, {"appearance", "id"}));
        add_property(document, "Appearance", "Phenotype", json_path_value(json, {"appearance", "phenotype"}));
        add_property(document, "Appearance", "Head", json_path_value(json, {"appearance", "body_parts", "head"}));
        const uintmax_t equipped_slots = json.is_object() && json.contains("equipment") && json["equipment"].is_object()
            ? json["equipment"].size()
            : uintmax_t{0};
        add_property(document, "Inventory", "Equipped Slots", equipped_slots);
        add_property(document, "Inventory", "Inventory Items", json_array_size(json, {"inventory"}));
    } else if (type == "uti") {
        add_property(document, "Item", "Base Item", json_path_value(json, {"baseitem"}));
        add_property(document, "Item", "Tag", json_path_value(json, {"common", "tag"}));
        add_property(document, "Item", "Charges", json_path_value(json, {"charges"}));
        add_property(document, "Item", "Cost", json_path_value(json, {"cost"}));
    } else if (type == "utd" || type == "utp") {
        add_property(document, type == "utd" ? "Door" : "Placeable", "Appearance", json_path_value(json, {"appearance"}));
        add_property(document, type == "utd" ? "Door" : "Placeable", "Faction", json_path_value(json, {"faction_id"}));
        add_property(document, type == "utd" ? "Door" : "Placeable", "Conversation", json_path_value(json, {"conversation"}));
        add_property(document, type == "utd" ? "Door" : "Placeable", "Plot", json_path_value(json, {"plot"}));
    } else if (type == "ifo") {
        add_property(document, "Module", "Haks", json_string_array_value(json, {"haks"}));
        add_property(document, "Module", "Custom TLK", json_path_value(json, {"tlk"}));
        add_property(document, "Module", "Areas", json_array_size(json, {"areas"}));
    }
}

void read_json_document(ResourceDocument& document)
{
    std::ifstream input{document.absolute_path};
    if (!input) {
        document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::error,
            "Failed to open JSON resource: " + document.absolute_path.string()});
        return;
    }

    try {
        nlohmann::json json;
        input >> json;
        if (!json.is_object()) {
            document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::warning,
                "JSON root is not an object; summary fields are limited."});
        }
        add_json_summary(document, json);
    } catch (const std::exception& e) {
        document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::error,
            std::string{"Failed to parse JSON: "} + e.what()});
    }
}

void read_text_document(ResourceDocument& document)
{
    std::ifstream input{document.absolute_path};
    if (!input) {
        document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::error,
            "Failed to open text resource: " + document.absolute_path.string()});
        return;
    }

    size_t lines = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++lines;
    }
    add_property(document, "Text", "Lines", static_cast<uintmax_t>(lines));
}

ResourceDocumentKind classify_resource_document(bool is_directory,
    const fs::path& relative_path,
    bool area,
    bool previewable,
    const nw::Resource& resource)
{
    if (is_directory) {
        return ResourceDocumentKind::folder;
    }
    if (area) {
        return ResourceDocumentKind::area;
    }
    if (previewable) {
        return ResourceDocumentKind::preview;
    }
    if (resource.valid()) {
        return ResourceDocumentKind::resource;
    }
    return relative_path.has_filename() ? ResourceDocumentKind::file : ResourceDocumentKind::unknown;
}

} // namespace

bool save_json_resource_document_atomic(const fs::path& target,
    const nlohmann::json& value,
    std::string& error)
{
    error.clear();
    if (target.empty() || target.filename().empty()) {
        error = "Resource document path is empty";
        return false;
    }

    std::error_code ec;
    if (!fs::is_regular_file(target, ec)) {
        error = ec ? "Failed to inspect resource document " + target.string() + ": " + ec.message()
                   : "Resource document does not exist: " + target.string();
        return false;
    }

    fs::path temporary = target;
    temporary += ".rollnw-client-save.tmp";
    remove_temporary_file(temporary);

    std::string serialized;
    try {
        serialized = value.dump(2);
    } catch (const std::exception& e) {
        error = "Failed to serialize resource document " + target.string() + ": " + e.what();
        return false;
    }

    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            error = "Failed to open temporary resource document " + temporary.string();
            return false;
        }
        output << serialized << '\n';
        output.flush();
        if (!output) {
            error = "Failed to write temporary resource document " + temporary.string();
            output.close();
            remove_temporary_file(temporary);
            return false;
        }
    }

    const auto permissions = fs::status(target, ec).permissions();
    if (!ec) {
        fs::permissions(temporary, permissions, ec);
        if (ec) {
            error = "Failed to preserve resource document permissions: " + ec.message();
            remove_temporary_file(temporary);
            return false;
        }
    }

    fs::rename(temporary, target, ec);
    if (ec) {
        error = "Failed to replace resource document " + target.string() + ": " + ec.message();
        remove_temporary_file(temporary);
        return false;
    }
    return true;
}

std::string resource_document_kind_label(ResourceDocumentKind kind)
{
    switch (kind) {
    case ResourceDocumentKind::folder:
        return "Folder";
    case ResourceDocumentKind::file:
        return "File";
    case ResourceDocumentKind::resource:
        return "Resource";
    case ResourceDocumentKind::area:
        return "Area";
    case ResourceDocumentKind::preview:
        return "Preview";
    case ResourceDocumentKind::unknown:
        break;
    }
    return "Unknown";
}

std::string resource_document_diagnostic_severity_label(ResourceDocumentDiagnosticSeverity severity)
{
    switch (severity) {
    case ResourceDocumentDiagnosticSeverity::info:
        return "Info";
    case ResourceDocumentDiagnosticSeverity::warning:
        return "Warning";
    case ResourceDocumentDiagnosticSeverity::error:
        return "Error";
    }
    return "Info";
}

ResourceDocument load_project_resource_document(const fs::path& project_dir, const fs::path& requested_relative_path)
{
    if (project_dir.empty()) {
        return failure("No project is open");
    }
    if (!is_project_directory(project_dir)) {
        return failure("Not an Client project: " + project_dir.string());
    }
    if (requested_relative_path.empty()) {
        return failure("Resource path required");
    }

    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(project_dir, ec);
    if (ec) {
        return failure("Failed to resolve project path: " + project_dir.string());
    }

    const fs::path target_path = requested_relative_path.is_absolute()
        ? requested_relative_path
        : canonical_root / requested_relative_path;
    if (!fs::exists(target_path, ec)) {
        return failure("Resource does not exist: " + target_path.string());
    }

    const fs::path canonical_target = fs::weakly_canonical(target_path, ec);
    if (ec) {
        return failure("Failed to resolve resource path: " + target_path.string());
    }

    fs::path relative = fs::relative(canonical_target, canonical_root, ec);
    if (ec || relative.empty() || relative_path_escapes_root(relative)) {
        return failure("Resource is outside the current project: " + target_path.string());
    }

    const bool directory = fs::is_directory(canonical_target, ec);
    const nw::Resource resource = nw::Resource::from_path(relative, false);
    const bool area = project_resource_is_area(relative);
    const bool previewable = project_resource_is_preview_blueprint(relative);

    ResourceDocument document;
    document.ok = true;
    document.project_dir = canonical_root;
    document.relative_path = relative;
    document.absolute_path = canonical_target;
    document.detail = relative.generic_string();
    document.title = project_resource_display_name(canonical_root, relative);
    document.area = area;
    document.previewable = previewable;
    document.kind = classify_resource_document(directory, relative, area, previewable, resource);
    document.format = format_from_path(relative, directory);
    if (resource.valid()) {
        document.resource_type = std::string{nw::ResourceType::to_string(resource.type)};
    }
    if (!directory) {
        document.file_size = fs::file_size(canonical_target, ec);
        if (ec) {
            document.file_size = 0;
            document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::warning,
                "Failed to read file size: " + ec.message()});
        }
    }

    add_resource_identity(document);

    if (directory) {
        uintmax_t child_count = 0;
        for (const auto& entry : fs::directory_iterator{canonical_target, ec}) {
            if (ec) {
                break;
            }
            static_cast<void>(entry);
            ++child_count;
        }
        if (ec) {
            document.diagnostics.push_back({ResourceDocumentDiagnosticSeverity::warning,
                "Failed to count folder entries: " + ec.message()});
        } else {
            add_property(document, "Folder", "Entries", child_count);
        }
    } else if (document.format == "JSON") {
        read_json_document(document);
    } else if (document.format == "Text") {
        read_text_document(document);
    }

    document.message = "Loaded resource document: " + document.detail;
    return document;
}

} // namespace nw::toolset
