#pragma once

#include "renderer.hpp"
#include "session.hpp"

#include <nw/render/shader_provider.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace nw {
class ResourceManager;
}

namespace nw::render::viewer {

struct ViewerDeviceOptions {
    std::vector<std::filesystem::path> shader_roots;
};

class ViewerDevice {
public:
    ViewerDevice(nw::gfx::Context* context, nw::ResourceManager& resman);
    ~ViewerDevice();

    ViewerDevice(const ViewerDevice&) = delete;
    ViewerDevice& operator=(const ViewerDevice&) = delete;

    bool initialize(const ViewerDeviceOptions& options = {});
    void shutdown();
    bool reload_shaders();

    [[nodiscard]] bool initialized() const noexcept { return renderer_ != nullptr; }
    [[nodiscard]] Renderer* renderer() noexcept { return renderer_.get(); }
    [[nodiscard]] const Renderer* renderer() const noexcept { return renderer_.get(); }
    [[nodiscard]] nw::render::ShaderProvider* shader_provider() noexcept { return shader_provider_.get(); }
    [[nodiscard]] const nw::render::ShaderProvider* shader_provider() const noexcept { return shader_provider_.get(); }

    std::unique_ptr<ViewerSession> make_session();

private:
    bool register_shader_roots(const std::vector<std::filesystem::path>& shader_roots);

    nw::gfx::Context* context_ = nullptr;
    nw::ResourceManager* resman_ = nullptr;
    std::unique_ptr<nw::render::ShaderProvider> shader_provider_;
    std::unique_ptr<Renderer> renderer_;
};

} // namespace nw::render::viewer
