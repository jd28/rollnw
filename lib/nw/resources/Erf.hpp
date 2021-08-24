#pragma once

#include "../i18n/LocString.hpp"
#include "../util/ByteArray.hpp"
#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <fstream>
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
    /// @name Constructors / Destructor
    /// @{
    explicit Erf(std::filesystem::path path);
    Erf(const Erf&) = delete;
    Erf(Erf&& other) = default;
    ~Erf() = default;
    /// @}

    ByteArray read(const ErfElement& element);

    virtual std::vector<Resource> all() override;
    virtual ByteArray demand(Resource res) override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) override;

    /**
     * @brief Number of resources in the Erf file.
     */
    virtual size_t size() const override;

    /// Erf type.
    ErfType type;
    /// Version
    ErfVersion version;
    /// Description
    LocString description;

    /// @name Operators
    /// @{
    Erf& operator=(const Erf&) = delete;
    Erf& operator=(Erf&&) = default;
    /// @}

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::streamsize fsize_;
    bool is_loaded_ = false;
    absl::flat_hash_map<Resource, ErfElement> elements_;

    bool load();
};

} // namespace nw
