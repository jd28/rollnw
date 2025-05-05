#include "assets.hpp"

#include "../kernel/Strings.hpp"
#include "../log.hpp"
#include "../util/platform.hpp"
#include "Container.hpp"

#include <nlohmann/json.hpp>

#include <ostream>
#include <tuple>

namespace nw {

// == Resref ==================================================================
// ============================================================================

Resref::Resref()
{
}

Resref::Resref(const char* string) noexcept
    : Resref(StringView(string))
{
}

Resref::Resref(StringView string) noexcept
{
    if (string.empty()) { return; }
    if (string.length() > maximum_size) {
        LOG_F(ERROR, "invalid resref: '{}', resref must be less than {} characters", string, maximum_size);
        return;
    }

    char buffer[maximum_size];
    size_t len = string.length();

    for (size_t i = 0; i < len; ++i) {
        buffer[i] = static_cast<char>(::tolower(static_cast<unsigned char>(string[i])));
    }

    data_ = nw::kernel::strings().intern(StringView(buffer, len));
}

bool Resref::empty() const noexcept { return !data_; }

Resref::size_type Resref::length() const noexcept
{
    return data_.view().length();
}

String Resref::string() const
{
    return String(view());
}

StringView Resref::view() const noexcept { return data_.view(); }

std::ostream& operator<<(std::ostream& out, const Resref& resref)
{
    out << resref.view();
    return out;
}

void from_json(const nlohmann::json& j, Resref& r)
{
    if (j.is_string()) {
        r = Resref{j.get<String>()};
    }
}

void to_json(nlohmann::json& j, const Resref& r)
{
    j = r.view();
}

// == Resource ================================================================
// ============================================================================

Resource::Resource() noexcept
    : resref{}
    , type{ResourceType::invalid}
{
}

Resource::Resource(const Resref& resref_, ResourceType::type type_) noexcept
    : resref{resref_}
    , type{type_}
{
}

Resource::Resource(StringView resref_, ResourceType::type type_) noexcept
    : Resource(Resref(resref_), type_)
{
}

Resource Resource::from_filename(StringView filename)
{
    auto it = filename.find('.');
    if (it != String::npos) {
        return Resource{filename.substr(0, it), ResourceType::from_extension(filename.substr(it))};
    }
    return {};
}

Resource Resource::from_path(const std::filesystem::path& path)
{
    String ext = path_to_string(path.extension());
    String stem = path_to_string(path.stem());
    return {stem, ResourceType::from_extension(ext)};
}

bool Resource::valid() const noexcept
{
    return type != ResourceType::invalid && !resref.empty();
}

String Resource::filename() const
{
    return resref.string() + "." + String(ResourceType::to_string(type));
}

std::ostream& operator<<(std::ostream& out, const Resource& res)
{
    out << res.filename();
    return out;
}

void from_json(const nlohmann::json& j, Resource& r)
{
    const auto& str = j.get<String>();
    r = Resource::from_filename(str);
}

void to_json(nlohmann::json& j, const Resource& r)
{
    j = r.filename();
}

// == ResourceRegistry ========================================================
// ============================================================================

void ResourceRegistry::clear()
{
    entries_.clear();
}

bool ResourceRegistry::contains(Resource uri) const noexcept
{
    return entries_.find(uri) != std::end(entries_);
}

ResourceData ResourceRegistry::demand(Resource uri) const
{
    auto entry = lookup(uri);
    if (!entry) { return {}; }
    return entry->container->demand(entry->key);
}

void ResourceRegistry::insert(Resource uri, Container* container, const ContainerKey* key)
{
    entries_.insert({uri, {container, key}});
}

const ResourceRegistry::Entry* ResourceRegistry::lookup(Resource uri) const
{
    auto it = entries_.find(uri);
    return it != std::end(entries_) ? &it->second : nullptr;
}

void ResourceRegistry::reserve(size_t size)
{
    entries_.reserve(size);
}

size_t ResourceRegistry::size() const noexcept
{
    return entries_.size();
}

void ResourceRegistry::visit(std::function<void(Resource)> visitor) const
{
    for (const auto& [k, _] : entries_) {
        visitor(k);
    }
}

// == ResourceDescriptor ======================================================
// ============================================================================

// == ResourceData ============================================================
// ============================================================================

ResourceData ResourceData::copy() const
{
    ResourceData result;
    result.name = name;
    result.bytes = bytes;
    return result;
}

ResourceData ResourceData::from_file(const std::filesystem::path& path)
{
    ResourceData result;
    result.name = Resource::from_path(path);
    result.bytes = ByteArray::from_file(path);
    return result;
}
} // namespace nw
