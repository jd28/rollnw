#pragma once

#include "../legacy/LocString.hpp"
#include "../util/ByteArray.hpp"
#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace nw {

/// @private
struct ErfElementInfo {
    uint32_t offset;
    uint32_t size;
};

/// @private
using ErfElement = std::variant<ErfElementInfo, std::filesystem::path>;

enum struct ErfType {
    erf,
    hak,
    mod,
    sav
};

enum struct ErfVersion {
    v1_0,
    v1_1
};

struct Erf : public Container {
public:
    Erf() = default;
    explicit Erf(const std::filesystem::path& path);
    Erf(const Erf&) = delete;
    Erf(Erf&& other) = default;
    virtual ~Erf() = default;

    /// Adds resources from array of bytes
    bool add(Resource res, const ByteArray& bytes);

    /// Adds resources from path
    bool add(const std::filesystem::path& path);

    /// Removes resource
    size_t erase(const Resource& res);

    /// Merges the contents of another container
    bool merge(const Container* container);

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

    virtual std::vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return name_; };
    virtual const std::string& path() const override { return path_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return is_loaded_; }
    virtual void visit(std::function<void(const Resource&)> callback) const noexcept override;

    /// Erf type.
    ErfType type = ErfType::erf;
    /// Version
    ErfVersion version = ErfVersion::v1_0;
    /// Description
    LocString description;

    Erf& operator=(const Erf&) = delete;
    Erf& operator=(Erf&&) = default;

private:
    std::string path_;
    std::string name_;
    mutable std::ifstream file_;
    std::streamsize fsize_;
    bool is_loaded_ = false;
    absl::flat_hash_map<Resource, ErfElement> elements_;

    bool load(const std::filesystem::path& path);
    ResourceData read(const ErfElement& element) const;
};

} // namespace nw
