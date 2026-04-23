#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace nw::render {

class ShaderProvider {
public:
    ShaderProvider(nw::gfx::Context* ctx, nw::ResourceManager* resources);
    ~ShaderProvider();

    bool initialize();
    nw::gfx::Handle<nw::gfx::Shader> get_shader(std::string_view name);
    void clear_cache();

private:
    nw::gfx::Handle<nw::gfx::Shader> load_and_compile(const std::string& name);
    nw::gfx::Handle<nw::gfx::Shader> compile_hlsl(
        const std::string& name,
        const std::string& source,
        const wchar_t* target_profile);

    nw::gfx::Context* ctx_ = nullptr;
    nw::ResourceManager* resources_ = nullptr;
    std::unordered_map<std::string, nw::gfx::Handle<nw::gfx::Shader>> shaders_;
    bool dxc_available_ = false;
    void* dxc_compiler_ = nullptr;
    void* dxc_utils_ = nullptr;
};

} // namespace nw::render
