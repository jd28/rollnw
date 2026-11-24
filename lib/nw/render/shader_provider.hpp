#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace nw::render {

/// Pipeline stage a shader entry point is compiled for. The shaders carry no
/// `[shader(...)]` attribute, so the stage has to be supplied explicitly; it
/// is derived from the `.vs.`/`.ps.`/`.cs.` filename infix.
enum class ShaderStage {
    vertex,
    pixel,
    compute,
};

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
        ShaderStage stage);

    nw::gfx::Context* ctx_ = nullptr;
    nw::ResourceManager* resources_ = nullptr;
    std::unordered_map<std::string, nw::gfx::Handle<nw::gfx::Shader>> shaders_;
    bool compiler_available_ = false;
    // slang::IGlobalSession / slang::ISession, type-erased so the Slang
    // headers stay private to shader_provider.cpp.
    void* slang_global_session_ = nullptr;
    void* slang_session_ = nullptr;
    // ResourceFileSystem, owned here and borrowed by the session.
    void* slang_file_system_ = nullptr;
};

} // namespace nw::render
