#include "shader_provider.hpp"

#include <nw/log.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>

#ifdef NW_RENDER_HAS_SLANG
#include <slang-com-ptr.h>
#include <slang.h>
#endif

namespace nw::render {

namespace {

#ifndef NW_GFX_BACKEND_METAL
// See ShaderProvider::initialize for why the SPIR-V profile is pinned.
constexpr const char* kSpirvProfile = "spirv_1_6";
#endif

#ifdef NW_RENDER_HAS_SLANG
SlangStage slang_stage_for(ShaderStage stage) noexcept
{
    switch (stage) {
    case ShaderStage::vertex:
        return SLANG_STAGE_VERTEX;
    case ShaderStage::pixel:
        return SLANG_STAGE_FRAGMENT;
    case ShaderStage::compute:
        return SLANG_STAGE_COMPUTE;
    }
    return SLANG_STAGE_NONE;
}

bool same_guid(const SlangUUID& lhs, const SlangUUID& rhs) noexcept
{
    return std::memcmp(&lhs, &rhs, sizeof(SlangUUID)) == 0;
}

// Slang's COM interfaces deliberately declare non-virtual destructors, as COM
// requires; both implementations below are only ever destroyed through their
// own static type. The warning is about the vendored bases, not this code.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

// Slang releases the blobs it is handed, so this owns a copy of the bytes
// rather than borrowing the ResourceManager's.
class SourceBlob : public ISlangBlob {
public:
    explicit SourceBlob(std::string source)
        : source_(std::move(source))
    {
    }

    // The COM bases declare non-virtual destructors; both of these types are
    // only ever deleted through their own static type, and saying so keeps
    // -Wdelete-non-virtual-dtor honest about it.
    virtual ~SourceBlob() = default;

    SLANG_IUNKNOWN_ALL

    SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() override { return source_.data(); }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return source_.size(); }

private:
    ISlangUnknown* getInterface(const SlangUUID& uuid)
    {
        if (same_guid(uuid, ISlangUnknown::getTypeGuid())
            || same_guid(uuid, ISlangBlob::getTypeGuid())) {
            return static_cast<ISlangBlob*>(this);
        }
        return nullptr;
    }

    std::string source_;
    std::atomic<uint32_t> m_refCount{1};
};

// Resolves shader #include through ResourceManager instead of the real
// filesystem, so a module or hak can override a shader the same way it
// overrides any other resource. This is the reason the provider supplies a
// file system at all -- a stock filesystem-backed compiler cannot see those.
class ResourceFileSystem : public ISlangFileSystem {
public:
    explicit ResourceFileSystem(nw::ResourceManager* resources)
        : resources_(resources)
    {
    }

    virtual ~ResourceFileSystem() = default;

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(
        SlangUUID const& uuid, void** outObject) override
    {
        if (void* intf = castAs(uuid)) {
            *outObject = intf;
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    // ShaderProvider owns this and releases the session before destroying it,
    // so the reference count is deliberately inert.
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override
    {
        if (same_guid(guid, ISlangUnknown::getTypeGuid())
            || same_guid(guid, ISlangCastable::getTypeGuid())
            || same_guid(guid, ISlangFileSystem::getTypeGuid())) {
            return static_cast<ISlangFileSystem*>(this);
        }
        return nullptr;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(
        char const* path, ISlangBlob** outBlob) override
    {
        if (!path || !outBlob) { return SLANG_E_INVALID_ARG; }

        // Resource names are a flat namespace, so match on the filename even
        // if Slang hands back a combined path.
        const auto filename = std::filesystem::path{path}.filename();
        const auto resource = nw::Resource::from_path(filename);
        if (!resource.valid() || resource.type != nw::ResourceType::hlsl) {
            return SLANG_E_NOT_FOUND;
        }

        auto data = resources_->demand(resource);
        if (!data.bytes.size()) { return SLANG_E_NOT_FOUND; }

        const auto view = data.bytes.string_view();
        *outBlob = new SourceBlob(std::string{view.data(), view.size()});
        return SLANG_OK;
    }

private:
    nw::ResourceManager* resources_ = nullptr;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef NW_GFX_BACKEND_METAL
// Convert Vulkan NDC clip-space Y (-1 top, +1 bottom) to Metal NDC (+1 top, -1 bottom)
// inside the [[vertex]] entry point so all projection matrices, full-screen quads,
// shadow maps, and framebuffer winding rules remain identical across backends.
std::string invert_metal_vertex_y(std::string_view msl)
{
    // Locate `float4 <pos_field> [[position]];`
    const size_t pos_attr = msl.find("[[position]]");
    if (pos_attr == std::string_view::npos) {
        return std::string{msl};
    }
    size_t name_end = pos_attr;
    while (name_end > 0 && (msl[name_end - 1] == ' ' || msl[name_end - 1] == '\t')) {
        --name_end;
    }
    size_t name_start = name_end;
    while (name_start > 0 && (std::isalnum(static_cast<unsigned char>(msl[name_start - 1])) || msl[name_start - 1] == '_')) {
        --name_start;
    }
    if (name_start == name_end) {
        return std::string{msl};
    }
    const std::string_view pos_field = msl.substr(name_start, name_end - name_start);

    // Locate `[[vertex]]` entry point and its brace-delimited body.
    const size_t vs_pos = msl.find("[[vertex]]");
    if (vs_pos == std::string_view::npos) {
        return std::string{msl};
    }
    const size_t body_open = msl.find('{', vs_pos);
    if (body_open == std::string_view::npos) {
        return std::string{msl};
    }
    size_t depth = 0;
    size_t body_close = std::string_view::npos;
    for (size_t i = body_open; i < msl.size(); ++i) {
        if (msl[i] == '{') {
            ++depth;
        } else if (msl[i] == '}') {
            if (--depth == 0) {
                body_close = i;
                break;
            }
        }
    }
    if (body_close == std::string_view::npos) {
        return std::string{msl};
    }

    std::string out;
    out.reserve(msl.size() + 128);
    out.append(msl.substr(0, body_open + 1));

    const std::string_view body = msl.substr(body_open + 1, body_close - (body_open + 1));
    size_t cursor = 0;
    while (cursor < body.size()) {
        const size_t ret_pos = body.find("return ", cursor);
        if (ret_pos == std::string_view::npos) {
            out.append(body.substr(cursor));
            break;
        }
        // Ensure `return` is a standalone token, not part of an identifier.
        if (ret_pos > 0 && (std::isalnum(static_cast<unsigned char>(body[ret_pos - 1])) || body[ret_pos - 1] == '_')) {
            out.append(body.substr(cursor, (ret_pos + 7) - cursor));
            cursor = ret_pos + 7;
            continue;
        }
        const size_t semi = body.find(';', ret_pos + 7);
        if (semi == std::string_view::npos) {
            out.append(body.substr(cursor));
            break;
        }
        const std::string_view expr = body.substr(ret_pos + 7, semi - (ret_pos + 7));
        out.append(body.substr(cursor, ret_pos - cursor));
        out.append("{ auto _nw_vs_out = (");
        out.append(expr);
        out.append("); _nw_vs_out.");
        out.append(pos_field);
        out.append(".y = -_nw_vs_out.");
        out.append(pos_field);
        out.append(".y; return _nw_vs_out; }");
        cursor = semi + 1;
    }

    out.append(msl.substr(body_close));
    return out;
}
#endif

// Slang reports warnings as well as errors through the diagnostic blob, and
// reports them on success too, so this is called after every step rather than
// only on failure.
void log_slang_diagnostics(
    const std::string& name, const Slang::ComPtr<slang::IBlob>& diagnostics)
{
    if (!diagnostics || diagnostics->getBufferSize() == 0) { return; }
    const std::string_view text{
        static_cast<const char*>(diagnostics->getBufferPointer()),
        diagnostics->getBufferSize()};
    LOG_F(WARNING, "Slang diagnostics for {}:\n{}", name, text);
}
#endif

} // namespace

ShaderProvider::ShaderProvider(nw::gfx::Context* ctx, nw::ResourceManager* resources)
    : ctx_(ctx)
    , resources_(resources)
{
}

ShaderProvider::~ShaderProvider()
{
    clear_cache();
#ifdef NW_RENDER_HAS_SLANG
    // Release in reverse creation order: the session keeps the global
    // session alive.
    if (slang_session_) {
        static_cast<slang::ISession*>(slang_session_)->release();
    }
    if (slang_global_session_) {
        static_cast<slang::IGlobalSession*>(slang_global_session_)->release();
    }
    // Deleted last: the session borrows it without holding a reference.
    delete static_cast<ResourceFileSystem*>(slang_file_system_);
#endif
}

bool ShaderProvider::initialize()
{
    if (!ctx_ || !resources_) {
        LOG_F(ERROR, "shader provider: missing gfx context or resource manager");
        return false;
    }
#ifdef NW_RENDER_HAS_SLANG
    slang::IGlobalSession* global_session = nullptr;
    if (SLANG_FAILED(slang::createGlobalSession(&global_session))) {
        LOG_F(ERROR, "Failed to create Slang global session");
        return false;
    }

    slang::TargetDesc target{};
#ifdef NW_GFX_BACKEND_METAL
    target.format = SLANG_METAL;
    slang::PreprocessorMacroDesc metal_macros[1]{};
    metal_macros[0].name = "NW_SHADER_METAL";
    metal_macros[0].value = "1";
#else
    // spirv_1_6 rather than a sm_6_* profile: the default drops to SPIR-V 1.3,
    // which lowers clip()/discard to OpKill instead of
    // OpDemoteToHelperInvocation. The two differ for quad derivatives along an
    // alpha-cutout edge, and the Vulkan backend deliberately enables
    // shaderDemoteToHelperInvocation.
    target.format = SLANG_SPIRV;
    target.profile = global_session->findProfile(kSpirvProfile);
    if (target.profile == SLANG_PROFILE_UNKNOWN) {
        LOG_F(ERROR, "Slang does not know the '{}' profile", kSpirvProfile);
        global_session->release();
        return false;
    }
#endif

    slang::CompilerOptionEntry options[2]{};
    uint32_t option_count = 0;
    options[option_count].name = slang::CompilerOptionName::Optimization;
    options[option_count].value.kind = slang::CompilerOptionValueKind::Int;
    options[option_count].value.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
    ++option_count;
#ifdef NW_GFX_BACKEND_METAL
    // Metal reserves `main` as a C/MSL identifier in some contexts so Slang
    // always renames HLSL entry point `main` to `main_0` (which metal_context.mm
    // looks up explicitly). Silence the noisy per-shader rename warning.
    options[option_count].name = slang::CompilerOptionName::DisableWarning;
    options[option_count].value.kind = slang::CompilerOptionValueKind::String;
    options[option_count].value.stringValue0 = "40100";
    ++option_count;
#endif

    auto* file_system = new ResourceFileSystem{resources_};

    slang::SessionDesc desc{};
    desc.targets = &target;
    desc.targetCount = 1;
#ifdef NW_GFX_BACKEND_METAL
    desc.preprocessorMacros = metal_macros;
    desc.preprocessorMacroCount = 1;
#endif
    // Shader #include resolves through ResourceManager; see ResourceFileSystem.
    desc.fileSystem = file_system;
    // HLSL's default matrix layout is column-major, which is what DXC used
    // for these same sources and what the C++ side uploads glm matrices as.
    // Slang's API default is ROW_MAJOR (unlike its own slangc CLI default),
    // and getting this wrong transposes every transform -- geometry renders
    // at wildly wrong scale rather than failing outright.
    desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    desc.compilerOptionEntries = options;
    desc.compilerOptionEntryCount = option_count;

    slang::ISession* session = nullptr;
    if (SLANG_FAILED(global_session->createSession(desc, &session)) || !session) {
        LOG_F(ERROR, "Failed to create Slang session");
        delete file_system;
        global_session->release();
        return false;
    }

    slang_global_session_ = global_session;
    slang_session_ = session;
    slang_file_system_ = file_system;
    compiler_available_ = true;
#ifdef NW_GFX_BACKEND_METAL
    LOG_F(INFO, "Slang available for runtime HLSL compilation (target Metal MSL)");
#else
    LOG_F(INFO, "Slang available for runtime HLSL compilation (profile {})",
        kSpirvProfile);
#endif
#else
    LOG_F(WARNING, "Slang not available, shader compilation disabled");
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

    const ShaderStage stage = is_vertex
        ? ShaderStage::vertex
        : (is_pixel ? ShaderStage::pixel : ShaderStage::compute);

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

    // Handed to Slang verbatim; its preprocessor resolves #include through
    // ResourceFileSystem, so module and hak overrides apply to headers too.
    const auto source_view = data.bytes.string_view();
    return compile_hlsl(name, std::string{source_view.data(), source_view.size()}, stage);
}

nw::gfx::Handle<nw::gfx::Shader> ShaderProvider::compile_hlsl(
    const std::string& name,
    const std::string& source,
    ShaderStage stage)
{
#ifdef NW_RENDER_HAS_SLANG
    if (!compiler_available_) {
        LOG_F(ERROR, "Slang not available");
        return {};
    }

    auto* session = static_cast<slang::ISession*>(slang_session_);

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
        name.c_str(), name.c_str(), source.c_str(), diagnostics.writeRef());
    log_slang_diagnostics(name, diagnostics);
    if (!module) {
        LOG_F(ERROR, "Slang failed to load shader module: {}", name);
        return {};
    }
    // The session's module cache owns the module; take a counted reference
    // (assignment addRef's, unlike attach()) so it stays alive through the
    // composition below without over-releasing the cache's own reference.
    Slang::ComPtr<slang::IModule> module_ref{module};

    // The shaders are plain HLSL with no [shader(...)] attribute, so the
    // stage cannot be inferred and must be passed in.
    diagnostics.setNull();
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    if (SLANG_FAILED(module->findAndCheckEntryPoint("main",
            slang_stage_for(stage), entry_point.writeRef(),
            diagnostics.writeRef()))
        || !entry_point) {
        log_slang_diagnostics(name, diagnostics);
        LOG_F(ERROR, "Slang could not find entry point 'main' in: {}", name);
        return {};
    }
    log_slang_diagnostics(name, diagnostics);

    slang::IComponentType* components[]
        = {static_cast<slang::IComponentType*>(module),
            static_cast<slang::IComponentType*>(entry_point.get())};
    diagnostics.setNull();
    Slang::ComPtr<slang::IComponentType> composed;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components, 2, composed.writeRef(), diagnostics.writeRef()))
        || !composed) {
        log_slang_diagnostics(name, diagnostics);
        LOG_F(ERROR, "Slang failed to compose shader program: {}", name);
        return {};
    }
    log_slang_diagnostics(name, diagnostics);

    diagnostics.setNull();
    Slang::ComPtr<slang::IComponentType> linked;
    if (SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef()))
        || !linked) {
        log_slang_diagnostics(name, diagnostics);
        LOG_F(ERROR, "Slang failed to link shader program: {}", name);
        return {};
    }
    log_slang_diagnostics(name, diagnostics);

    diagnostics.setNull();
    Slang::ComPtr<slang::IBlob> code;
    if (SLANG_FAILED(linked->getEntryPointCode(
            0, 0, code.writeRef(), diagnostics.writeRef()))
        || !code || code->getBufferSize() == 0) {
        log_slang_diagnostics(name, diagnostics);
        LOG_F(ERROR, "Slang produced no SPIR-V for: {}", name);
        return {};
    }
    log_slang_diagnostics(name, diagnostics);

    nw::gfx::ShaderDesc desc{};
#ifdef NW_GFX_BACKEND_METAL
    std::string metal_source;
    if (stage == ShaderStage::vertex) {
        metal_source = invert_metal_vertex_y(std::string_view{
            static_cast<const char*>(code->getBufferPointer()),
            code->getBufferSize()});
        desc.code = metal_source.data();
        desc.size = metal_source.size();
    } else {
        desc.code = code->getBufferPointer();
        desc.size = code->getBufferSize();
    }
    if (stage == ShaderStage::compute) {
        if (auto* layout = linked->getLayout(0)) {
            if (auto* ep = layout->getEntryPointByIndex(0)) {
                SlangUInt tg_size[3] = {1, 1, 1};
                ep->getComputeThreadGroupSize(3, tg_size);
                desc.threadgroup_size[0] = std::max<uint32_t>(1u, static_cast<uint32_t>(tg_size[0]));
                desc.threadgroup_size[1] = std::max<uint32_t>(1u, static_cast<uint32_t>(tg_size[1]));
                desc.threadgroup_size[2] = std::max<uint32_t>(1u, static_cast<uint32_t>(tg_size[2]));
            }
        }
    }
#else
    desc.code = code->getBufferPointer();
    desc.size = code->getBufferSize();
#endif

    const nw::gfx::Handle<nw::gfx::Shader> shader
        = nw::gfx::create_shader(ctx_, desc);
    if (shader.valid()) {
        LOG_F(INFO, "Successfully compiled shader: {}", name);
        shaders_[name] = shader;
    } else {
        LOG_F(ERROR, "Failed to create gfx shader for: {}", name);
    }
    return shader;
#else
    LOG_F(ERROR, "Cannot compile HLSL: Slang not available");
    (void)name;
    (void)source;
    (void)stage;
    return {};
#endif
}

} // namespace nw::render
