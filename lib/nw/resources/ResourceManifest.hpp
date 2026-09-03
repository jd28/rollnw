#pragma once

#include "assets.hpp"

#include "../config.hpp"
#include "../i18n/Language.hpp"
#include "../util/game_install.hpp"

#include <filesystem>

namespace nw {

enum class ResourceManifestPrecedence : uint8_t {
    base,
    override,
};

/// One validated, expanded container declaration. The batch returned by
/// load_resource_manifest owns every path and name for the caller's service
/// generation. Missing containers are intentionally not resolved here.
struct ResourceManifestContainer {
    std::filesystem::path directory;
    String name;
    ResourceManifestPrecedence precedence = ResourceManifestPrecedence::base;
    ResourceType::type resource_type = ResourceType::invalid;
};

struct ResourceManifestContext {
    std::filesystem::path install_root;
    std::filesystem::path user_root;
    GameVersion version = GameVersion::invalid;
    LanguageID language = LanguageID::english;
    bool include_install = true;
    bool include_user = true;
};

/// Validates and expands one selected package's resources.json into an ordered
/// batch. On failure output is empty and diagnostic describes the rejected
/// boundary input.
bool load_resource_manifest(const std::filesystem::path& package_directory,
    const ResourceManifestContext& context,
    Vector<ResourceManifestContainer>& output,
    String& diagnostic);

} // namespace nw
