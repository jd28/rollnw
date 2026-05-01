#include "imgui_runtime.hpp"

#include "app_runtime.hpp"
#include "viewer_runtime.hpp"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <nw/gfx/native_vulkan.hpp>
#include <nw/log.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mudl {
namespace {

struct ImguiRuntime {
    bool initialized = false;
    bool vulkan_initialized = false;
    bool panel_open = true;
    SDL_Window* window = nullptr;
    nw::gfx::Context* gfx_context = nullptr;
    nw::gfx::NativeVulkanCore core{};
};

ImguiRuntime g_imgui;

bool ensure_vulkan_backend(AppState& state, nw::gfx::CommandList* cmd)
{
    if (!g_imgui.initialized || g_imgui.vulkan_initialized || !cmd) {
        return g_imgui.vulkan_initialized;
    }

    nw::gfx::NativeVulkanFrame frame{};
    if (!nw::gfx::get_native_vulkan_frame(state.gfx_context, cmd, frame)) {
        return false;
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = reinterpret_cast<VkInstance>(g_imgui.core.instance);
    init_info.PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(g_imgui.core.physical_device);
    init_info.Device = reinterpret_cast<VkDevice>(g_imgui.core.device);
    init_info.QueueFamily = g_imgui.core.graphics_queue_family;
    init_info.Queue = reinterpret_cast<VkQueue>(g_imgui.core.graphics_queue);
    init_info.DescriptorPoolSize = 32;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    const VkFormat color_format = static_cast<VkFormat>(frame.swapchain_format);
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        LOG_F(ERROR, "ImGui Vulkan backend initialization failed");
        return false;
    }

    g_imgui.vulkan_initialized = true;
    return true;
}

bool has_animation_names(const std::vector<std::vector<std::string>>& groups)
{
    for (const auto& names : groups) {
        if (!names.empty()) {
            return true;
        }
    }
    return false;
}

std::string scene_model_label(const nw::render::nwn::ModelInstance& model, size_t model_index)
{
    if (model.mdl_ && !model.mdl_->model.name.empty()) {
        return model.mdl_->model.name.c_str();
    }
    return "model " + std::to_string(model_index);
}

bool is_ring_emitter_name(const std::string& name)
{
    return name.rfind("ring", 0) == 0;
}

bool set_emitter_visibility_by_name_prefix(SceneParticleSystem& scene_particles, bool prefix_visible)
{
    bool changed = false;
    const size_t count = std::min(
        scene_particles.import.effect.emitters.size(),
        scene_particles.system.emitter_visible.size());
    for (size_t emitter_index = 0; emitter_index < count; ++emitter_index) {
        const bool matches = is_ring_emitter_name(scene_particles.import.effect.emitters[emitter_index].name);
        const uint8_t next_visible = (matches == prefix_visible) ? 1u : 0u;
        if (scene_particles.system.emitter_visible[emitter_index] != next_visible) {
            scene_particles.system.emitter_visible[emitter_index] = next_visible;
            changed = true;
        }
    }
    return changed;
}

bool set_all_emitters_visible(SceneParticleSystem& scene_particles, bool visible)
{
    bool changed = false;
    for (auto& emitter_visible : scene_particles.system.emitter_visible) {
        const uint8_t next_visible = visible ? 1u : 0u;
        if (emitter_visible != next_visible) {
            emitter_visible = next_visible;
            changed = true;
        }
    }
    return changed;
}

const char* particle_kernel_label(nw::render::CompiledParticleKernel kernel)
{
    using Kernel = nw::render::CompiledParticleKernel;
    switch (kernel) {
    case Kernel::sprite_basic_constant: return "sprite_basic_constant";
    case Kernel::sprite_basic: return "sprite_basic";
    case Kernel::sprite_target_gravity: return "sprite_target_gravity";
    case Kernel::sprite_target_bezier: return "sprite_target_bezier";
    case Kernel::linked_chain: return "linked_chain";
    case Kernel::beam_lightning: return "beam_lightning";
    case Kernel::mesh_basic: return "mesh_basic";
    }
    return "unknown";
}

const char* particle_render_mode_label(nw::render::ParticleRenderMode mode)
{
    using Mode = nw::render::ParticleRenderMode;
    switch (mode) {
    case Mode::billboard: return "billboard";
    case Mode::billboard_local_z: return "billboard_local_z";
    case Mode::billboard_world_z: return "billboard_world_z";
    case Mode::aligned_world_z: return "aligned_world_z";
    case Mode::velocity_aligned: return "velocity_aligned";
    case Mode::stretched: return "stretched";
    case Mode::linked_chain: return "linked_chain";
    case Mode::beam: return "beam";
    case Mode::mesh: return "mesh";
    }
    return "unknown";
}

const char* particle_blend_mode_label(nw::render::ParticleBlendMode mode)
{
    using Mode = nw::render::ParticleBlendMode;
    switch (mode) {
    case Mode::alpha: return "alpha";
    case Mode::cutout: return "cutout";
    case Mode::additive: return "additive";
    }
    return "unknown";
}

const char* particle_emission_mode_label(nw::render::ParticleEmissionMode mode)
{
    using Mode = nw::render::ParticleEmissionMode;
    switch (mode) {
    case Mode::continuous: return "continuous";
    case Mode::single_shot: return "single_shot";
    case Mode::event_burst: return "event_burst";
    case Mode::beam_continuous: return "beam_continuous";
    }
    return "unknown";
}

const char* particle_spawn_metric_label(nw::render::ParticleSpawnMetric metric)
{
    using Metric = nw::render::ParticleSpawnMetric;
    switch (metric) {
    case Metric::per_second: return "per_second";
    case Metric::per_distance: return "per_distance";
    }
    return "unknown";
}

const char* particle_simulation_space_label(nw::render::ParticleSimulationSpace space)
{
    using Space = nw::render::ParticleSimulationSpace;
    switch (space) {
    case Space::world: return "world";
    case Space::local: return "local";
    case Space::emitter_attached: return "emitter_attached";
    case Space::spawn_attached: return "spawn_attached";
    }
    return "unknown";
}

const char* particle_targeting_mode_label(nw::render::ParticleTargetingMode mode)
{
    using Mode = nw::render::ParticleTargetingMode;
    switch (mode) {
    case Mode::none: return "none";
    case Mode::point_gravity: return "point_gravity";
    case Mode::point_bezier: return "point_bezier";
    case Mode::beam_lightning: return "beam_lightning";
    }
    return "unknown";
}

const char* particle_path_kind_label(nw::render::ParticleRenderPathKind kind)
{
    using Kind = nw::render::ParticleRenderPathKind;
    switch (kind) {
    case Kind::none: return "none";
    case Kind::bezier: return "bezier";
    case Kind::beam: return "beam";
    }
    return "unknown";
}

const char* sequence_step_kind_label(VfxSequenceStepKind kind)
{
    switch (kind) {
    case VfxSequenceStepKind::model: return "model";
    case VfxSequenceStepKind::attached_model: return "attached_model";
    case VfxSequenceStepKind::beam: return "beam";
    case VfxSequenceStepKind::projectile: return "projectile";
    }
    return "unknown";
}

const char* sequence_projectile_path_label(VfxProjectilePathKind kind)
{
    switch (kind) {
    case VfxProjectilePathKind::default_path: return "default";
    case VfxProjectilePathKind::homing: return "homing";
    case VfxProjectilePathKind::ballistic: return "ballistic";
    case VfxProjectilePathKind::high_ballistic: return "high_ballistic";
    case VfxProjectilePathKind::burst_up: return "burst_up";
    case VfxProjectilePathKind::accelerating: return "accelerating";
    case VfxProjectilePathKind::spiral: return "spiral";
    case VfxProjectilePathKind::linked: return "linked";
    case VfxProjectilePathKind::bounce: return "bounce";
    case VfxProjectilePathKind::burst: return "burst";
    case VfxProjectilePathKind::linked_burst_up: return "linked_burst_up";
    case VfxProjectilePathKind::triple_ballistic_hit: return "triple_ballistic_hit";
    case VfxProjectilePathKind::triple_ballistic_miss: return "triple_ballistic_miss";
    case VfxProjectilePathKind::double_ballistic: return "double_ballistic";
    }
    return "unknown";
}

const char* sequence_projectile_orientation_label(VfxProjectileOrientationKind kind)
{
    switch (kind) {
    case VfxProjectileOrientationKind::none: return "none";
    case VfxProjectileOrientationKind::target: return "target";
    case VfxProjectileOrientationKind::path: return "path";
    }
    return "unknown";
}

const char* sequence_projectile_transport_label(VfxProjectileTransportKind kind)
{
    switch (kind) {
    case VfxProjectileTransportKind::none: return "none";
    case VfxProjectileTransportKind::moving_root: return "moving_root";
    case VfxProjectileTransportKind::source_rooted_target_point: return "source_rooted_target_point";
    }
    return "unknown";
}

void text_vec3(const char* label, const glm::vec3& value)
{
    ImGui::Text("%s: (%.3f, %.3f, %.3f)", label,
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z));
}

void build_sequence_step_debug(const AppState& state, size_t step_index, bool default_open)
{
    if (!state.loaded_vfx_sequence || step_index >= state.loaded_vfx_sequence->steps.size()
        || step_index >= state.vfx_sequence_steps.size()) {
        return;
    }

    const auto& authored = state.loaded_vfx_sequence->steps[step_index];
    const auto& runtime = state.vfx_sequence_steps[step_index];
    ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    const std::string header = "Step " + std::to_string(step_index) + ": "
        + sequence_step_kind_label(authored.kind) + "##sequence_step_" + std::to_string(step_index);
    if (!ImGui::TreeNodeEx(header.c_str(), flags)) {
        return;
    }

    ImGui::Text("Source: %s", authored.source.c_str());
    ImGui::Text("Label: %s", authored.label.empty() ? "<none>" : authored.label.c_str());
    ImGui::Text("Origin: %s", authored.debug_origin.empty() ? "<none>" : authored.debug_origin.c_str());
    ImGui::Text("Kind: %s  Duration: %d ms", sequence_step_kind_label(authored.kind), authored.duration_ms);
    if (authored.start_offset_ms >= 0) {
        ImGui::Text("Authored start: %d ms", authored.start_offset_ms);
    } else {
        ImGui::TextUnformatted("Authored start: <sequential>");
    }
    ImGui::Text("Runtime: %d -> %d ms  Active: %s",
        runtime.start_ms, runtime.end_ms,
        static_cast<int>(step_index) == state.vfx_sequence_active_step ? "yes" : "no");
    ImGui::Text("Anchor: %s  Source anchor: %s",
        authored.anchor.empty() ? "<none>" : authored.anchor.c_str(),
        authored.source_anchor.empty() ? "<none>" : authored.source_anchor.c_str());
    ImGui::Text("Target side: %s  Target point: %s",
        runtime.target_side ? "yes" : "no",
        runtime.uses_target_point ? "yes" : "no");
    ImGui::Text("Target mode: %s  Target anchor: %s",
        vfx_target_point_kind_label(authored.target_point_kind).data(),
        authored.target_anchor.empty() ? "<none>" : authored.target_anchor.c_str());
    ImGui::Text("Scale: %.3f  Position-only anchor: %s",
        static_cast<double>(authored.scale),
        authored.anchor_position_only ? "yes" : "no");
    ImGui::Text("Animation: %s  Caster animation: %s",
        authored.animation.empty() ? "<none>" : authored.animation.c_str(),
        runtime.caster_animation.empty() ? "<none>" : runtime.caster_animation.c_str());
    ImGui::Text("Path: %s  Orientation: %s  Transport: %s",
        sequence_projectile_path_label(runtime.projectile_path),
        sequence_projectile_orientation_label(runtime.projectile_orientation),
        sequence_projectile_transport_label(runtime.projectile_transport));
    text_vec3("Resolved source point", runtime.start_pos);
    text_vec3("Resolved target point", runtime.end_pos);
    text_vec3("Start offset", authored.start_offset);
    text_vec3("End offset", authored.end_offset);

    ImGui::TreePop();
}

struct EmitterLiveSummary {
    uint32_t live_particles = 0;
    uint32_t sample_count = 0;
    std::array<uint32_t, 4> sample_indices{};
    glm::vec3 min_position{std::numeric_limits<float>::max()};
    glm::vec3 max_position{std::numeric_limits<float>::lowest()};
};

std::vector<EmitterLiveSummary> summarize_emitters(const SceneParticleSystem& scene_particles)
{
    std::vector<EmitterLiveSummary> result(scene_particles.system.emitters.size());
    const auto& core = scene_particles.system.particles.core;
    for (size_t i = 0; i < core.position.size(); ++i) {
        const auto emitter_id = core.emitter_id[i];
        if (emitter_id >= result.size()) {
            continue;
        }
        auto& summary = result[emitter_id];
        ++summary.live_particles;
        summary.min_position = glm::min(summary.min_position, core.position[i]);
        summary.max_position = glm::max(summary.max_position, core.position[i]);
        if (summary.sample_count < summary.sample_indices.size()) {
            summary.sample_indices[summary.sample_count++] = static_cast<uint32_t>(i);
        }
    }
    return result;
}

std::string packet_emitter_summary(const SceneParticleSystem& scene_particles, const nw::render::ParticleRenderPacket& packet)
{
    const auto emitter_count = scene_particles.system.emitters.size();
    std::vector<bool> seen(emitter_count, false);
    std::string result;
    const auto& core = scene_particles.system.particles.core;
    for (uint32_t n = 0; n < packet.count; ++n) {
        const size_t particle_index = packet.begin + n;
        if (particle_index >= core.emitter_id.size()) {
            break;
        }
        const auto emitter_id = core.emitter_id[particle_index];
        if (emitter_id >= emitter_count || seen[emitter_id]) {
            continue;
        }
        seen[emitter_id] = true;
        const auto& emitter = scene_particles.import.effect.emitters[emitter_id];
        const std::string name = emitter.name.empty() ? "emitter " + std::to_string(emitter_id) : emitter.name;
        if (!result.empty()) {
            result += ", ";
        }
        result += name;
    }
    return result.empty() ? "<none>" : result;
}

void build_particle_packet_controls(const SceneParticleSystem& scene_particles, size_t system_index)
{
    const auto packets = scene_particles.system.render_packets.span();
    const std::string header = "Render packets (" + std::to_string(packets.size()) + ")##packets_" + std::to_string(system_index);
    if (!ImGui::TreeNode(header.c_str())) {
        return;
    }

    for (size_t packet_index = 0; packet_index < packets.size(); ++packet_index) {
        const auto& packet = packets[packet_index];
        const std::string label = "Packet " + std::to_string(packet_index) + ": "
            + particle_render_mode_label(packet.mode) + " / "
            + particle_blend_mode_label(packet.blend) + " / count "
            + std::to_string(packet.count);
        if (!ImGui::TreeNode(label.c_str())) {
            continue;
        }

        const auto* material = packet.material < scene_particles.import.effect.materials.size()
            ? &scene_particles.import.effect.materials[packet.material]
            : nullptr;
        ImGui::Text("Material %u", packet.material);
        ImGui::Text("Texture: %s", material ? material->texture.c_str() : "");
        ImGui::Text("Mesh: %s", material ? material->mesh.c_str() : "");
        ImGui::Text("Emitters: %s", packet_emitter_summary(scene_particles, packet).c_str());
        ImGui::Text("Range: begin %u, count %u", packet.begin, packet.count);
        ImGui::Text("Sheet: %u x %u frames %u-%u fps %.2f random %s",
            packet.sheet.columns, packet.sheet.rows, packet.sheet.frame_begin, packet.sheet.frame_end,
            static_cast<double>(packet.sheet.frames_per_second), packet.sheet.random_start ? "yes" : "no");
        ImGui::Text("Opacity %.3f  Blur %.3f  Max half width %.3f",
            static_cast<double>(packet.opacity_scale),
            static_cast<double>(packet.blur_length),
            static_cast<double>(packet.max_half_width));
        ImGui::Text("Double sided %s  Transparent %s  Sort back-to-front %s  Preserve sequence %s",
            packet.double_sided ? "yes" : "no",
            packet.transparent ? "yes" : "no",
            packet.sort_back_to_front ? "yes" : "no",
            packet.preserve_sequence ? "yes" : "no");
        ImGui::Text("Path: %s", particle_path_kind_label(packet.path.kind));
        if (packet.path.kind != nw::render::ParticleRenderPathKind::none) {
            text_vec3("Path source", packet.path.source);
            text_vec3("Path target", packet.path.target);
        }
        ImGui::TreePop();
    }

    ImGui::TreePop();
}

bool build_particle_emitter_controls(SceneParticleSystem& scene_particles, size_t system_index)
{
    bool visibility_changed = false;
    const auto summaries = summarize_emitters(scene_particles);
    const size_t count = std::min(
        scene_particles.import.effect.emitters.size(),
        scene_particles.system.emitter_visible.size());
    for (size_t emitter_index = 0; emitter_index < count; ++emitter_index) {
        const auto& authored = scene_particles.import.effect.emitters[emitter_index];
        const auto& compiled = scene_particles.compiled.effect.emitters[emitter_index];
        const auto& state = scene_particles.system.emitters[emitter_index];
        const auto& summary = emitter_index < summaries.size() ? summaries[emitter_index] : EmitterLiveSummary{};
        const auto material_id = compiled.material;
        const auto* material = material_id < scene_particles.import.effect.materials.size()
            ? &scene_particles.import.effect.materials[material_id]
            : nullptr;

        const std::string name = authored.name.empty()
            ? "emitter " + std::to_string(emitter_index)
            : authored.name;
        const std::string label = name + "##emitter_" + std::to_string(system_index) + "_" + std::to_string(emitter_index);
        if (!ImGui::TreeNode(label.c_str())) {
            continue;
        }

        bool visible = scene_particles.system.emitter_visible[emitter_index] != 0u;
        const std::string visible_label = "Visible##visible_" + std::to_string(system_index) + "_" + std::to_string(emitter_index);
        if (ImGui::Checkbox(visible_label.c_str(), &visible)) {
            scene_particles.system.emitter_visible[emitter_index] = visible ? 1u : 0u;
            visibility_changed = true;
        }

        ImGui::Text("Kernel: %s", particle_kernel_label(compiled.kernel));
        ImGui::Text("Render: %s  Blend: %s  Targeting: %s",
            particle_render_mode_label(compiled.render.mode),
            material ? particle_blend_mode_label(material->blend) : "unknown",
            particle_targeting_mode_label(compiled.targeting.mode));
        ImGui::Text("Emission: %s  Metric: %s  Simulation: %s",
            particle_emission_mode_label(compiled.emission.mode),
            particle_spawn_metric_label(compiled.emission.metric),
            particle_simulation_space_label(compiled.simulation_space));
        ImGui::Text("Material %u  Texture: %s  Mesh: %s",
            material_id,
            material ? material->texture.c_str() : "",
            material ? material->mesh.c_str() : "");
        if (material) {
            ImGui::Text("Sheet: %u x %u frames %u-%u fps %.2f random %s",
                material->sheet.columns, material->sheet.rows, material->sheet.frame_begin, material->sheet.frame_end,
                static_cast<double>(material->sheet.frames_per_second), material->sheet.random_start ? "yes" : "no");
        }
        ImGui::Text("Opacity %.3f  Blur %.3f  Deadspace %.3f  Sort order %d",
            static_cast<double>(compiled.render.opacity_scale),
            static_cast<double>(compiled.render.blur_length),
            static_cast<double>(compiled.render.deadspace_radians),
            compiled.render.sort_order);
        ImGui::Text("Max particles %u  Active %s  Live %u  Pending bursts %u",
            compiled.max_particles, state.active ? "yes" : "no", summary.live_particles, state.pending_bursts);
        ImGui::Text("Emitter time %.3f  Spawn accumulator %.3f  Distance accumulator %.3f",
            static_cast<double>(state.time),
            static_cast<double>(state.spawn_accumulator),
            static_cast<double>(state.distance_accumulator));
        text_vec3("World position", glm::vec3(state.world_transform[3]));
        text_vec3("Prev world position", state.prev_world_pos);
        text_vec3("Linear velocity", state.linear_velocity);
        text_vec3("Frame delta", state.frame_delta);
        text_vec3("Target point", state.target_point);

        if (summary.live_particles > 0) {
            text_vec3("Live min", summary.min_position);
            text_vec3("Live max", summary.max_position);
            text_vec3("Live extent", summary.max_position - summary.min_position);
            const auto& core = scene_particles.system.particles.core;
            for (uint32_t sample_index = 0; sample_index < summary.sample_count; ++sample_index) {
                const auto particle_index = summary.sample_indices[sample_index];
                if (particle_index >= core.position.size()) {
                    continue;
                }
                const uint32_t rgba8 = core.color_rgba8[particle_index];
                ImGui::SeparatorText(("Particle " + std::to_string(sample_index)).c_str());
                text_vec3("Position", core.position[particle_index]);
                text_vec3("Velocity", core.velocity[particle_index]);
                if (particle_index < core.render_direction.size()) {
                    text_vec3("Render direction", core.render_direction[particle_index]);
                }
                ImGui::Text("Age %.3f / %.3f  Normalized %.3f",
                    static_cast<double>(core.age[particle_index]),
                    static_cast<double>(core.lifetime[particle_index]),
                    static_cast<double>(core.normalized_age[particle_index]));
                ImGui::Text("Size %.3f x %.3f  Rotation %.3f  Rate %.3f",
                    static_cast<double>(core.size_x[particle_index]),
                    static_cast<double>(core.size_y[particle_index]),
                    static_cast<double>(core.rotation[particle_index]),
                    static_cast<double>(core.rotation_rate[particle_index]));
                ImGui::Text("Frame %u  Offset %u  Alpha %.3f",
                    core.frame[particle_index], core.frame_offset[particle_index],
                    static_cast<double>(static_cast<float>((rgba8 >> 24u) & 0xffu) / 255.0f));
            }
        }

        ImGui::TreePop();
    }
    return visibility_changed;
}

void build_particle_system_debug(SceneParticleSystem& scene_particles, size_t system_index)
{
    const size_t emitter_count = scene_particles.system.emitters.size();
    const size_t live_particles = scene_particles.system.particles.core.position.size();
    size_t ring_emitter_count = 0;
    size_t visible_ring_emitter_count = 0;
    const size_t ring_count = std::min(
        scene_particles.import.effect.emitters.size(),
        scene_particles.system.emitter_visible.size());
    for (size_t emitter_index = 0; emitter_index < ring_count; ++emitter_index) {
        if (!is_ring_emitter_name(scene_particles.import.effect.emitters[emitter_index].name)) {
            continue;
        }
        ++ring_emitter_count;
        if (scene_particles.system.emitter_visible[emitter_index] != 0u) {
            ++visible_ring_emitter_count;
        }
    }
    const std::string owner_name = scene_particles.owner && scene_particles.owner->mdl_
        ? scene_particles.owner->mdl_->model.name.c_str()
        : std::string{"<none>"};
    const std::string header = "Emitter set " + std::to_string(system_index)
        + "##set_" + std::to_string(system_index);
    if (!ImGui::TreeNode(header.c_str())) {
        return;
    }

    ImGui::Text("Owner: %s", owner_name.c_str());
    ImGui::Text("Live particles: %zu  Packets: %zu  Emitters: %zu  Beam rows: %zu",
        live_particles,
        scene_particles.system.render_packets.span().size(),
        emitter_count,
        scene_particles.system.particles.beams.source_position.size());
    ImGui::Text("Ring emitters: %zu visible / %zu total", visible_ring_emitter_count, ring_emitter_count);
    ImGui::Text("Owner animation time: %.3f s  Collision: %s",
        static_cast<double>(scene_particles.animation_time),
        scene_particles.collision ? "yes" : "no");
    bool visibility_changed = false;
    if (ImGui::Button(("Show all##emitters_all_" + std::to_string(system_index)).c_str())) {
        visibility_changed |= set_all_emitters_visible(scene_particles, true);
    }
    ImGui::SameLine();
    if (ImGui::Button(("Only ring##emitters_ring_" + std::to_string(system_index)).c_str())) {
        visibility_changed |= set_emitter_visibility_by_name_prefix(scene_particles, true);
    }
    ImGui::SameLine();
    if (ImGui::Button(("Hide ring##emitters_not_ring_" + std::to_string(system_index)).c_str())) {
        visibility_changed |= set_emitter_visibility_by_name_prefix(scene_particles, false);
    }
    if (!scene_particles.import.effect_events.empty()) {
        if (ImGui::TreeNode(("Effect events##events_" + std::to_string(system_index)).c_str())) {
            for (size_t event_index = 0; event_index < scene_particles.import.effect_events.size(); ++event_index) {
                const auto& event = scene_particles.import.effect_events[event_index];
                ImGui::Text("Event %zu: time %.3f s, burst count %u",
                    event_index, static_cast<double>(event.time), event.burst_count);
            }
            ImGui::TreePop();
        }
    }
    if (!scene_particles.import.warnings.empty()) {
        if (ImGui::TreeNode(("Import warnings##import_" + std::to_string(system_index)).c_str())) {
            for (const auto& warning : scene_particles.import.warnings) {
                ImGui::BulletText("%s.%s: %s", warning.emitter.c_str(), warning.field.c_str(), warning.message.c_str());
            }
            ImGui::TreePop();
        }
    }
    if (!scene_particles.compiled.warnings.empty()) {
        if (ImGui::TreeNode(("Compile warnings##compile_" + std::to_string(system_index)).c_str())) {
            for (const auto& warning : scene_particles.compiled.warnings) {
                ImGui::BulletText("Emitter %u: %s", warning.emitter, warning.message.c_str());
            }
            ImGui::TreePop();
        }
    }

    visibility_changed |= build_particle_emitter_controls(scene_particles, system_index);
    build_particle_packet_controls(scene_particles, system_index);

    if (visibility_changed) {
        nw::render::build_particle_render_packets(scene_particles.system);
    }

    ImGui::TreePop();
}

void build_animation_controls(AppState& state)
{
    const bool has_nwn_animations = has_animation_names(state.model_animation_names);
    const bool has_gltf_animations = has_animation_names(state.gltf_animation_names);
    if (!has_nwn_animations && !has_gltf_animations) {
        return;
    }

    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (has_nwn_animations) {
        if (!state.vfx_sequence_steps.empty()) {
            ImGui::TextUnformatted("NWN scene animations are driven by the active VFX sequence.");
        } else {
            const bool shared_source = state.current_scene
                && scene_uses_shared_nwn_animation_source(*state.current_scene);
            if (shared_source) {
                size_t shared_model_index = state.current_scene->models.size();
                for (size_t model_index = 0; model_index < state.model_animation_names.size()
                        && model_index < state.current_scene->models.size();
                    ++model_index) {
                    const auto& names = state.model_animation_names[model_index];
                    const auto& model = state.current_scene->models[model_index];
                    if (names.empty() || !model || !model->scene_animation_enabled) {
                        continue;
                    }

                    shared_model_index = model_index;
                    const char* current_animation = model->anim_ ? model->anim_->name.c_str() : "<none>";
                    if (ImGui::BeginCombo("NWN animation", current_animation)) {
                        for (const auto& name : names) {
                            const bool selected = model->anim_ && name == model->anim_->name.c_str();
                            if (ImGui::Selectable(name.c_str(), selected)) {
                                select_model_animation(state, shared_model_index, name);
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
            } else {
                for (size_t model_index = 0; model_index < state.model_animation_names.size()
                        && model_index < state.current_scene->models.size();
                    ++model_index) {
                    const auto& names = state.model_animation_names[model_index];
                    const auto& model = state.current_scene->models[model_index];
                    if (names.empty() || !model || !model->scene_animation_enabled) {
                        continue;
                    }

                    ImGui::PushID(static_cast<int>(model_index));
                    if (state.current_scene->models.size() > 1) {
                        ImGui::Text("Model %zu: %s", model_index, scene_model_label(*model, model_index).c_str());
                    }

                    const char* current_animation = model->anim_ ? model->anim_->name.c_str() : "<none>";
                    const char* combo_label = "##nwn_animation";
                    if (ImGui::BeginCombo(combo_label, current_animation)) {
                        for (const auto& name : names) {
                            const bool selected = model->anim_ && name == model->anim_->name.c_str();
                            if (ImGui::Selectable(name.c_str(), selected)) {
                                select_model_animation(state, model_index, name);
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            }
        }
    }

    if (has_gltf_animations) {
        if (has_nwn_animations) {
            ImGui::Separator();
        }
        if (state.current_scene->static_models.size() > 1) {
            ImGui::TextUnformatted("glTF clip selection is shared by clip index across animated static models.");
        }

        for (size_t model_index = 0; model_index < state.gltf_animation_names.size()
                && model_index < state.current_scene->static_models.size();
            ++model_index) {
            const auto& names = state.gltf_animation_names[model_index];
            const auto& model = state.current_scene->static_models[model_index];
            if (names.empty() || !model) {
                continue;
            }

            ImGui::PushID(static_cast<int>(1000 + model_index));
            if (state.current_scene->static_models.size() > 1) {
                ImGui::Text("glTF model %zu: %s", model_index, model->name.c_str());
            }

            const size_t active_index = state.gltf_animation_clip % names.size();
            const char* combo_label = state.current_scene->static_models.size() > 1 ? "Clip" : "glTF clip";
            if (ImGui::BeginCombo(combo_label, names[active_index].c_str())) {
                for (size_t clip_index = 0; clip_index < names.size(); ++clip_index) {
                    const bool selected = clip_index == active_index;
                    if (ImGui::Selectable(names[clip_index].c_str(), selected)) {
                        set_gltf_animation_clip(state, static_cast<uint32_t>(clip_index));
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
    }
}

void build_rendering_controls(AppState& state)
{
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (ImGui::Button("Reload shaders")) {
        request_renderer_reload(state);
    }

    ImGui::Checkbox("Grid", &state.show_debug_overlay);
    if (state.show_debug_overlay) {
        ImGui::SliderFloat("Grid spacing", &state.debug_grid_spacing, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Major interval", &state.debug_grid_major_interval, 2.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Minor width", &state.debug_grid_minor_width, 0.001f, 0.25f, "%.3f");
        ImGui::SliderFloat("Major width", &state.debug_grid_major_width, 0.001f, 0.35f, "%.3f");
        ImGui::SliderFloat("Grid opacity", &state.debug_grid_opacity, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("Grid z offset", &state.debug_grid_z_offset, -0.25f, 0.25f, "%.3f");
    }

    if (state.current_scene && state.current_scene->is_area) {
        ImGui::Checkbox("Authored fog", &state.show_authored_area_fog);

        static constexpr std::array<const char*, 2> kShadowDebugModes{{
            "Off",
            "Cascades",
        }};
        int shadow_debug_mode = static_cast<int>(std::min<uint32_t>(
            state.shadow_debug_mode, static_cast<uint32_t>(kShadowDebugModes.size() - 1)));
        if (ImGui::Combo("Shadow debug", &shadow_debug_mode, kShadowDebugModes.data(),
                static_cast<int>(kShadowDebugModes.size()))) {
            state.shadow_debug_mode = static_cast<uint32_t>(shadow_debug_mode);
        }

        if (has_area_day_night_controls(state)) {
            ImGui::SeparatorText("Area lighting");
            ImGui::Checkbox("Autoplay cycle", &state.area_day_night_autoplay);

            float cycle_time = state.area_day_night_elapsed;
            if (ImGui::SliderFloat("Cycle time", &cycle_time, 0.0f, 60.0f, "%.1f s")) {
                state.area_day_night_autoplay = false;
                set_area_day_night_elapsed_seconds(state, cycle_time, false);
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset cycle")) {
                state.area_day_night_autoplay = false;
                reset_area_day_night_cycle(state);
            }
        }
    }

    bool has_gltf_models = false;
    if (state.current_scene) {
        for (const auto& model : state.current_scene->static_models) {
            if (model) {
                has_gltf_models = true;
                break;
            }
        }
    }

    if (has_gltf_models) {
        ImGui::SliderFloat("glTF IBL", &state.gltf_ibl_strength, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("glTF exposure", &state.gltf_exposure, 0.1f, 10.0f, "%.2f");
    }
}

void build_debug_window(AppState& state)
{
    if (!g_imgui.panel_open) {
        return;
    }

    ImGui::Begin("mudl debug", &g_imgui.panel_open, ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("Viewport: %d x %d", state.window_width, state.window_height);
    build_rendering_controls(state);

    if (!state.vfx_sequence_steps.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Spell / VFX");
        ImGui::Text("Label: %s", state.vfx_sequence_label.empty() ? "<unnamed>" : state.vfx_sequence_label.c_str());
        ImGui::Text("Time: %d ms / %d ms", state.vfx_sequence_time_ms, state.vfx_sequence_loop_ms);
        ImGui::Text("Steps: %zu  Active step: %d", state.vfx_sequence_steps.size(), state.vfx_sequence_active_step);
        ImGui::Text("Autoplay: %s", state.vfx_sequence_autoplay ? "enabled" : "disabled");
        ImGui::TextUnformatted("Controls: Space toggle autoplay, . step scene, / reset scene");
        int scrub_time = state.vfx_sequence_time_ms;
        if (ImGui::SliderInt("Sequence time", &scrub_time, 0, std::max(state.vfx_sequence_loop_ms, 1))) {
            set_vfx_sequence_time(state, scrub_time, true);
        }

        if (state.loaded_vfx_sequence) {
            ImGui::Text("Source-target layout: %s  Distance: %.2f",
                state.loaded_vfx_sequence->source_target_layout ? "yes" : "no",
                static_cast<double>(state.loaded_vfx_sequence->source_target_distance));
            ImGui::Text("Spell actors: %s", state.loaded_vfx_sequence->use_spell_actors ? "yes" : "no");
            if (state.vfx_sequence_active_step >= 0
                && static_cast<size_t>(state.vfx_sequence_active_step) < state.vfx_sequence_steps.size()
                && static_cast<size_t>(state.vfx_sequence_active_step) < state.loaded_vfx_sequence->steps.size()) {
                ImGui::SeparatorText("Active step");
                build_sequence_step_debug(state, static_cast<size_t>(state.vfx_sequence_active_step), true);
            }
            if (ImGui::TreeNode("Sequence steps")) {
                for (size_t step_index = 0; step_index < state.vfx_sequence_steps.size(); ++step_index) {
                    const bool default_open = static_cast<int>(step_index) == state.vfx_sequence_active_step;
                    build_sequence_step_debug(state, step_index, default_open);
                }
                ImGui::TreePop();
            }
        }
    }

    if (state.current_scene) {
        ImGui::Separator();
        ImGui::Text("Models: %zu", state.current_scene->models.size());
        ImGui::Text("Particles: %zu", state.current_scene->particles.size());
        ImGui::Text("Scene playback: %s", state.scene_playing ? "playing" : "paused");
        if (ImGui::Button(state.scene_playing ? "Pause scene" : "Resume scene")) {
            toggle_scene_playback(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop scene")) {
            stop_scene_playback(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset scene")) {
            reset_scene_playback(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Step 33 ms")) {
            step_scene_playback(state, 33);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Shortcuts: P pause, Space autoplay, . step, / reset");
        if (supports_vfx_sequence_controls(state)) {
            ImGui::Text("VFX autoplay: %s", state.vfx_sequence_autoplay ? "enabled" : "disabled");
        }
        if (supports_gltf_animation_controls(state)) {
            ImGui::Text("glTF autoplay: %s", state.gltf_animation_autoplay ? "enabled" : "disabled");
        }
        if (has_area_day_night_controls(state)) {
            ImGui::Text("Area autoplay: %s", state.area_day_night_autoplay ? "enabled" : "disabled");
        }
        build_animation_controls(state);
        for (size_t system_index = 0; system_index < state.current_scene->particles.size(); ++system_index) {
            build_particle_system_debug(state.current_scene->particles[system_index], system_index);
        }
    }

    ImGui::End();
}

} // namespace

bool init_imgui(AppState& state, SDL_Window* window)
{
    if (!window || g_imgui.initialized) {
        return false;
    }

    if (!nw::gfx::get_native_vulkan_core(state.gfx_core, g_imgui.core)) {
        LOG_F(ERROR, "Failed to fetch native Vulkan core for ImGui");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        LOG_F(ERROR, "ImGui SDL3 backend initialization failed");
        ImGui::DestroyContext();
        return false;
    }

    g_imgui.window = window;
    g_imgui.gfx_context = state.gfx_context;
    g_imgui.initialized = true;
    return true;
}

void shutdown_imgui()
{
    if (!g_imgui.initialized) {
        return;
    }

    if (g_imgui.vulkan_initialized) {
        ImGui_ImplVulkan_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_imgui = {};
}

void imgui_process_event(const SDL_Event* event)
{
    if (!g_imgui.initialized || !event) {
        return;
    }
    ImGui_ImplSDL3_ProcessEvent(event);
}

bool imgui_wants_keyboard()
{
    return g_imgui.initialized && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
}

bool imgui_wants_mouse()
{
    return g_imgui.initialized && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

void imgui_prepare_frame(AppState& state, nw::gfx::CommandList* cmd)
{
    if (!g_imgui.initialized || !ensure_vulkan_backend(state, cmd)) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    build_debug_window(state);
}

void imgui_render(AppState& state, nw::gfx::CommandList* cmd)
{
    if (!g_imgui.initialized || !g_imgui.vulkan_initialized || !cmd) {
        return;
    }

    ImGui::Render();
    nw::gfx::NativeVulkanFrame frame{};
    if (!nw::gfx::get_native_vulkan_frame(state.gfx_context, cmd, frame)) {
        return;
    }
    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(), reinterpret_cast<VkCommandBuffer>(frame.command_buffer));
}

} // namespace mudl
