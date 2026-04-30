#include "shader_provider.hpp"

#include <nw/log.hpp>

#include <filesystem>

#ifdef NW_RENDER_HAS_DXC
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
#endif
#if __has_include(<dxc/dxcapi.h>)
#include <dxc/dxcapi.h>
#elif __has_include(<dxcapi.h>)
#include <dxcapi.h>
#else
#error "DXC headers not found"
#endif
#endif

namespace nw::render {

ShaderProvider::ShaderProvider(nw::gfx::Context* ctx, nw::ResourceManager* resources)
    : ctx_(ctx)
    , resources_(resources)
{
}

ShaderProvider::~ShaderProvider()
{
    clear_cache();
#ifdef NW_RENDER_HAS_DXC
    if (dxc_utils_) static_cast<IDxcUtils*>(dxc_utils_)->Release();
    if (dxc_compiler_) static_cast<IDxcCompiler3*>(dxc_compiler_)->Release();
#endif
}

bool ShaderProvider::initialize()
{
    if (!ctx_ || !resources_) {
        LOG_F(ERROR, "shader provider: missing gfx context or resource manager");
        return false;
    }
#ifdef NW_RENDER_HAS_DXC
    IDxcCompiler3* compiler = nullptr;
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        LOG_F(ERROR, "Failed to create DXC compiler");
        return false;
    }
    IDxcUtils* utils = nullptr;
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
        LOG_F(ERROR, "Failed to create DXC utils");
        compiler->Release();
        return false;
    }
    dxc_compiler_ = compiler;
    dxc_utils_ = utils;
    dxc_available_ = true;
    LOG_F(INFO, "DXC available for runtime HLSL compilation");
#else
    LOG_F(WARNING, "DXC not available, shader compilation disabled");
#endif
    return true;
}

nw::gfx::Handle<nw::gfx::Shader> ShaderProvider::get_shader(std::string_view name)
{
    const std::string shader_name{name};
    if (auto it = shaders_.find(shader_name); it != shaders_.end()) {
        return it->second;
    }
    return load_and_compile(shader_name);
}

void ShaderProvider::clear_cache()
{
    for (auto& [name, shader] : shaders_) {
        if (shader.valid()) {
            nw::gfx::destroy_shader(ctx_, shader);
        }
    }
    shaders_.clear();
}

nw::gfx::Handle<nw::gfx::Shader> ShaderProvider::load_and_compile(const std::string& name)
{
    const bool is_vertex = (name.find(".vs.") != std::string::npos);
    const bool is_pixel = (name.find(".ps.") != std::string::npos);
    const bool is_compute = (name.find(".cs.") != std::string::npos);

    if (!is_vertex && !is_pixel && !is_compute) {
        LOG_F(ERROR, "Cannot determine shader type for: {}", name);
        return {};
    }

    const auto resource = nw::Resource::from_path(std::filesystem::path{name});
    if (!resource.valid() || resource.type != nw::ResourceType::hlsl) {
        LOG_F(ERROR, "Invalid shader resource name: {}", name);
        return {};
    }

    auto data = resources_->demand(resource);
    if (!data.bytes.size()) {
        LOG_F(ERROR, "Failed to load shader resource: {}", name);
        return {};
    }

    const auto source_view = data.bytes.string_view();
    const std::string source{source_view.data(), source_view.size()};
    const wchar_t* profile = is_vertex ? L"vs_6_0" : (is_pixel ? L"ps_6_0" : L"cs_6_0");
    return compile_hlsl(name, source, profile);
}

nw::gfx::Handle<nw::gfx::Shader> ShaderProvider::compile_hlsl(
    const std::string& name,
    const std::string& source,
    const wchar_t* target_profile)
{
#ifdef NW_RENDER_HAS_DXC
    if (!dxc_available_) {
        LOG_F(ERROR, "DXC not available");
        return {};
    }

    auto* compiler = static_cast<IDxcCompiler3*>(dxc_compiler_);

    DxcBuffer source_buffer{};
    source_buffer.Ptr = source.c_str();
    source_buffer.Size = source.size();
    source_buffer.Encoding = DXC_CP_UTF8;

    std::vector<LPCWSTR> args;
    args.push_back(L"-E");
    args.push_back(L"main");
    args.push_back(L"-T");
    args.push_back(target_profile);
    args.push_back(L"-spirv");
    args.push_back(L"-fspv-target-env=vulkan1.3");
    args.push_back(L"-O3");

    IDxcResult* result = nullptr;
    const HRESULT hr = compiler->Compile(
        &source_buffer,
        args.data(),
        static_cast<UINT32>(args.size()),
        nullptr,
        IID_PPV_ARGS(&result));

    if (FAILED(hr)) {
        LOG_F(ERROR, "DXC compilation failed for: {}", name);
        return {};
    }

    HRESULT status;
    result->GetStatus(&status);
    if (FAILED(status)) {
        IDxcBlobUtf8* errors = nullptr;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            LOG_F(ERROR, "Shader compilation errors in {}:\n{}", name, errors->GetStringPointer());
        }
        if (errors) errors->Release();
        result->Release();
        return {};
    }

    IDxcBlob* shader_blob = nullptr;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_blob), nullptr);
    if (!shader_blob) {
        LOG_F(ERROR, "Failed to get compiled shader blob for: {}", name);
        result->Release();
        return {};
    }

    nw::gfx::ShaderDesc desc{};
    desc.code = shader_blob->GetBufferPointer();
    desc.size = shader_blob->GetBufferSize();

    const nw::gfx::Handle<nw::gfx::Shader> shader = nw::gfx::create_shader(ctx_, desc);

    shader_blob->Release();
    result->Release();

    if (shader.valid()) {
        LOG_F(INFO, "Successfully compiled shader: {}", name);
        shaders_[name] = shader;
    } else {
        LOG_F(ERROR, "Failed to create gfx shader for: {}", name);
    }

    return shader;
#else
    LOG_F(ERROR, "Cannot compile HLSL: DXC not available");
    (void)name;
    (void)source;
    (void)target_profile;
    return {};
#endif
}

} // namespace nw::render
