#include "client_rml_runtime.hpp"

#include <nw/kernel/Kernel.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <SDL3/SDL.h>

#include <cstdio>

namespace nw::toolset {
namespace {

bool load_font(const char* path, const char* family, Rml::Style::FontWeight weight, std::vector<Rml::byte>& storage)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Font not found: %s", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const auto size = std::ftell(f);
    if (size <= 0) {
        std::fclose(f);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Font file has invalid size: %s", path);
        return false;
    }
    std::rewind(f);
    storage.resize(static_cast<size_t>(size));
    const size_t read = std::fread(storage.data(), 1, static_cast<size_t>(size), f);
    std::fclose(f);
    if (read != static_cast<size_t>(size)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read full font file: %s", path);
        storage.clear();
        return false;
    }

    return Rml::LoadFontFace(Rml::Span<const Rml::byte>(storage.data(), storage.size()),
        family, Rml::Style::FontStyle::Normal, weight);
}

struct RequiredFont {
    const char* filename;
    const char* family;
    Rml::Style::FontWeight weight;
};

constexpr std::array required_fonts{
    RequiredFont{"Inter-Regular.ttf", "RollnwSans", Rml::Style::FontWeight::Normal},
    RequiredFont{"Inter-Medium.ttf", "RollnwSans", static_cast<Rml::Style::FontWeight>(500)},
    RequiredFont{"Inter-SemiBold.ttf", "RollnwSans", static_cast<Rml::Style::FontWeight>(600)},
    RequiredFont{"Inter-Bold.ttf", "RollnwSans", Rml::Style::FontWeight::Bold},
    RequiredFont{"Cousine-Regular.ttf", "RollnwMono", Rml::Style::FontWeight::Normal},
};

} // namespace

ClientRmlRuntime::ClientRmlRuntime(const std::filesystem::path& ui_directory, ResourceManager& game_resources)
    : assets_(ui_directory)
    , resources_(nw::kernel::global_allocator())
    , files_(resources_, game_resources)
{
}

ClientRmlRuntime::~ClientRmlRuntime()
{
    shutdown();
}

bool ClientRmlRuntime::initialize(Rml::SystemInterface& system, Rml::RenderInterface& renderer,
    const std::filesystem::path& font_directory, Rml::Vector2i dimensions)
{
    if (initialized_ || dimensions.x <= 0 || dimensions.y <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid or repeated Rml initialization");
        return false;
    }
    if (!assets_.valid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to index rollnw client UI assets: %s", assets_.path().c_str());
        return false;
    }
    if (!resources_.add_custom_container(&assets_, false)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to add rollnw client UI assets to resource manager");
        return false;
    }
    resources_.build_registry();
    const nw::Resource panel_rml{nw::Resref{"ui/panel"}, nw::ResourceType::rml};
    const nw::Resource panel_rcss{nw::Resref{"ui/panel"}, nw::ResourceType::rcss};
    const nw::Resource command_modals_rml{nw::Resref{"ui/command_modals"}, nw::ResourceType::rml};
    if (!resources_.contains(panel_rml) || !resources_.contains(panel_rcss) || !resources_.contains(command_modals_rml)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "rollnw client UI resource package is incomplete");
        return false;
    }

    renderer_ = &renderer;
    Rml::SetRenderInterface(&renderer);
    Rml::SetSystemInterface(&system);
    Rml::SetFileInterface(&files_);
    if (!Rml::Initialise()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::Initialise failed");
        return false;
    }
    initialized_ = true;
    bool fonts_ok = true;
    for (size_t i = 0; i < required_fonts.size(); ++i) {
        const auto& font = required_fonts[i];
        const auto path = (font_directory / font.filename).string();
        fonts_ok = load_font(path.c_str(), font.family, font.weight, fonts_[i]) && fonts_ok;
    }
    if (!fonts_ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load required fonts for RmlUI");
        return false;
    }
    contexts_.toolset = Rml::CreateContext("toolset", dimensions);
    if (!contexts_.toolset) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext failed");
        return false;
    }
    return true;
}

bool ClientRmlRuntime::create_overlay_contexts(Rml::Vector2i dimensions)
{
    if (!initialized_ || !contexts_.toolset || contexts_.fps || contexts_.palette
        || dimensions.x <= 0 || dimensions.y <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid or repeated Rml overlay context creation");
        return false;
    }
    contexts_.fps = Rml::CreateContext("viewer_fps", dimensions);
    if (!contexts_.fps) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext failed: viewer_fps");
        return false;
    }
    contexts_.fps->SetDensityIndependentPixelRatio(contexts_.toolset->GetDensityIndependentPixelRatio());
    contexts_.palette = Rml::CreateContext("command_palette", dimensions);
    if (!contexts_.palette) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext failed: command_palette");
        return false;
    }
    contexts_.palette->SetDensityIndependentPixelRatio(contexts_.toolset->GetDensityIndependentPixelRatio());
    return true;
}

Rml::ElementDocument* ClientRmlRuntime::load_document(Rml::Context& context, Resource resource) const
{
    if (!initialized_) {
        return nullptr;
    }
    auto data = resources_.demand(resource);
    if (!data.bytes.size()) {
        return nullptr;
    }
    Rml::String source{reinterpret_cast<const char*>(data.bytes.data()), data.bytes.size()};
    return context.LoadDocumentFromMemory(source, resource.filename());
}

void ClientRmlRuntime::release_render_resources()
{
    if (initialized_) {
        Rml::ReleaseCompiledGeometry(renderer_);
        Rml::ReleaseTextures(renderer_);
    }
}

void ClientRmlRuntime::shutdown()
{
    if (initialized_) {
        Rml::RemoveContext("command_palette");
        Rml::RemoveContext("viewer_fps");
        Rml::RemoveContext("toolset");
        Rml::Shutdown();
        initialized_ = false;
        contexts_ = {};
        renderer_ = nullptr;
    }
}

} // namespace nw::toolset
