#pragma once

#include "client_rml_file_interface.hpp"

#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/StaticDirectory.hpp>

#include <RmlUi/Core/Types.h>

#include <array>
#include <filesystem>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
class RenderInterface;
class SystemInterface;
}

namespace nw::toolset {

// SDK borrows, valid until shutdown. Primary is acquired before overlays so the
// renderer can perform its existing primary-context resize between these steps.
struct ClientRmlContexts {
    Rml::Context* toolset = nullptr;
    Rml::Context* fps = nullptr;
    Rml::Context* palette = nullptr;
};

// One process UI singleton owns package, file adapter and five font byte arrays.
// External game resources and SDK interfaces must outlive shutdown/destruction.
// Font buffers cannot move while FreeType borrows them. No engine hot path uses
// these required SDK pointers; contexts are always the three fixed roles above.
class ClientRmlRuntime {
public:
    ClientRmlRuntime(const std::filesystem::path& ui_directory, ResourceManager& game_resources);
    ~ClientRmlRuntime();
    ClientRmlRuntime(const ClientRmlRuntime&) = delete;
    ClientRmlRuntime& operator=(const ClientRmlRuntime&) = delete;
    ClientRmlRuntime(ClientRmlRuntime&&) = delete;
    ClientRmlRuntime& operator=(ClientRmlRuntime&&) = delete;

    [[nodiscard]] bool initialize(Rml::SystemInterface& system, Rml::RenderInterface& renderer,
        const std::filesystem::path& font_directory, Rml::Vector2i dimensions);
    [[nodiscard]] bool create_overlay_contexts(Rml::Vector2i dimensions);
    [[nodiscard]] const ClientRmlContexts& contexts() const noexcept { return contexts_; }
    [[nodiscard]] Rml::ElementDocument* load_document(Rml::Context& context, Resource resource) const;
    void release_render_resources();
    void shutdown();

private:
    StaticDirectory assets_;
    ResourceManager resources_;
    ClientRmlFileInterface files_;
    std::array<std::vector<Rml::byte>, 5> fonts_;
    ClientRmlContexts contexts_;
    Rml::RenderInterface* renderer_ = nullptr;
    bool initialized_ = false;
};

} // namespace nw::toolset
