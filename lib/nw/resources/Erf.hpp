#pragma once

#include "../i18n/LocString.hpp"
#include "../util/ByteArray.hpp"
#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <nowide/fstream.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nw {

/// @cond NEVER
struct ErfElementInfo {
    uint32_t offset;
    uint32_t size;
};

struct ErfElement {
    ErfElementInfo info;
};
/// @endcond

enum class ErfType {
    erf,
    hak,
    mod,
    sav
};

enum class ErfVersion {
    v1_0,
};

class Erf : public Container {
public:
    Erf() = default;
    explicit Erf(const std::filesystem::path& path);
    Erf(const Erf&) = delete;
    Erf(Erf&& other) = default;
    ~Erf() = default;

    /// Returns if Key file was successfully loaded
    bool is_loaded() const noexcept { return is_loaded_; }

    ByteArray read(const ErfElement& element);

    virtual std::vector<ResourceDescriptor> all() override;
    virtual ByteArray demand(Resource res) override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) override;
    virtual const std::string& name() const override { return name_; };
    virtual const std::string& path() const override { return path_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) override;
    virtual bool valid() const noexcept override { return is_loaded_; }

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
    nowide::ifstream file_;
    std::streamsize fsize_;
    bool is_loaded_ = false;
    absl::flat_hash_map<Resource, ErfElement> elements_;

    bool load(const std::filesystem::path& path);
};

} // namespace nw
