#pragma once

#include "../i18n/LocString.hpp"
#include "../util/ByteArray.hpp"
#include "Container.hpp"
#include "ErfCommon.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <fstream>

namespace nw {

// == Erf =====================================================================
// ============================================================================

struct Erf final {
public:
    Erf() = default;
    explicit Erf(const std::filesystem::path& path);
    Erf(const Erf&) = delete;
    Erf(Erf&& other) = default;
    virtual ~Erf();

    /// Adds resources from array of bytes
    bool add(Resource res, const ByteArray& bytes);

    /// Adds resources from path
    bool add(const std::filesystem::path& path);

    /// Removes resource
    size_t erase(const Resource& res);

    /// Extracts assets by regex
    int extract(const std::regex& pattern, const std::filesystem::path& output) const;

    /// Merges the contents of another container
    bool merge(const Container* container);

    /// Merges the contents of another container
    bool merge(const Erf& container);

    /// Reloads Erf
    /// @note Erf::working_directory() will not change
    bool reload();

    /// Saves Erf to Erf::path()
    /// @note It's probably best to call Erf::reload after save.
    bool save() const;

    /// Saves Erf to different path
    /// @note Current Erf unmodified, to load Erf at new path a new Erf must
    /// be constructed.
    bool save_as(const std::filesystem::path& path) const;

    ResourceData demand(Resource res) const;
    const String& name() const { return name_; };
    const String& path() const { return path_; };
    size_t size() const;
    bool valid() const noexcept { return is_loaded_; }
    void visit(std::function<void(const Resource&)> callback, std::initializer_list<ResourceType::type> types = {}) const noexcept;

    /// Erf type.
    ErfType type = ErfType::erf;
    /// Version
    ErfVersion version = ErfVersion::v1_0;
    /// Description
    LocString description;

    Erf& operator=(const Erf&) = delete;
    Erf& operator=(Erf&&) = default;

    /// Get container working directory
    const std::filesystem::path& working_directory() const;

private:
    std::filesystem::path working_dir_;
    String path_;
    String name_;
    mutable std::ifstream file_;
    std::streamsize fsize_;
    bool is_loaded_ = false;
    absl::flat_hash_map<Resource, ErfElement> elements_;

    bool load(const std::filesystem::path& path);
    ResourceData read(const ErfElement& element) const;
};

} // namespace nw
