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
