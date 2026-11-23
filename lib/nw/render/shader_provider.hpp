#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nw::render {

using ShaderIncludeResolver = std::function<std::optional<std::string>(std::string_view filename)>;

/// Expands `#include "file"` lines by splicing in resolved content, wrapped in
/// #line directives so compiler diagnostics point at the original files. Each
/// include is spliced at most once per expansion. Returns nullopt if an include
/// cannot be resolved or nesting exceeds the depth limit.
std::optional<std::string> expand_shader_includes(
    std::string_view source,
    std::string_view source_name,
    const ShaderIncludeResolver& resolver);

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
};

} // namespace nw::render
