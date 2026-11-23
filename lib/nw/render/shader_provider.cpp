#include "shader_provider.hpp"

#include <nw/log.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <unordered_set>

#ifdef NW_RENDER_HAS_SLANG
#include <slang-com-ptr.h>
#include <slang.h>
#endif

namespace nw::render {

namespace {

constexpr int max_include_depth = 8;

// See ShaderProvider::initialize for why the SPIR-V profile is pinned.
constexpr const char* kSpirvProfile = "spirv_1_6";

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

// Returns the include filename if the line is `#include "file"`, otherwise nullopt.
std::optional<std::string_view> parse_include_line(std::string_view line)
{
    size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string_view::npos || line[pos] != '#') { return {}; }
    constexpr std::string_view directive = "#include";
    if (line.compare(pos, directive.size(), directive) != 0) { return {}; }
    const size_t open = line.find('"', pos + directive.size());
    if (open == std::string_view::npos) { return {}; }
    const size_t close = line.find('"', open + 1);
    if (close == std::string_view::npos) { return {}; }
    return line.substr(open + 1, close - open - 1);
}

bool expand_includes_impl(std::string_view source, std::string_view source_name,
    const ShaderIncludeResolver& resolver, int depth,
    std::unordered_set<std::string>& seen, std::string& out)
{
    if (depth > max_include_depth) {
        LOG_F(ERROR, "shader include depth limit exceeded in: {}", source_name);
        return false;
    }

    size_t line_no = 0;
    size_t start = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        std::string_view line = source.substr(start,
            end == std::string_view::npos ? std::string_view::npos : end - start);
        ++line_no;

        if (auto include = parse_include_line(line)) {
            const std::string filename{*include};
            if (seen.insert(filename).second) {
                auto content = resolver(filename);
                if (!content) {
                    LOG_F(ERROR, "failed to resolve shader include \"{}\" in: {}", filename, source_name);
                    return false;
                }
                out += fmt::format("#line 1 \"{}\"\n", filename);
                if (!expand_includes_impl(*content, filename, resolver, depth + 1, seen, out)) {
                    return false;
                }
                if (!out.empty() && out.back() != '\n') { out += '\n'; }
                out += fmt::format("#line {} \"{}\"\n", line_no + 1, source_name);
            }
        } else {
            out += line;
            if (end != std::string_view::npos) { out += '\n'; }
        }

        if (end == std::string_view::npos) { break; }
        start = end + 1;
    }
    return true;
}

} // namespace

std::optional<std::string> expand_shader_includes(
    std::string_view source,
    std::string_view source_name,
    const ShaderIncludeResolver& resolver)
{
    std::string out;
    out.reserve(source.size());
    std::unordered_set<std::string> seen;
    if (!expand_includes_impl(source, source_name, resolver, 0, seen, out)) {
        return {};
    }
    return out;
}

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

    // spirv_1_6 rather than a sm_6_* profile: the default drops to SPIR-V 1.3,
    // which lowers clip()/discard to OpKill instead of
    // OpDemoteToHelperInvocation. The two differ for quad derivatives along an
    // alpha-cutout edge, and the Vulkan backend deliberately enables
    // shaderDemoteToHelperInvocation.
    slang::TargetDesc target{};
    target.format = SLANG_SPIRV;
    target.profile = global_session->findProfile(kSpirvProfile);
    if (target.profile == SLANG_PROFILE_UNKNOWN) {
        LOG_F(ERROR, "Slang does not know the '{}' profile", kSpirvProfile);
        global_session->release();
        return false;
    }

    slang::CompilerOptionEntry options[1]{};
    options[0].name = slang::CompilerOptionName::Optimization;
    options[0].value.kind = slang::CompilerOptionValueKind::Int;
    options[0].value.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;

    slang::SessionDesc desc{};
    desc.targets = &target;
    desc.targetCount = 1;
    // HLSL's default matrix layout is column-major, which is what DXC used
    // for these same sources and what the C++ side uploads glm matrices as.
    // Slang's API default is ROW_MAJOR (unlike its own slangc CLI default),
    // and getting this wrong transposes every transform -- geometry renders
    // at wildly wrong scale rather than failing outright.
    desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    desc.compilerOptionEntries = options;
    desc.compilerOptionEntryCount = 1;

    slang::ISession* session = nullptr;
    if (SLANG_FAILED(global_session->createSession(desc, &session)) || !session) {
        LOG_F(ERROR, "Failed to create Slang session");
        global_session->release();
        return false;
    }

    slang_global_session_ = global_session;
    slang_session_ = session;
    compiler_available_ = true;
    LOG_F(INFO, "Slang available for runtime HLSL compilation (profile {})",
        kSpirvProfile);
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

    const auto source_view = data.bytes.string_view();
    auto source = expand_shader_includes(source_view, name,
        [this](std::string_view filename) -> std::optional<std::string> {
            const auto res = nw::Resource::from_path(std::filesystem::path{filename});
            if (!res.valid() || res.type != nw::ResourceType::hlsl) { return {}; }
            auto bytes = resources_->demand(res);
            if (!bytes.bytes.size()) { return {}; }
            const auto view = bytes.bytes.string_view();
            return std::string{view.data(), view.size()};
        });
    if (!source) {
        LOG_F(ERROR, "Failed to expand shader includes for: {}", name);
        return {};
    }

    return compile_hlsl(name, *source, stage);
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

    // The source arrives already include-expanded (expand_shader_includes
    // resolves through ResourceManager so a module or hak can override a
    // shader), so Slang is handed one self-contained translation unit and
    // never needs its own file system.
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
    desc.code = code->getBufferPointer();
    desc.size = code->getBufferSize();

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
