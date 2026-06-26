#include <gtest/gtest.h>

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/render/model_instance_animation.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace {

nw::model::Vertex make_test_vertex(const glm::vec3& position)
{
    nw::model::Vertex vertex;
    vertex.position = position;
    vertex.normal = glm::vec3{0.0f, 0.0f, 1.0f};
    vertex.tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
    return vertex;
}

bool valid_render_vec3(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && glm::dot(value, value) > 1.0e-10f;
}

bool valid_render_tangent(const glm::vec4& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w)
        && glm::dot(glm::vec3{value}, glm::vec3{value}) > 1.0e-10f;
}

bool unit_render_vec3(const glm::vec3& value)
{
    return valid_render_vec3(value) && std::abs(glm::length(value) - 1.0f) <= 1.0e-3f;
}

bool unit_render_tangent(const glm::vec4& value)
{
    return valid_render_tangent(value)
        && std::abs(glm::length(glm::vec3{value}) - 1.0f) <= 1.0e-3f
        && std::abs(std::abs(value.w) - 1.0f) <= 1.0e-6f;
}

bool same_vec2(const glm::vec2& lhs, const glm::vec2& rhs, float epsilon = 1.0e-6f)
{
    return std::abs(lhs.x - rhs.x) <= epsilon
        && std::abs(lhs.y - rhs.y) <= epsilon;
}

bool same_vec3(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 1.0e-6f)
{
    return std::abs(lhs.x - rhs.x) <= epsilon
        && std::abs(lhs.y - rhs.y) <= epsilon
        && std::abs(lhs.z - rhs.z) <= epsilon;
}

float max_abs_matrix_delta(const glm::mat4& lhs, const glm::mat4& rhs) noexcept
{
    float result = 0.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result = std::max(result, std::abs(lhs[column][row] - rhs[column][row]));
        }
    }
    return result;
}

uint32_t packed_u8_lane(uint32_t value, int lane) noexcept
{
    return (value >> (static_cast<uint32_t>(lane) * 8u)) & 0xffu;
}

struct TestGfxRuntime {
    nw::gfx::Core* core = nullptr;
    nw::gfx::Context* context = nullptr;
    bool owns_sdl_video = false;

    ~TestGfxRuntime()
    {
        if (context) {
            nw::gfx::wait_idle(context);
            nw::gfx::destroy_context(context);
        }
        if (core) {
            nw::gfx::destroy_core(core);
        }
        if (owns_sdl_video) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    bool initialize()
    {
        if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                return false;
            }
            owns_sdl_video = true;
        }

        nw::gfx::CoreConfig core_config{};
        core_config.app_name = "rollnw_test";
        core_config.enable_validation = false;
        core = nw::gfx::create_core(core_config);
        if (!core) {
            return false;
        }

        nw::gfx::ContextDesc context_desc{};
        context_desc.width = 64;
        context_desc.height = 64;
        context = nw::gfx::create_context(core, context_desc);
        return context != nullptr;
    }
};

void destroy_render_model_test_buffers(nw::render::RenderModel& model)
{
    for (auto& primitive : model.primitives) {
        if (primitive.vertices.valid()) {
            nw::gfx::destroy_buffer(primitive.vertices);
            primitive.vertices = {};
        }
        if (primitive.indices.valid()) {
            nw::gfx::destroy_buffer(primitive.indices);
            primitive.indices = {};
        }
    }
}

void destroy_render_model_test_textures(nw::gfx::Context* context, nw::render::RenderModel& model)
{
    for (auto& texture : model.textures) {
        if (texture.valid()) {
            nw::gfx::destroy_texture(context, texture);
            texture = {};
        }
    }
    model.textures.clear();
}

} // namespace

TEST(RenderModelLoader, CreatesSkinMeshNodes)
{
    nw::model::Mdl mdl{"test_data/user/development/c_satyr.mdl"};
    ASSERT_TRUE(mdl.valid());

    nw::render::nwn::ModelInstance model;
    ASSERT_TRUE(model.load(&mdl, nullptr));

    size_t parsed_skin_nodes = 0;
    for (const auto& node : mdl.model.nodes) {
        const auto* skin = dynamic_cast<const nw::model::SkinNode*>(node.get());
        if (skin && skin->render && !skin->vertices.empty() && !skin->indices.empty()) {
            ++parsed_skin_nodes;
        }
    }
    ASSERT_GT(parsed_skin_nodes, 0u);

    size_t loaded_skin_meshes = 0;
    for (const auto& node : model.nodes_) {
        if (dynamic_cast<const nw::render::nwn::SkinMesh*>(node.get())) {
            ++loaded_skin_meshes;
        }
    }

    EXPECT_EQ(loaded_skin_meshes, parsed_skin_nodes);
}

TEST(RenderModelLoader, ImportsNwnModelAssetSkinMeshes)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/c_satyr.mdl"};
    ASSERT_TRUE(mdl.valid());

    size_t parsed_skin_nodes = 0;
    for (const auto& node : mdl.model.nodes) {
        const auto* skin = dynamic_cast<const nw::model::SkinNode*>(node.get());
        if (skin && skin->render && !skin->vertices.empty() && !skin->indices.empty()) {
            ++parsed_skin_nodes;
        }
    }
    ASSERT_GT(parsed_skin_nodes, 0u);

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_EQ(result.stats.skipped_skin_mesh_count, 0u);
    ASSERT_FALSE(asset.skins.empty());

    size_t skinned_primitive_count = 0;
    uint32_t first_skin = std::numeric_limits<uint32_t>::max();
    for (const auto& primitive : asset.primitives) {
        if (!primitive.skinned) {
            continue;
        }

        ++skinned_primitive_count;
        ASSERT_TRUE(primitive.uses_skinned_vertices());
        ASSERT_LT(primitive.skin, asset.skins.size());
        ASSERT_GE(primitive.node, 0);
        ASSERT_LT(static_cast<size_t>(primitive.node), asset.nodes.size());
        if (first_skin == std::numeric_limits<uint32_t>::max()) {
            first_skin = primitive.skin;
        }

        const auto& skin = asset.skins[primitive.skin];
        EXPECT_EQ(skin.skeleton, 0u);
        ASSERT_FALSE(skin.joints.empty());
        ASSERT_EQ(skin.joints.size(), skin.inverse_bind_matrices.size());
        EXPECT_TRUE(nw::render::model_skin_bone_count_supported(skin.joints.size()));
        for (const int32_t joint : skin.joints) {
            ASSERT_GE(joint, 0);
            ASSERT_LT(static_cast<size_t>(joint), asset.nodes.size());
        }

        for (const auto& vertex : primitive.skinned_vertices) {
            EXPECT_TRUE(valid_render_vec3(vertex.normal));
            EXPECT_TRUE(valid_render_tangent(vertex.tangent));
            for (int lane = 0; lane < 4; ++lane) {
                EXPECT_LT(packed_u8_lane(vertex.joint_indices, lane), skin.joints.size());
            }
        }
    }
    EXPECT_EQ(skinned_primitive_count, parsed_skin_nodes);

    const auto validation = nw::render::validate_model_asset(asset);
    EXPECT_TRUE(validation.passed());

    ASSERT_NE(first_skin, std::numeric_limits<uint32_t>::max());
    ASSERT_FALSE(asset.animations.empty());
    nw::render::RenderModel runtime_model;
    runtime_model.name = asset.name;
    runtime_model.nodes = asset.nodes;
    runtime_model.skeletons = asset.skeletons;
    runtime_model.animations = asset.animations;
    runtime_model.skins = asset.skins;

    nw::render::ModelInstance instance;
    instance.kind = nw::render::ModelInstanceKind::render_model;
    instance.animation.enabled = true;
    instance.animation.clip = 0;
    instance.animation.time = 0.25f;
    instance.animation.looping = true;
    instance.animation.backend = nw::render::make_render_model_animation_backend(runtime_model);
    ASSERT_TRUE(instance.animation.backend);

    ASSERT_TRUE(nw::render::sample_model_instance_animation(instance, runtime_model));
    ASSERT_LT(first_skin, instance.animation.skin_matrices.size());
    EXPECT_EQ(instance.animation.skin_matrices[first_skin].size(), asset.skins[first_skin].joints.size());
}

TEST(RenderModelLoader, AppliesMtrMaterialnameSidecarScalars)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    const nwn::Mesh* mesh = nullptr;
    for (const auto& node : model->nodes_) {
        const auto* candidate = dynamic_cast<const nwn::Mesh*>(node.get());
        if (candidate && candidate->materialname == "test_mtr_material") {
            mesh = candidate;
            break;
        }
    }

    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->bitmap_name, "test_mtr_diffuse");
    EXPECT_EQ(mesh->materialname, "test_mtr_material");
    EXPECT_EQ(mesh->material_mode, nw::render::MaterialMode::transparent);
    EXPECT_TRUE(mesh->two_sided_lighting);
    EXPECT_NEAR(mesh->roughness, 0.32f, 0.001f);
    EXPECT_NEAR(mesh->common_pbr_roughness, 0.32f, 0.001f);
    EXPECT_NEAR(mesh->specular_strength, 0.18f, 0.001f);
    EXPECT_NEAR(mesh->alpha_cutout_threshold, 0.42f, 0.001f);
    EXPECT_NEAR(mesh->emissive.x, 0.10f, 0.001f);
    EXPECT_NEAR(mesh->emissive.y, 0.20f, 0.001f);
    EXPECT_NEAR(mesh->emissive.z, 0.30f, 0.001f);
}

TEST(RenderModelLoader, ResourceCacheStatsTrackMtrAndClear)
{
    namespace nwn = nw::render::nwn;

    nwn::clear_model_loader_resource_caches();
    EXPECT_TRUE(nwn::model_loader_resource_cache_stats().empty());

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    const auto stats = nwn::model_loader_resource_cache_stats();
    EXPECT_GE(stats.mtr_material_count, 1u);

    nwn::clear_model_loader_resource_caches();
    EXPECT_TRUE(nwn::model_loader_resource_cache_stats().empty());
}

TEST(RenderModelLoader, TileGradedAlphaWithoutExplicitHintStaysOpaque)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_mesh"};
    node.bitmap = "stone_alpha_noise";
    node.transparencyhint = 0;

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::graded,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::opaque);
}

TEST(RenderModelLoader, TileGradedGrassTextureWithoutExplicitHintStaysOpaque)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_ground"};
    node.bitmap = "ttf01_grass01";
    node.transparencyhint = 0;

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::graded,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::opaque);
}

TEST(RenderModelLoader, TileGradedAlphaUsesExplicitTransparencyHint)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_overlay"};
    node.bitmap = "stone_overlay";
    node.transparencyhint = 1;

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::graded,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::transparent);
}

TEST(RenderModelLoader, ExtractsDummyNodesAsSocketRecords)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    ASSERT_EQ(model->sockets().size(), 1u);
    const uint32_t socket_index = model->socket_index("test_mtr_material");
    ASSERT_NE(socket_index, nw::render::kInvalidModelNodeIndex);

    const auto* socket = model->socket(socket_index);
    ASSERT_NE(socket, nullptr);
    EXPECT_EQ(socket->name, "test_mtr_material");
    ASSERT_LT(socket->source_node_index, model->source_nodes_.size());
    EXPECT_EQ(model->socket_node(socket_index), model->source_nodes_[socket->source_node_index]);
    EXPECT_EQ(model->socket_node(99u), nullptr);
    EXPECT_EQ(model->socket_index("missing_socket"), nw::render::kInvalidModelNodeIndex);
    EXPECT_FLOAT_EQ(socket->local_transform[0][0], 1.0f);
    EXPECT_FLOAT_EQ(socket->bind_transform[0][0], 1.0f);
}

TEST(RenderModelLoader, MeshHandAnchorsImportAsSocketAliases)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mesh_socket_alias.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    ASSERT_EQ(model->sockets().size(), 3u);
    EXPECT_EQ(model->socket_index("rhand_g"), nw::render::kInvalidModelNodeIndex);
    EXPECT_EQ(model->socket_index("lhand_g"), nw::render::kInvalidModelNodeIndex);

    const uint32_t right_index = model->socket_index("rhand");
    const uint32_t left_index = model->socket_index("lhand");
    ASSERT_NE(right_index, nw::render::kInvalidModelNodeIndex);
    ASSERT_NE(left_index, nw::render::kInvalidModelNodeIndex);

    const auto* right_socket = model->socket(right_index);
    const auto* left_socket = model->socket(left_index);
    ASSERT_NE(right_socket, nullptr);
    ASSERT_NE(left_socket, nullptr);
    ASSERT_LT(right_socket->source_node_index, model->source_nodes_.size());
    ASSERT_LT(left_socket->source_node_index, model->source_nodes_.size());

    const auto* right_node = model->socket_node(right_index);
    const auto* left_node = model->socket_node(left_index);
    ASSERT_NE(right_node, nullptr);
    ASSERT_NE(left_node, nullptr);
    ASSERT_NE(right_node->orig_, nullptr);
    ASSERT_NE(left_node->orig_, nullptr);
    EXPECT_EQ(right_node->orig_->name, "rhand_g");
    EXPECT_EQ(left_node->orig_->name, "lhand_g");
    EXPECT_FLOAT_EQ(right_socket->local_transform[0][0], 1.5f);
    EXPECT_FLOAT_EQ(right_socket->bind_transform[0][0], 1.5f);
    EXPECT_FLOAT_EQ(left_socket->local_transform[0][0], 0.75f);
    EXPECT_FLOAT_EQ(left_socket->bind_transform[0][0], 0.75f);

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    auto find_asset_socket = [&](std::string_view name) -> const nw::render::ModelSocket* {
        const auto it = std::find_if(asset.sockets.begin(), asset.sockets.end(), [name](const auto& socket) {
            return nw::string::icmp(socket.name, name);
        });
        return it == asset.sockets.end() ? nullptr : &*it;
    };

    ASSERT_EQ(asset.sockets.size(), 3u);
    const auto* asset_right_socket = find_asset_socket("rhand");
    const auto* asset_left_socket = find_asset_socket("lhand");
    ASSERT_NE(asset_right_socket, nullptr);
    ASSERT_NE(asset_left_socket, nullptr);
    EXPECT_EQ(asset_right_socket->source_node_index, right_socket->source_node_index);
    EXPECT_EQ(asset_left_socket->source_node_index, left_socket->source_node_index);
    EXPECT_FLOAT_EQ(asset_right_socket->local_transform[0][0], 1.5f);
    EXPECT_FLOAT_EQ(asset_right_socket->bind_transform[0][0], 1.5f);
    EXPECT_FLOAT_EQ(asset_left_socket->local_transform[0][0], 0.75f);
    EXPECT_FLOAT_EQ(asset_left_socket->bind_transform[0][0], 0.75f);
}

TEST(RenderModelLoader, ImportsStaticNwnModelAssetPayloads)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_EQ(asset.source_kind, nw::render::ModelAssetSourceKind::nwn_legacy);
    EXPECT_EQ(asset.name, mdl.model.name);
    EXPECT_EQ(result.stats.source_node_count, mdl.model.nodes.size());
    EXPECT_GT(result.stats.primitive_count, 0u);
    EXPECT_EQ(result.stats.primitive_count, asset.primitives.size());
    EXPECT_EQ(result.stats.material_count, asset.materials.size());
    EXPECT_EQ(asset.material_texture_sources.size(), asset.materials.size());
    ASSERT_EQ(asset.sockets.size(), 1u);
    EXPECT_EQ(asset.sockets.front().name, "test_mtr_material");
    EXPECT_TRUE(asset.shadow.valid);
    EXPECT_FALSE(asset.shadow.casts_shadow);
    EXPECT_EQ(asset.shadow.caster_count, 0u);
    ASSERT_FALSE(asset.primitives.empty());
    ASSERT_LT(asset.primitives.front().material, asset.materials.size());
    EXPECT_NEAR(asset.materials[asset.primitives.front().material].roughness, 0.32f, 0.001f);
    EXPECT_NEAR(asset.materials[asset.primitives.front().material].specular_strength, 0.18f, 0.001f);

    const auto validation = nw::render::validate_model_asset(asset);
    EXPECT_TRUE(validation.passed());
    EXPECT_EQ(validation.primitive_count, result.stats.primitive_count);
}

TEST(RenderModelLoader, ImportsNwnModelAssetSameSourceAnimationClips)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/it_torch_000.mdl"};
    ASSERT_TRUE(mdl.valid());
    ASSERT_GE(mdl.model.animations.size(), 2u);

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    ASSERT_EQ(asset.skeletons.size(), 1u);
    ASSERT_EQ(asset.animations.size(), mdl.model.animations.size());
    EXPECT_EQ(asset.animations[0].name, "equip");
    EXPECT_EQ(asset.animations[1].name, "unequip");
    EXPECT_EQ(asset.animations[0].skeleton, 0u);
    EXPECT_EQ(asset.animations[1].skeleton, 0u);

    const auto& skeleton = asset.skeletons[0];
    ASSERT_FALSE(skeleton.joints.empty());
    EXPECT_EQ(asset.animations[0].tracks.size(), skeleton.joints.size());
    EXPECT_EQ(asset.animations[1].tracks.size(), skeleton.joints.size());
    for (const auto& joint : skeleton.joints) {
        ASSERT_GE(joint.node, 0);
        EXPECT_LT(static_cast<size_t>(joint.node), asset.nodes.size());
    }
}

TEST(RenderModelLoader, ImportsNwnModelAssetParticleSystems)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/vfx_detonate_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_EQ(result.stats.primitive_count, 0u);
    EXPECT_EQ(result.stats.particle_system_count, asset.particle_systems.size());
    EXPECT_TRUE(asset.shadow.valid);
    EXPECT_FALSE(asset.shadow.casts_shadow);
    EXPECT_EQ(asset.shadow.caster_count, 0u);
    ASSERT_EQ(asset.particle_systems.size(), 2u);

    const auto base = std::find_if(asset.particle_systems.begin(), asset.particle_systems.end(), [](const auto& it) {
        return it.animation_name.empty();
    });
    ASSERT_NE(base, asset.particle_systems.end());
    EXPECT_EQ(base->effect.emitters.size(), 1u);
    EXPECT_TRUE(base->effect_events.empty());
    EXPECT_FLOAT_EQ(base->animation_length, 0.0f);

    const auto impact = std::find_if(asset.particle_systems.begin(), asset.particle_systems.end(), [](const auto& it) {
        return it.animation_name == "impact";
    });
    ASSERT_NE(impact, asset.particle_systems.end());
    ASSERT_EQ(impact->effect.emitters.size(), 1u);
    ASSERT_EQ(impact->effect_events.size(), 2u);
    EXPECT_FLOAT_EQ(impact->effect_events[0].time, 0.0f);
    EXPECT_EQ(impact->effect_events[0].burst_count, 1u);
    EXPECT_FLOAT_EQ(impact->effect_events[1].time, 0.5f);
    EXPECT_EQ(impact->effect_events[1].burst_count, 2u);
    EXPECT_GT(impact->animation_length, 0.0f);
    EXPECT_EQ(result.stats.particle_event_count, 2u);

    const auto validation = nw::render::validate_model_asset(asset);
    EXPECT_TRUE(validation.passed());
    EXPECT_EQ(validation.particle_system_count, asset.particle_systems.size());
}

TEST(RenderModelLoader, ImportsNwnModelAssetParticleAnimationCurves)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/c_mindflayer.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    const auto special = std::find_if(asset.particle_systems.begin(), asset.particle_systems.end(), [](const auto& it) {
        return it.animation_name == "cspecial";
    });
    ASSERT_NE(special, asset.particle_systems.end());

    const auto emitter = std::find_if(special->effect.emitters.begin(), special->effect.emitters.end(), [](const auto& it) {
        return it.name == "OmenEmitter01";
    });
    ASSERT_NE(emitter, special->effect.emitters.end());
    ASSERT_GT(emitter->emission.rate_over_time.keys.size(), 1u);
    EXPECT_GT(emitter->emission.rate_over_time.keys[1].value, 0.0f);
}

TEST(RenderModelLoader, ImportsNwnModelAssetSupermodelAnimationClips)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/c_bodak.mdl"};
    ASSERT_TRUE(mdl.valid());
    ASSERT_TRUE(mdl.model.supermodel);
    ASSERT_GT(mdl.model.supermodel->model.animations.size(), 0u);

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    auto& asset = *result.asset;

    ASSERT_EQ(asset.skeletons.size(), 1u);
    const auto& skeleton = asset.skeletons[0];
    ASSERT_FALSE(skeleton.joints.empty());
    EXPECT_GT(asset.animations.size(), mdl.model.animations.size());

    const auto clip_it = std::find_if(asset.animations.begin(), asset.animations.end(), [](const auto& clip) {
        return clip.name == "walk";
    });
    ASSERT_NE(clip_it, asset.animations.end());
    EXPECT_EQ(clip_it->skeleton, 0u);
    EXPECT_EQ(clip_it->tracks.size(), skeleton.joints.size());

    const auto keyed_track = std::find_if(clip_it->tracks.begin(), clip_it->tracks.end(), [](const auto& track) {
        return !track.translations.empty() || !track.rotations.empty() || !track.scales.empty();
    });
    ASSERT_NE(keyed_track, clip_it->tracks.end());

    nw::render::RenderModel runtime_model;
    runtime_model.name = asset.name;
    runtime_model.nodes = asset.nodes;
    runtime_model.skeletons = asset.skeletons;
    runtime_model.animations = asset.animations;
    runtime_model.skins = asset.skins;

    nw::render::ModelInstance instance;
    instance.kind = nw::render::ModelInstanceKind::render_model;
    instance.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 4.0f, 0.0f});
    instance.animation.enabled = true;
    instance.animation.clip = static_cast<uint32_t>(std::distance(asset.animations.begin(), clip_it));
    instance.animation.time = 0.25f;
    instance.animation.looping = true;
    instance.animation.backend = nw::render::make_render_model_animation_backend(runtime_model);
    ASSERT_TRUE(instance.animation.backend);

    ASSERT_TRUE(nw::render::sample_model_instance_animation(instance, runtime_model));
    ASSERT_FALSE(instance.attachment_node_world_transforms.empty());
    ASSERT_EQ(instance.attachment_node_world_transforms.size(), instance.attachment_node_transform_valid.size());

    size_t valid_row_count = 0;
    for (const auto valid : instance.attachment_node_transform_valid) {
        valid_row_count += valid != 0u ? 1u : 0u;
    }
    EXPECT_EQ(valid_row_count, skeleton.joints.size());
}

TEST(RenderModelLoader, ImportsNwnModelAssetTextureSourcesAsEncodedBytes)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/PLC_C04.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_FALSE(result.stats.complete());
    EXPECT_EQ(result.stats.unsupported_specular_texture_count, 3u);
    EXPECT_EQ(result.stats.unsupported_plt_texture_count, 0u);
    EXPECT_EQ(result.stats.missing_texture_source_count, 0u);
    ASSERT_EQ(asset.primitives.size(), 3u);
    ASSERT_EQ(asset.material_texture_sources.size(), asset.materials.size());
    ASSERT_GE(asset.texture_sources.size(), 3u);
    for (const auto& source : asset.texture_sources) {
        EXPECT_EQ(source.kind, nw::render::ModelAssetTextureSourceKind::encoded_bytes);
        EXPECT_TRUE(source.resource.valid());
        EXPECT_TRUE(source.resource.type == nw::ResourceType::dds || source.resource.type == nw::ResourceType::tga);
        EXPECT_FALSE(source.encoded_bytes.empty());
        EXPECT_TRUE(source.path.empty());
    }
    for (const auto& sources : asset.material_texture_sources) {
        EXPECT_NE(sources.albedo, nw::render::kInvalidModelAssetTextureSourceIndex);
        EXPECT_NE(sources.normal, nw::render::kInvalidModelAssetTextureSourceIndex);
        EXPECT_NE(sources.metallic_roughness, nw::render::kInvalidModelAssetTextureSourceIndex);
        EXPECT_EQ(sources.emissive, nw::render::kInvalidModelAssetTextureSourceIndex);
    }
    for (const auto& material : asset.materials) {
        EXPECT_FLOAT_EQ(material.roughness, 1.0f);
        EXPECT_FLOAT_EQ(material.metallic, 0.0f);
    }
}

TEST(RenderModelLoader, ImportsNwnModelAssetPltTextureSources)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_plt_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_EQ(result.stats.unsupported_plt_texture_count, 0u);
    EXPECT_EQ(result.stats.missing_texture_source_count, 0u);
    ASSERT_EQ(asset.texture_sources.size(), 1u);
    EXPECT_EQ(asset.texture_sources[0].kind, nw::render::ModelAssetTextureSourceKind::encoded_bytes);
    EXPECT_EQ(asset.texture_sources[0].resource, nw::Resource(nw::Resref{"pmh0_head001"}, nw::ResourceType::plt));
    EXPECT_FALSE(asset.texture_sources[0].encoded_bytes.empty());
    ASSERT_EQ(asset.material_texture_sources.size(), 1u);
    EXPECT_EQ(asset.material_texture_sources[0].albedo, 0u);
    ASSERT_EQ(asset.materials.size(), 1u);
    EXPECT_TRUE(asset.materials[0].albedo_uses_plt);
    EXPECT_FALSE(asset.materials[0].plt_enabled);

    nw::render::ModelAssetTextureUploadStats decode_stats;
    const auto decoded = nw::render::decode_model_asset_texture_source_rgba8(asset.texture_sources[0], decode_stats);
    EXPECT_TRUE(decoded.valid());
    EXPECT_EQ(decode_stats.decode_failure_count, 0u);
}

TEST(RenderModelLoader, NwnModelAssetImportCountsVertexFrameRepairs)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_invalid_vertex_frame.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    ASSERT_EQ(result.asset->primitives.size(), 1u);
    ASSERT_FALSE(result.asset->primitives.front().vertices.empty());
    EXPECT_TRUE(unit_render_vec3(result.asset->primitives.front().vertices.front().normal));
    EXPECT_TRUE(unit_render_tangent(result.asset->primitives.front().vertices.front().tangent));

    EXPECT_EQ(result.stats.primitive_count, 1u);
    EXPECT_EQ(result.stats.normal_repair_count, 1u);
    EXPECT_EQ(result.stats.tangent_repair_count, 1u);
}

TEST(RenderModelLoader, ImportsCAribethDanglyPrimitivesWithRenderableSources)
{
    namespace nwn = nw::render::nwn;

    if (!nw::kernel::resman().contains({nw::Resref{"c_aribeth"}, nw::ResourceType::mdl})) {
        GTEST_SKIP() << "c_aribeth installed-game resource unavailable";
    }

    auto data = nw::kernel::resman().demand({nw::Resref{"c_aribeth"}, nw::ResourceType::mdl});
    ASSERT_GT(data.bytes.size(), 0u);

    nw::model::Mdl mdl{std::move(data)};
    ASSERT_TRUE(mdl.valid());
    ASSERT_TRUE(mdl.model.supermodel);
    ASSERT_GT(mdl.model.supermodel->model.animations.size(), 0u);

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_EQ(result.stats.secondary_motion_deformer_count, 2u);
    EXPECT_EQ(result.stats.legacy_reference_deformer_count, 0u);
    EXPECT_EQ(result.stats.missing_texture_source_count, 0u);
    EXPECT_EQ(result.stats.unsupported_plt_texture_count, 0u);
    ASSERT_EQ(asset.texture_sources.size(), 1u);
    EXPECT_EQ(asset.texture_sources.front().resource.resref, nw::Resref{"c_aribeth"});
    EXPECT_TRUE(asset.texture_sources.front().resource.type == nw::ResourceType::dds
        || asset.texture_sources.front().resource.type == nw::ResourceType::tga);
    ASSERT_EQ(asset.material_texture_sources.size(), asset.materials.size());
    for (size_t i = 0; i < asset.materials.size(); ++i) {
        EXPECT_EQ(asset.materials[i].alpha_mode, nw::render::MaterialMode::opaque);
        EXPECT_FLOAT_EQ(asset.materials[i].albedo.a, 1.0f);
        EXPECT_FLOAT_EQ(asset.materials[i].alpha_cutoff, 0.5f);
        EXPECT_FLOAT_EQ(asset.materials[i].roughness, 0.78f);
        EXPECT_FLOAT_EQ(asset.materials[i].metallic, 0.0f);
        EXPECT_GE(asset.materials[i].specular_strength, 0.0f);
        EXPECT_LE(asset.materials[i].specular_strength, 1.0f);
        EXPECT_FLOAT_EQ(asset.materials[i].emissive.x, 0.0f);
        EXPECT_FLOAT_EQ(asset.materials[i].emissive.y, 0.0f);
        EXPECT_FLOAT_EQ(asset.materials[i].emissive.z, 0.0f);
        EXPECT_FALSE(asset.materials[i].double_sided);
        EXPECT_FALSE(asset.materials[i].material_uses_fallback);
        EXPECT_EQ(asset.material_texture_sources[i].albedo, 0u);
        EXPECT_EQ(asset.material_texture_sources[i].normal, nw::render::kInvalidModelAssetTextureSourceIndex);
        EXPECT_EQ(asset.material_texture_sources[i].metallic_roughness, nw::render::kInvalidModelAssetTextureSourceIndex);
        EXPECT_EQ(asset.material_texture_sources[i].emissive, nw::render::kInvalidModelAssetTextureSourceIndex);
    }

    ASSERT_EQ(asset.skeletons.size(), 1u);
    ASSERT_FALSE(asset.animations.empty());
    const auto inherited_clip_name = std::string{mdl.model.supermodel->model.animations.front()->name.c_str()};
    EXPECT_NE(std::find_if(asset.animations.begin(), asset.animations.end(), [&](const auto& clip) {
        return clip.name == inherited_clip_name;
    }),
        asset.animations.end());

    auto pause1 = std::find_if(asset.animations.begin(), asset.animations.end(), [](const auto& clip) {
        return clip.name == "pause1";
    });
    ASSERT_NE(pause1, asset.animations.end());

    nw::render::RenderModel runtime_model;
    runtime_model.name = asset.name;
    runtime_model.nodes = asset.nodes;
    runtime_model.skeletons = asset.skeletons;
    runtime_model.animations = asset.animations;
    runtime_model.skins = asset.skins;

    nw::render::ModelInstance instance;
    instance.kind = nw::render::ModelInstanceKind::render_model;
    instance.animation.enabled = true;
    instance.animation.clip = static_cast<uint32_t>(std::distance(asset.animations.begin(), pause1));
    instance.animation.time = 0.033f;
    instance.animation.looping = true;
    instance.animation.backend = nw::render::make_render_model_animation_backend(runtime_model);
    ASSERT_TRUE(instance.animation.backend);
    ASSERT_TRUE(nw::render::sample_model_instance_animation(instance, runtime_model));

    namespace nwn = nw::render::nwn;
    nwn::ModelLoader loader{nullptr};
    auto legacy = loader.load(&mdl);
    ASSERT_TRUE(legacy);
    ASSERT_TRUE(legacy->load_animation("pause1"));
    legacy->update(33);

    size_t transformed_rigid_primitives = 0;
    size_t legacy_matching_primitives = 0;
    float max_legacy_delta = 0.0f;
    for (const auto& primitive : asset.primitives) {
        glm::mat4 sampled_world{1.0f};
        if (!nw::render::model_instance_node_world_transform(instance, primitive.node, sampled_world)) {
            continue;
        }

        const glm::mat4 static_world = instance.root_transform * primitive.transform;
        if (max_abs_matrix_delta(sampled_world, static_world) > 1.0e-4f) {
            ++transformed_rigid_primitives;
        }

        const auto* legacy_node = legacy->node_from_source_index(primitive.node);
        if (!legacy_node) {
            continue;
        }
        const glm::mat4 legacy_world = legacy_node->get_transform();
        const float legacy_delta = max_abs_matrix_delta(sampled_world, legacy_world);
        max_legacy_delta = std::max(max_legacy_delta, legacy_delta);
        if (legacy_delta <= 1.0e-4f) {
            ++legacy_matching_primitives;
        }
    }
    EXPECT_GT(transformed_rigid_primitives, 0u);
    EXPECT_LE(max_legacy_delta, 1.0e-4f);
    EXPECT_EQ(legacy_matching_primitives, asset.primitives.size());

    size_t deformed_primitive_count = 0;
    bool saw_torso_g = false;
    bool saw_head_g = false;
    for (const auto& primitive : asset.primitives) {
        if (primitive.deformer == nw::render::kInvalidModelDeformerIndex) {
            continue;
        }

        ++deformed_primitive_count;
        ASSERT_GE(primitive.node, 0);
        ASSERT_LT(static_cast<size_t>(primitive.node), mdl.model.nodes.size());
        const auto* source = dynamic_cast<const nw::model::DanglymeshNode*>(
            mdl.model.nodes[static_cast<size_t>(primitive.node)].get());
        ASSERT_NE(source, nullptr);
        const std::string source_name{source->name.c_str()};
        if (source_name == "torso_g") {
            saw_torso_g = true;
        } else if (source_name == "head_g") {
            saw_head_g = true;
        } else {
            ADD_FAILURE() << "unexpected c_aribeth dangly primitive " << source_name;
        }

        ASSERT_EQ(primitive.indices.size(), source->indices.size());
        for (size_t i = 0; i < primitive.indices.size(); ++i) {
            EXPECT_EQ(primitive.indices[i], source->indices[i]);
        }
        ASSERT_EQ(primitive.vertices.size(), source->vertices.size());
        ASSERT_LT(primitive.material, asset.material_texture_sources.size());
        EXPECT_EQ(asset.material_texture_sources[primitive.material].albedo, 0u);
        for (size_t i = 0; i < primitive.vertices.size(); ++i) {
            const auto& vertex = primitive.vertices[i];
            const auto& source_vertex = source->vertices[i];
            EXPECT_TRUE(same_vec3(vertex.position, source_vertex.position));
            EXPECT_TRUE(same_vec2(vertex.texcoord, source_vertex.tex_coords));
            if (unit_render_vec3(source_vertex.normal)) {
                EXPECT_GT(glm::dot(vertex.normal, source_vertex.normal), 0.999f);
            }
            EXPECT_TRUE(unit_render_vec3(vertex.normal));
            EXPECT_TRUE(unit_render_tangent(vertex.tangent));
            EXPECT_LE(std::abs(glm::dot(vertex.normal, glm::vec3{vertex.tangent})), 1.0e-3f);
        }
    }
    EXPECT_EQ(deformed_primitive_count, 2u);
    EXPECT_TRUE(saw_torso_g);
    EXPECT_TRUE(saw_head_g);

    const auto validation = nw::render::validate_model_asset(asset);
    EXPECT_TRUE(validation.passed());
}

TEST(RenderModelLoader, NwnRenderModelImportRejectsMissingContextAfterCpuImport)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_render_model(mdl, nw::render::ModelAssetTextureUploadDesc{});

    EXPECT_FALSE(result.model);
    EXPECT_GT(result.import_stats.primitive_count, 0u);
    EXPECT_EQ(result.geometry_upload_stats.primitive_count, result.import_stats.primitive_count);
    EXPECT_EQ(result.geometry_upload_stats.invalid_primitive_count, 0u);
    EXPECT_EQ(result.geometry_upload_stats.missing_context_count, 1u);
    EXPECT_FALSE(result.geometry_upload_stats.passed());
    EXPECT_EQ(result.texture_upload_stats.material_count, 0u);
}

TEST(RenderModelLoader, NwnRenderModelImportUsesCommonUploadWhenGraphicsAvailable)
{
    namespace nwn = nw::render::nwn;

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    nw::model::Mdl mdl{"test_data/user/development/PLC_C04.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_render_model(mdl, nw::render::ModelAssetTextureUploadDesc{
                                                        .ctx = gfx.context,
                                                    });

    ASSERT_TRUE(result.model);
    EXPECT_EQ(result.import_stats.primitive_count, 3u);
    EXPECT_EQ(result.import_stats.unsupported_specular_texture_count, 3u);
    EXPECT_TRUE(result.geometry_upload_stats.passed());
    EXPECT_TRUE(result.texture_upload_stats.passed());
    EXPECT_EQ(result.geometry_upload_stats.uploaded_primitive_count, 3u);
    EXPECT_EQ(result.texture_upload_stats.material_count, 3u);
    EXPECT_GE(result.texture_upload_stats.uploaded_texture_count, 3u);
    EXPECT_EQ(result.model->primitives.size(), 3u);
    EXPECT_EQ(result.model->materials.size(), 3u);

    destroy_render_model_test_textures(gfx.context, *result.model);
    destroy_render_model_test_buffers(*result.model);
}

TEST(RenderModelLoader, TransformAnchorUsesCachedSocketIndices)
{
    namespace nwn = nw::render::nwn;

    nwn::ModelInstance context;
    auto anchor_node = std::make_unique<nwn::Node>();
    anchor_node->has_transform_ = true;
    anchor_node->position_ = glm::vec3{2.0f, 3.0f, 4.0f};
    auto* anchor_node_ptr = anchor_node.get();
    context.nodes_.push_back(std::move(anchor_node));
    context.source_nodes_.push_back(anchor_node_ptr);
    context.sockets_.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = anchor_node_ptr->get_local_transform(),
        .bind_transform = anchor_node_ptr->bind_pose_,
        .name = "hand",
    });

    nwn::ModelInstance attached;
    attached.set_transform_anchor(&context, "hand");
    context.sockets_.front().name = "renamed_after_binding";

    const glm::mat4 transform = attached.root_transform();

    EXPECT_NEAR(transform[3].x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(transform[3].y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(transform[3].z, 4.0f, 1.0e-5f);
}

TEST(RenderModelLoader, AppliesTextureStackMtrFromTextureReference)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/PLC_C04.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    size_t mesh_count = 0;
    for (const auto& node : model->nodes_) {
        const auto* candidate = dynamic_cast<const nwn::Mesh*>(node.get());
        if (candidate) {
            ++mesh_count;
            EXPECT_EQ(candidate->bitmap_name, "bones");
            EXPECT_EQ(candidate->renderhint, "normalandspecmapped");
            EXPECT_EQ(candidate->normal_map_name, "bones_n");
            EXPECT_EQ(candidate->specular_map_name, "bones_s");
            EXPECT_EQ(candidate->roughness_map_name, "bones_r");
            EXPECT_TRUE(candidate->emissive_map_name.empty());
            EXPECT_FALSE(candidate->material_uses_fallback);
        }
    }
    EXPECT_EQ(mesh_count, 3u);
}

TEST(RenderModelLoader, FlagsMissingExplicitTextureFallback)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_missing_texture_fallback.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    const nwn::Mesh* mesh = nullptr;
    for (const auto& node : model->nodes_) {
        const auto* candidate = dynamic_cast<const nwn::Mesh*>(node.get());
        if (candidate) {
            mesh = candidate;
            break;
        }
    }

    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->bitmap_name, "zz_no_tex_fbk");
    EXPECT_TRUE(mesh->material_uses_fallback);
}

TEST(RenderModelLoader, MtrWithoutTexture0PreservesMdlDiffuse)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_preserve_diffuse.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    const nwn::Mesh* mesh = nullptr;
    for (const auto& node : model->nodes_) {
        const auto* candidate = dynamic_cast<const nwn::Mesh*>(node.get());
        if (candidate) {
            mesh = candidate;
            break;
        }
    }

    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->bitmap_name, "preserved_diffuse");
    EXPECT_EQ(mesh->renderhint, "normalandspecmapped");
    EXPECT_EQ(mesh->normal_map_name, "preserved_normal");
    EXPECT_EQ(mesh->specular_map_name, "preserved_specular");
    EXPECT_EQ(mesh->roughness_map_name, "preserved_roughness");
}

TEST(RenderModelLoader, Texture0OverridesBitmapDiffuse)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_texture0_override.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    const nwn::Mesh* mesh = nullptr;
    for (const auto& node : model->nodes_) {
        const auto* candidate = dynamic_cast<const nwn::Mesh*>(node.get());
        if (candidate) {
            mesh = candidate;
            break;
        }
    }

    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->bitmap_name, "texture0_diffuse");
}

TEST(RenderModelLoader, SelectsDanglyDeformPolicy)
{
    namespace nwn = nw::render::nwn;

    nw::model::DanglymeshNode foliage_node{"Plane84"};
    foliage_node.bitmap = "tts01_grass02";
    foliage_node.displacement = 0.2f;
    foliage_node.period = 1.0f;
    foliage_node.tightness = 1.0f;
    foliage_node.vertices = {
        make_test_vertex(glm::vec3{-1.0f, 0.0f, 0.0f}),
        make_test_vertex(glm::vec3{1.0f, 0.0f, 0.0f}),
        make_test_vertex(glm::vec3{0.0f, 0.0f, 2.0f}),
    };
    foliage_node.constraints = {0.0f, 0.0f, 255.0f};

    EXPECT_EQ(nwn::dangly_deform_policy_for(&foliage_node), nwn::DanglyDeformPolicy::foliage_sway_cpu);
    EXPECT_EQ(nwn::dangly_deform_policy_name(nwn::DanglyDeformPolicy::foliage_sway_cpu), "foliage_sway_cpu");
    EXPECT_EQ(nwn::model_deformer_kind_for(nwn::DanglyDeformPolicy::foliage_sway_cpu),
        nw::render::ModelDeformerKind::vertex_shader_sway);

    nwn::DanglyMesh foliage_mesh;
    foliage_mesh.initialize_dangly(&foliage_node);
    EXPECT_EQ(foliage_mesh.deform_policy_, nwn::DanglyDeformPolicy::foliage_sway_cpu);
    EXPECT_TRUE(foliage_mesh.two_sided_lighting);
    const auto foliage_deformer = foliage_mesh.make_deformer_record(7u);
    EXPECT_EQ(foliage_deformer.kind, nw::render::ModelDeformerKind::vertex_shader_sway);
    EXPECT_EQ(foliage_deformer.source_node_index, 7u);
    EXPECT_EQ(foliage_deformer.vertex_count, 3u);
    EXPECT_FLOAT_EQ(foliage_deformer.weight_min, 0.0f);
    EXPECT_FLOAT_EQ(foliage_deformer.weight_max, 1.0f);
    EXPECT_GT(foliage_deformer.amplitude, 0.0f);

    nw::model::DanglymeshNode palm_node{"Leaves01"};
    palm_node.bitmap = "plc_palmfrond";

    EXPECT_EQ(nwn::dangly_deform_policy_for(&palm_node), nwn::DanglyDeformPolicy::foliage_sway_cpu);

    nw::model::DanglymeshNode plant_node{"1_plant00"};
    plant_node.bitmap = "tcm02_plants";

    EXPECT_EQ(nwn::dangly_deform_policy_for(&plant_node), nwn::DanglyDeformPolicy::foliage_sway_cpu);

    nw::model::DanglymeshNode robe_node{"coat_top"};
    robe_node.bitmap = "pfh0_robe020";
    robe_node.displacement = 0.1f;
    robe_node.period = 1.0f;
    robe_node.tightness = 2.0f;
    robe_node.vertices = {
        make_test_vertex(glm::vec3{0.0f, 0.0f, 0.0f}),
        make_test_vertex(glm::vec3{0.0f, 0.0f, 1.0f}),
        make_test_vertex(glm::vec3{0.0f, 0.0f, 2.0f}),
    };
    robe_node.constraints = {0.0f, 128.0f, 255.0f};

    EXPECT_EQ(nwn::dangly_deform_policy_for(&robe_node), nwn::DanglyDeformPolicy::legacy_spring_cpu);
    EXPECT_EQ(nwn::dangly_deform_policy_name(nwn::DanglyDeformPolicy::legacy_spring_cpu), "legacy_spring_cpu");
    EXPECT_EQ(nwn::model_deformer_kind_for(nwn::DanglyDeformPolicy::legacy_spring_cpu),
        nw::render::ModelDeformerKind::secondary_motion_chain);

    nwn::DanglyMesh robe_mesh;
    robe_mesh.initialize_dangly(&robe_node);
    EXPECT_EQ(robe_mesh.deform_policy_, nwn::DanglyDeformPolicy::legacy_spring_cpu);
    EXPECT_FALSE(robe_mesh.two_sided_lighting);
    const auto robe_deformer = robe_mesh.make_deformer_record(11u);
    EXPECT_EQ(robe_deformer.kind, nw::render::ModelDeformerKind::secondary_motion_chain);
    EXPECT_EQ(robe_deformer.source_node_index, 11u);
    EXPECT_EQ(robe_deformer.vertex_count, 3u);

    nw::model::DanglymeshNode banner_node{"banner"};
    banner_node.bitmap = "dag_flag01";

    EXPECT_EQ(nwn::dangly_deform_policy_for(&banner_node), nwn::DanglyDeformPolicy::legacy_spring_cpu);

    nwn::DanglyMesh explicit_mesh;
    explicit_mesh.initialize_dangly(&robe_node, nwn::DanglyDeformPolicy::foliage_sway_cpu);
    EXPECT_EQ(explicit_mesh.deform_policy_, nwn::DanglyDeformPolicy::foliage_sway_cpu);
    EXPECT_TRUE(explicit_mesh.two_sided_lighting);

    nw::model::DanglymeshNode malformed_node{"bad_leaf"};
    malformed_node.bitmap = "plc_palmfrond";
    malformed_node.displacement = 0.2f;
    malformed_node.period = 1.0f;
    malformed_node.tightness = 1.0f;
    malformed_node.vertices = foliage_node.vertices;
    malformed_node.constraints = {255.0f};

    nwn::DanglyMesh malformed_mesh;
    malformed_mesh.initialize_dangly(&malformed_node);
    const auto malformed_deformer = malformed_mesh.make_deformer_record(12u);
    EXPECT_EQ(malformed_deformer.kind, nw::render::ModelDeformerKind::legacy_reference_cpu);
}

TEST(RenderModelLoader, FoliageDanglyMotionUsesNwnScale)
{
    namespace nwn = nw::render::nwn;

    nwn::set_dangly_debug_scale(1.0f);
    nwn::set_dangly_mode(nwn::DanglyMode::legacy);

    nw::model::DanglymeshNode node{"leaf_card"};
    node.bitmap = "plc_palmfrond";
    node.period = 1.0f;
    node.tightness = 1.0f;
    node.displacement = 0.2f;
    node.vertices = {
        make_test_vertex(glm::vec3{-1.0f, 0.0f, 0.0f}),
        make_test_vertex(glm::vec3{1.0f, 0.0f, 0.0f}),
        make_test_vertex(glm::vec3{0.0f, 0.0f, 2.0f}),
    };
    node.indices = {0, 1, 2};
    node.constraints = {255.0f, 255.0f, 255.0f};

    nwn::DanglyMesh mesh;
    mesh.initialize_dangly(&node);
    ASSERT_EQ(mesh.deform_policy_, nwn::DanglyDeformPolicy::foliage_sway_cpu);
    ASSERT_EQ(mesh.cpu_vertices_.size(), node.vertices.size());

    mesh.update_dangly(glm::mat4{1.0f}, 1000);

    float max_delta = 0.0f;
    for (size_t i = 0; i < mesh.cpu_vertices_.size(); ++i) {
        max_delta = std::max(max_delta, glm::length(mesh.cpu_vertices_[i].position - node.vertices[i].position));
    }

    EXPECT_GT(max_delta, 0.0001f);
    EXPECT_LT(max_delta, 0.08f);
    EXPECT_LE(mesh.foliage_amplitude_, 0.035f);
}

TEST(RenderModelLoader, FoliageDanglyMeshesDoNotCastShadows)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/plc_palm02.mdl"};
    ASSERT_TRUE(mdl.valid());

    nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);
    ASSERT_FALSE(model->shadow_casters_.empty());

    bool saw_foliage_mesh = false;
    size_t dangly_deformer_count = 0;
    bool saw_non_foliage_caster = false;
    ASSERT_FALSE(model->deformers().empty());
    for (const auto& node : model->nodes_) {
        const auto* dangly = dynamic_cast<const nwn::DanglyMesh*>(node.get());
        if (dangly && !dangly->rest_positions_.empty()) {
            ASSERT_NE(dangly->deformer_index_, nw::render::kInvalidModelDeformerIndex);
            const auto* deformer = model->deformer(dangly->deformer_index_);
            ASSERT_NE(deformer, nullptr);
            EXPECT_EQ(model->deformer_node(dangly->deformer_index_), static_cast<const nwn::Node*>(dangly));
            EXPECT_EQ(deformer->vertex_count, dangly->rest_positions_.size());
            if (dangly->uses_foliage_sway() && dangly->constraints_valid_) {
                EXPECT_EQ(deformer->kind, nw::render::ModelDeformerKind::vertex_shader_sway);
            }
            ++dangly_deformer_count;
        }
        if (dangly && dangly->uses_foliage_sway()) {
            saw_foliage_mesh = true;
            EXPECT_TRUE(dangly->two_sided_lighting);
        }
    }

    for (const auto* mesh : model->shadow_casters_) {
        ASSERT_NE(mesh, nullptr);
        const auto* source = dynamic_cast<const nw::model::TrimeshNode*>(mesh->orig_);
        ASSERT_NE(source, nullptr);
        EXPECT_TRUE(source->shadow);
        if (const auto* dangly = dynamic_cast<const nwn::DanglyMesh*>(mesh)) {
            EXPECT_FALSE(dangly->uses_foliage_sway());
        } else {
            saw_non_foliage_caster = true;
        }
    }

    EXPECT_TRUE(saw_foliage_mesh);
    EXPECT_GT(dangly_deformer_count, 0u);
    EXPECT_EQ(model->deformer(9999u), nullptr);
    EXPECT_EQ(model->deformer_node(9999u), nullptr);
    EXPECT_TRUE(saw_non_foliage_caster);
}

TEST(RenderModelLoader, ImportsNwnDanglyModelAssetDeformers)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/plc_palm02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    ASSERT_FALSE(asset.deformers.empty());
    EXPECT_EQ(result.stats.deformer_count, asset.deformers.size());
    bool saw_foliage_sway = false;
    for (const auto& deformer : asset.deformers) {
        ASSERT_LT(deformer.source_node_index, asset.nodes.size());
        EXPECT_GT(deformer.vertex_count, 0u);
        if (deformer.kind == nw::render::ModelDeformerKind::vertex_shader_sway) {
            saw_foliage_sway = true;
            EXPECT_GT(deformer.amplitude, 0.0f);
        }
    }
    EXPECT_TRUE(saw_foliage_sway);

    size_t deformed_primitive_count = 0;
    for (const auto& primitive : asset.primitives) {
        if (primitive.deformer == nw::render::kInvalidModelDeformerIndex) {
            continue;
        }
        ++deformed_primitive_count;
        for (const auto& vertex : primitive.vertices) {
            EXPECT_TRUE(valid_render_vec3(vertex.normal));
            EXPECT_TRUE(valid_render_tangent(vertex.tangent));
        }
    }
    EXPECT_EQ(deformed_primitive_count, asset.deformers.size());

    const auto validation = nw::render::validate_model_asset(asset);
    EXPECT_TRUE(validation.passed());
}

TEST(RenderModelLoader, ModelTransformCacheRefreshesAfterDirectNodeEdits)
{
    namespace nwn = nw::render::nwn;

    nwn::Node root;
    root.has_transform_ = true;
    root.position_ = glm::vec3{1.0f, 0.0f, 0.0f};

    nwn::Mesh mesh;
    mesh.parent_ = &root;
    mesh.has_transform_ = true;
    mesh.position_ = glm::vec3{0.0f, 2.0f, 0.0f};
    root.children_.push_back(&mesh);

    const glm::mat4 identity{1.0f};
    const auto& root_transform = root.refresh_model_transform_cache(identity, 0);
    glm::mat4 mesh_transform = mesh.refresh_model_transform_cache(root_transform, root.model_transform_cache_revision());
    const uint64_t first_revision = mesh.model_transform_cache_revision();

    EXPECT_FLOAT_EQ(mesh_transform[3].x, 1.0f);
    EXPECT_FLOAT_EQ(mesh_transform[3].y, 2.0f);

    mesh.refresh_model_transform_cache(root_transform, root.model_transform_cache_revision());
    EXPECT_EQ(mesh.model_transform_cache_revision(), first_revision);

    mesh.position_ = glm::vec3{0.0f, 3.0f, 0.0f};
    mesh_transform = mesh.refresh_model_transform_cache(root_transform, root.model_transform_cache_revision());
    const uint64_t local_edit_revision = mesh.model_transform_cache_revision();

    EXPECT_NE(local_edit_revision, first_revision);
    EXPECT_FLOAT_EQ(mesh_transform[3].x, 1.0f);
    EXPECT_FLOAT_EQ(mesh_transform[3].y, 3.0f);

    root.position_ = glm::vec3{2.0f, 0.0f, 0.0f};
    const auto& moved_root_transform = root.refresh_model_transform_cache(identity, 0);
    mesh_transform = mesh.refresh_model_transform_cache(moved_root_transform, root.model_transform_cache_revision());

    EXPECT_NE(mesh.model_transform_cache_revision(), local_edit_revision);
    EXPECT_FLOAT_EQ(mesh_transform[3].x, 2.0f);
    EXPECT_FLOAT_EQ(mesh_transform[3].y, 3.0f);
}

TEST(RenderModelLoader, NormalMatrixCachesRefreshForRootRevision)
{
    namespace nwn = nw::render::nwn;

    nwn::ModelInstance model;
    const glm::mat4 first_root = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f, 3.0f, 4.0f});
    const uint64_t first_revision = model.root_transform_cache_revision(first_root);
    glm::mat4 normal = model.refresh_root_normal_matrix_cache(first_root, first_revision);

    EXPECT_FLOAT_EQ(normal[0][0], 0.5f);
    EXPECT_FLOAT_EQ(normal[1][1], 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(normal[2][2], 0.25f);
    EXPECT_EQ(model.root_transform_cache_revision(first_root), first_revision);

    const glm::mat4 second_root = glm::scale(glm::mat4{1.0f}, glm::vec3{4.0f, 3.0f, 4.0f});
    const uint64_t second_revision = model.root_transform_cache_revision(second_root);
    normal = model.refresh_root_normal_matrix_cache(second_root, second_revision);

    EXPECT_NE(second_revision, first_revision);
    EXPECT_FLOAT_EQ(normal[0][0], 0.25f);

    nwn::Mesh mesh;
    normal = mesh.refresh_normal_matrix_cache(first_root, 1, first_revision);
    EXPECT_FLOAT_EQ(normal[0][0], 0.5f);

    normal = mesh.refresh_normal_matrix_cache(second_root, 1, second_revision);
    EXPECT_FLOAT_EQ(normal[0][0], 0.25f);
}
