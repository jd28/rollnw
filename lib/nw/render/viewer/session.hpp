#pragma once

#include "camera.hpp"
#include "preview_scene.hpp"
#include "renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace nw::render::viewer {

struct ViewerViewport {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

enum class ViewerSceneKind : uint8_t {
    none,
    model,
    area,
    object_file,
};

class ViewerSession {
public:
    explicit ViewerSession(Renderer& renderer);
    ~ViewerSession();

    ViewerSession(const ViewerSession&) = delete;
    ViewerSession& operator=(const ViewerSession&) = delete;

    bool load_model(std::string_view resref);
    bool load_area(std::string_view resref);
    bool load_object_file(const std::filesystem::path& path);
    void clear();

    void tick(int32_t dt_ms);
    void render(nw::gfx::CommandList* command_list, ViewerViewport viewport);

    bool select_animation(std::string_view animation);
    bool fit_to_scene(ViewerViewport viewport);
    void update_viewport(ViewerViewport viewport);

    [[nodiscard]] Camera& camera() noexcept { return camera_; }
    [[nodiscard]] const Camera& camera() const noexcept { return camera_; }
    [[nodiscard]] PreviewScene* scene() noexcept { return scene_.get(); }
    [[nodiscard]] const PreviewScene* scene() const noexcept { return scene_.get(); }
    [[nodiscard]] ViewerSceneKind scene_kind() const noexcept { return scene_kind_; }
    [[nodiscard]] std::string_view loaded_source() const noexcept { return loaded_source_; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    void set_playing(bool playing) noexcept { playing_ = playing; }

private:
    bool set_scene(std::unique_ptr<PreviewScene> scene, ViewerSceneKind kind, std::string source);
    void fit_loaded_scene();
    [[nodiscard]] nw::render::RenderContext make_render_context() const;

    Renderer* renderer_ = nullptr;
    Camera camera_;
    std::unique_ptr<PreviewScene> scene_;
    ViewerViewport viewport_;
    ViewerSceneKind scene_kind_ = ViewerSceneKind::none;
    std::string loaded_source_;
    bool playing_ = true;
};

} // namespace nw::render::viewer
