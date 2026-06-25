#include "device.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/render/render_service.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/assets.hpp>

namespace nw::render::viewer {

namespace {

nw::render::RenderService* ensure_render_service()
{
    auto* service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!service) {
        service = nw::kernel::services().add<nw::render::RenderService>();
    }
    return service;
}

} // namespace

ViewerDevice::ViewerDevice(nw::gfx::Context* context, nw::ResourceManager& resman)
    : context_(context)
    , resman_(&resman)
{
}

ViewerDevice::~ViewerDevice()
{
    shutdown();
}

bool ViewerDevice::initialize(const ViewerDeviceOptions& options)
{
    if (preview_resources_) {
        return true;
    }
    if (!context_ || !resman_) {
        LOG_F(ERROR, "Viewer device: missing graphics context or resource manager");
        return false;
    }

    nw::kernel::services().create();
    if (!register_shader_roots(options.shader_roots)) {
        return false;
    }

    if (!shader_provider_) {
        shader_provider_ = std::make_unique<nw::render::ShaderProvider>(context_, resman_);
        if (!shader_provider_->initialize()) {
            shader_provider_.reset();
            LOG_F(ERROR, "Viewer device: failed to initialize shader provider");
            return false;
        }
    }

    auto* service = ensure_render_service();
    service->configure(context_, shader_provider_.get());

    preview_resources_ = std::make_unique<PreviewRenderResources>(context_);
    if (!preview_resources_->initialize(shader_provider_.get())) {
        preview_resources_.reset();
        LOG_F(ERROR, "Viewer device: failed to initialize preview render resources");
        return false;
    }
    debug_renderer_ = std::make_unique<SceneDebugRenderer>(context_);
    if (!debug_renderer_->initialize(*shader_provider_)) {
        debug_renderer_.reset();
        preview_resources_.reset();
        LOG_F(ERROR, "Viewer device: failed to initialize scene debug renderer");
        return false;
    }

    return true;
}

void ViewerDevice::shutdown()
{
    if (context_) {
        nw::gfx::wait_idle(context_);
    }

    debug_renderer_.reset();
    preview_resources_.reset();
    if (auto* service = nw::kernel::services().get_mut<nw::render::RenderService>()) {
        service->shutdown_renderer();
    }
    shader_provider_.reset();
}

bool ViewerDevice::reload_shaders()
{
    if (!context_ || !shader_provider_) {
        LOG_F(ERROR, "Viewer device: cannot reload shaders before initialization");
        return false;
    }

    auto* service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!service) {
        LOG_F(ERROR, "Viewer device: render service is not registered");
        return false;
    }

    nw::gfx::wait_idle(context_);
    debug_renderer_.reset();
    preview_resources_.reset();

    if (!service->reload_shaders()) {
        LOG_F(ERROR, "Viewer device: failed to reload render service shaders");
        return false;
    }

    preview_resources_ = std::make_unique<PreviewRenderResources>(context_);
    if (!preview_resources_->initialize(shader_provider_.get())) {
        preview_resources_.reset();
        LOG_F(ERROR, "Viewer device: failed to rebuild preview render resources after shader reload");
        return false;
    }
    debug_renderer_ = std::make_unique<SceneDebugRenderer>(context_);
    if (!debug_renderer_->initialize(*shader_provider_)) {
        debug_renderer_.reset();
        preview_resources_.reset();
        LOG_F(ERROR, "Viewer device: failed to rebuild scene debug renderer after shader reload");
        return false;
    }

    return true;
}

std::unique_ptr<ViewerSession> ViewerDevice::make_session()
{
    if (!preview_resources_) {
        return {};
    }
    return std::make_unique<ViewerSession>(*preview_resources_, debug_renderer_.get());
}

bool ViewerDevice::register_shader_roots(const std::vector<std::filesystem::path>& shader_roots)
{
    if (!resman_ || shader_roots.empty()) {
        return true;
    }

    if (resman_->is_frozen()) {
        resman_->unfreeze();
    }

    bool registered = false;
    for (const auto& shader_root : shader_roots) {
        if (!std::filesystem::is_directory(shader_root)) {
            continue;
        }
        resman_->add_base_container(shader_root.parent_path(), shader_root.filename().string(), nw::ResourceType::hlsl);
        registered = true;
    }

    if (registered && !resman_->is_frozen()) {
        resman_->build_registry();
    }
    return true;
}

} // namespace nw::render::viewer
