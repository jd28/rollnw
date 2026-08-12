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

TEST(RenderModelLoader, ImportsNwnIdentitySkinBoneRows)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/c_satyr.mdl"};
    ASSERT_TRUE(mdl.valid());

    size_t source_node_index = 0;
    nw::model::SkinNode* source_skin = nullptr;
    for (; source_node_index < mdl.model.nodes.size(); ++source_node_index) {
        source_skin = dynamic_cast<nw::model::SkinNode*>(mdl.model.nodes[source_node_index].get());
        if (source_skin && source_skin->render && !source_skin->vertices.empty()
            && !source_skin->indices.empty()) {
            break;
        }
    }
    ASSERT_NE(source_skin, nullptr);
    ASSERT_LT(source_node_index, mdl.model.nodes.size());

    constexpr int32_t identity_source_slot = 17;
    source_skin->bone_nodes[identity_source_slot] = -1;
    source_skin->vertices[0].bones = glm::ivec4{identity_source_slot, -1, -1, -1};
    source_skin->vertices[0].weights = glm::vec4{1.0f, 0.0f, 0.0f, 0.0f};

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    EXPECT_EQ(result.stats.skipped_skin_mesh_count, 0u);

    const auto primitive = std::find_if(
        result.asset->primitives.begin(), result.asset->primitives.end(), [&](const auto& candidate) {
            return candidate.skinned && candidate.node == static_cast<int32_t>(source_node_index);
        });
    ASSERT_NE(primitive, result.asset->primitives.end());
    ASSERT_LT(primitive->skin, result.asset->skins.size());
    const auto& skin = result.asset->skins[primitive->skin];
    const auto identity_joint = std::find(
        skin.joints.begin(), skin.joints.end(), nw::render::kModelSkinIdentityJoint);
    ASSERT_NE(identity_joint, skin.joints.end());
    const auto identity_joint_index = static_cast<uint32_t>(std::distance(skin.joints.begin(), identity_joint));
    ASSERT_FALSE(primitive->skinned_vertices.empty());
    EXPECT_EQ(packed_u8_lane(primitive->skinned_vertices[0].joint_indices, 0), identity_joint_index);
    EXPECT_EQ(packed_u8_lane(primitive->skinned_vertices[0].joint_weights, 0), 255u);
    EXPECT_TRUE(nw::render::validate_model_asset(*result.asset).passed());
}

TEST(RenderModelLoader, ResourceCacheStatsTrackMtrAndClear)
{
    namespace nwn = nw::render::nwn;

    nwn::clear_model_loader_resource_caches();
    EXPECT_TRUE(nwn::model_loader_resource_cache_stats().empty());

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);

    const auto stats = nwn::model_loader_resource_cache_stats();
    EXPECT_GE(stats.mtr_material_count, 1u);

    nwn::clear_model_loader_resource_caches();
    EXPECT_TRUE(nwn::model_loader_resource_cache_stats().empty());
}

TEST(RenderModelLoader, DerivesCanonicalHumanoidPaletteResref)
{
    namespace nwn = nw::render::nwn;

    EXPECT_EQ(nwn::nwn_humanoid_palette_resref("pmo2_belt151"), "pmh0_belt151");
    EXPECT_EQ(nwn::nwn_humanoid_palette_resref("pfe16_chest001"), "pfh0_chest001");
    EXPECT_EQ(nwn::nwn_humanoid_palette_resref("pmh0_chest001"), "pmh0_chest001");
}

TEST(RenderModelLoader, RejectsInvalidHumanoidPaletteIdentity)
{
    namespace nwn = nw::render::nwn;

    EXPECT_TRUE(nwn::nwn_humanoid_palette_resref("pmo2belt151").empty());
    EXPECT_TRUE(nwn::nwn_humanoid_palette_resref("pmoX_belt151").empty());
    EXPECT_TRUE(nwn::nwn_humanoid_palette_resref("c_belt151").empty());
}

TEST(RenderModelLoader, ResolvesMissingHumanoidAlbedoToCanonicalPlt)
{
    namespace nwn = nw::render::nwn;

    EXPECT_EQ(
        nwn::resolve_nwn_model_albedo_resref("pme2_testplt", "pme2_testplt"),
        "pmh0_testplt");
    EXPECT_EQ(
        nwn::resolve_nwn_model_albedo_resref("pmh0_testplt", "pmh0_testplt"),
        "pmh0_testplt");
    EXPECT_EQ(
        nwn::resolve_nwn_model_albedo_resref("not_a_humanoid_part", "missing_albedo"),
        "missing_albedo");
}

TEST(RenderModelLoader, PrefersSelectedHumanoidBodyPartPaletteOverSharedBitmap)
{
    namespace nwn = nw::render::nwn;

    const std::array body_parts{
        nw::Resref{"pmh0_handl001"},
        nw::Resref{"pmh0_legl003"},
    };
    for (const auto body_part : body_parts) {
        const nw::Resource model_resource{body_part, nw::ResourceType::mdl};
        const nw::Resource palette_resource{body_part, nw::ResourceType::plt};
        if (!nw::kernel::resman().contains(model_resource)
            || !nw::kernel::resman().contains(palette_resource)) {
            GTEST_SKIP() << "installed humanoid body-part resources unavailable";
        }

        nw::model::Mdl mdl{nw::kernel::resman().demand(model_resource)};
        ASSERT_TRUE(mdl.valid());

        auto result = nwn::import_nwn_model_asset(mdl);
        ASSERT_TRUE(result.asset);
        ASSERT_EQ(result.asset->materials.size(), 1u);
        ASSERT_EQ(result.asset->material_texture_sources.size(), 1u);
        const auto source_index = result.asset->material_texture_sources.front().albedo;
        ASSERT_LT(source_index, result.asset->texture_sources.size());
        EXPECT_TRUE(result.asset->materials.front().albedo_uses_plt);
        EXPECT_EQ(result.asset->texture_sources[source_index].resource, palette_resource);
    }
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

TEST(RenderModelLoader, TilePlanarQuadGradedAlphaUsesTransparency)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_overlay"};
    node.bitmap = "snow_overlay";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::graded,
        .is_planar_quad = true,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::transparent);
}

TEST(RenderModelLoader, TilePlanarQuadMostlyBinaryAlphaUsesCutout)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_overlay"};
    node.bitmap = "cutout_overlay";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::mostly_binary,
        .is_planar_quad = true,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::cutout);
}

TEST(RenderModelLoader, TileMostlyBinaryAlphaWithoutExplicitHintUsesCutout)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_mesh"};
    node.bitmap = "cutout_mesh";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::mostly_binary,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::cutout);
}

TEST(RenderModelLoader, CharacterMostlyBinaryAlphaUsesCutout)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"character_mesh"};
    node.bitmap = "character_cutout";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::character,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::mostly_binary,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::cutout);
}

TEST(RenderModelLoader, CharacterContinuousGradedAlphaStaysOpaque)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"character_mesh"};
    node.bitmap = "character_smooth_alpha";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::character,
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

TEST(RenderModelLoader, TileGradedAlphaUsesAuthoredAlphaMean)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_overlay"};
    node.bitmap = "alpha_overlay";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::graded,
        .has_txi = true,
        .txi_has_alphamean = true,
        .txi_alphamean = 0.233f,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::transparent);
}

TEST(RenderModelLoader, TileGradedAlphaRejectsInvalidAuthoredAlphaMean)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_mesh"};
    node.bitmap = "alpha_noise";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::graded,
        .has_txi = true,
        .txi_has_alphamean = true,
        .txi_alphamean = 1.0f,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::opaque);
}

TEST(RenderModelLoader, TileMostlyBinaryAlphaUsesExplicitTransparencyHint)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"tile_overlay"};
    node.bitmap = "cutout_overlay";
    node.transparencyhint = 1;

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::tile,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::mostly_binary,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::cutout);
}

TEST(RenderModelLoader, CharacterOpaqueTextureStaysOpaque)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"character_mesh"};
    node.bitmap = "opaque_diffuse";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::character,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::opaque,
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::opaque);
}

TEST(RenderModelLoader, OpaqueTextureWithLightenTxiUsesCutout)
{
    namespace nwn = nw::render::nwn;

    nw::model::TrimeshNode node{"lighten_mesh"};
    node.bitmap = "lighten_diffuse";

    const auto mode = nwn::classify_nwn_material(nwn::NwnMaterialClassificationInput{
        .node = &node,
        .bitmap_name = node.bitmap,
        .model_class = nw::model::ModelClass::character,
        .alpha_profile = nwn::NwnMaterialAlphaProfile::opaque,
        .has_txi = true,
        .txi_blending = "lighten",
    });

    EXPECT_EQ(mode, nw::render::MaterialMode::cutout);
}

TEST(RenderModelLoader, ImportsStaticNwnModelAssetPayloads)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    EXPECT_EQ(asset.source_kind, nw::render::ModelAssetSourceKind::nwn);
    EXPECT_EQ(asset.name, mdl.model.name);
    EXPECT_EQ(result.stats.source_node_count, mdl.model.nodes.size());
    EXPECT_GT(result.stats.primitive_count, 0u);
    EXPECT_EQ(result.stats.primitive_count, asset.primitives.size());
    EXPECT_EQ(result.stats.material_count, asset.materials.size());
    EXPECT_EQ(asset.material_texture_sources.size(), asset.materials.size());
    for (const auto& sources : asset.material_texture_sources) {
        EXPECT_FALSE(sources.albedo_srgb);
    }
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
    EXPECT_GT(asset.bounds.radius(), 0.0f);

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

// NWN node names are case-insensitive and the stock humanoid rigs mix casing
// (pmh0 carries Lbicep_g beside lforearm_g/lhand_g). Binding clip tracks by
// exact name drops the track for any node whose casing differs from the
// animation source, freezing that joint at bind pose -- which is what left the
// assembled creature's hands unrotated while every other part animated.
TEST(RenderModelLoader, ClipTracksBindToJointsCaseInsensitively)
{
    namespace nwn = nw::render::nwn;

    const nw::Resource rig{nw::Resref{"pmh0"}, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(rig)) {
        GTEST_SKIP() << "installed humanoid base rig unavailable";
    }

    nw::model::Mdl mdl{nw::kernel::resman().demand(rig)};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    ASSERT_EQ(result.asset->skeletons.size(), 1u);
    ASSERT_FALSE(result.asset->animations.empty());

    const auto& skeleton = result.asset->skeletons.front();
    const auto joint_index = [&](std::string_view name) -> size_t {
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            if (nw::string::icmp(skeleton.joints[i].name, name)) {
                return i;
            }
        }
        return skeleton.joints.size();
    };

    // Mixed-case joints must still receive keys from the supermodel clips.
    for (const auto* name : {"lhand_g", "rhand_g", "lbicep_g", "rbicep_g"}) {
        const size_t joint = joint_index(name);
        ASSERT_LT(joint, skeleton.joints.size()) << "missing joint " << name;

        const bool any_track_keyed = std::any_of(
            result.asset->animations.begin(),
            result.asset->animations.end(),
            [&](const auto& clip) {
                return joint < clip.tracks.size()
                    && (!clip.tracks[joint].rotations.empty()
                        || !clip.tracks[joint].translations.empty());
            });
        EXPECT_TRUE(any_track_keyed) << "no clip keys joint " << name;
    }
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

TEST(RenderModelLoader, ScalesInheritedNwnAnimationTranslations)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/c_satyr.mdl"};
    ASSERT_TRUE(mdl.valid());
    ASSERT_TRUE(mdl.model.supermodel);
    ASSERT_GT(mdl.model.animationscale, 1.0f);

    const auto* source_animation = mdl.model.supermodel->model.find_animation("walk");
    ASSERT_NE(source_animation, nullptr);
    const auto source_node = std::find_if(
        source_animation->nodes.begin(), source_animation->nodes.end(), [](const auto& candidate) {
            return candidate && candidate->name == "rootdummy";
        });
    ASSERT_NE(source_node, source_animation->nodes.end());
    const auto source_positions = (*source_node)->get_controller(nw::model::ControllerType::Position, true);
    ASSERT_FALSE(source_positions.time.empty());
    ASSERT_GE(source_positions.data.size(), 3u);

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;
    ASSERT_EQ(asset.skeletons.size(), 1u);

    const auto clip = std::find_if(asset.animations.begin(), asset.animations.end(), [](const auto& candidate) {
        return candidate.name == "walk";
    });
    ASSERT_NE(clip, asset.animations.end());

    const auto& skeleton = asset.skeletons.front();
    const auto joint = std::find_if(skeleton.joints.begin(), skeleton.joints.end(), [](const auto& candidate) {
        return candidate.name == "rootdummy";
    });
    ASSERT_NE(joint, skeleton.joints.end());
    const auto joint_index = static_cast<size_t>(std::distance(skeleton.joints.begin(), joint));
    ASSERT_LT(joint_index, clip->tracks.size());
    ASSERT_FALSE(clip->tracks[joint_index].translations.empty());

    const glm::vec3 source_translation{
        source_positions.data[0], source_positions.data[1], source_positions.data[2]};
    EXPECT_TRUE(same_vec3(
        clip->tracks[joint_index].translations.front().value,
        source_translation * mdl.model.animationscale));
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
        EXPECT_EQ(material.lighting_model, nw::render::MaterialLightingModel::nwn_diffuse);
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

TEST(RenderModelLoader, NwnModelAssetImportCountsWaterNamePolicy)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_water_name_policy.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    ASSERT_EQ(result.asset->materials.size(), 1u);
    EXPECT_EQ(result.stats.water_name_heuristic_count, 1u);
    EXPECT_EQ(result.stats.foliage_name_heuristic_count, 0u);
    EXPECT_EQ(result.asset->materials[0].alpha_mode, nw::render::MaterialMode::water);
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
    EXPECT_EQ(result.stats.unsupported_deformer_count, 0u);
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
    instance.animation.enabled = true;
    instance.animation.clip = static_cast<uint32_t>(std::distance(asset.animations.begin(), pause1));
    instance.animation.time = 0.033f;
    instance.animation.looping = true;
    instance.animation.backend = nw::render::make_render_model_animation_backend(runtime_model);
    ASSERT_TRUE(instance.animation.backend);
    ASSERT_TRUE(nw::render::sample_model_instance_animation(instance, runtime_model));

    size_t transformed_rigid_primitives = 0;
    for (const auto& primitive : asset.primitives) {
        glm::mat4 sampled_world{1.0f};
        if (!nw::render::model_instance_node_world_transform(instance, primitive.node, sampled_world)) {
            continue;
        }

        const glm::mat4 static_world = instance.root_transform * primitive.transform;
        if (max_abs_matrix_delta(sampled_world, static_world) > 1.0e-4f) {
            ++transformed_rigid_primitives;
        }
    }
    EXPECT_GT(transformed_rigid_primitives, 0u);

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

TEST(RenderModelLoader, ImportsNwnSocketCarrierWithoutGpuWork)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_socket_carrier.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_render_model(mdl, nw::render::ModelAssetTextureUploadDesc{});

    ASSERT_TRUE(result.model);
    EXPECT_TRUE(result.geometry_upload_stats.passed());
    EXPECT_TRUE(result.texture_upload_stats.passed());
    EXPECT_EQ(result.import_stats.primitive_count, 0u);
    EXPECT_EQ(result.geometry_upload_stats.primitive_count, 0u);
    EXPECT_EQ(result.geometry_upload_stats.uploaded_primitive_count, 0u);
    EXPECT_EQ(result.geometry_upload_stats.missing_context_count, 0u);
    EXPECT_TRUE(result.model->primitives.empty());
    EXPECT_EQ(result.model->socket_index("tail"), 1u);
    EXPECT_EQ(result.model->socket_index("wings"), 2u);
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

TEST(RenderModelLoader, FlagsMissingExplicitTextureFallback)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_missing_texture_fallback.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    ASSERT_EQ(result.asset->materials.size(), 1u);
    EXPECT_TRUE(result.asset->materials.front().material_uses_fallback);
    EXPECT_GT(result.stats.missing_texture_source_count, 0u);
}

TEST(RenderModelLoader, DropsNonRenderedTileMeshesWithoutSourceImagery)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/test_tile_render_zero_visibility.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    EXPECT_EQ(result.asset->primitives.size(), 2u);
    for (const auto& primitive : result.asset->primitives) {
        ASSERT_GE(primitive.node, 0);
        ASSERT_LT(static_cast<size_t>(primitive.node), mdl.model.nodes.size());
        const auto* source = dynamic_cast<const nw::model::TrimeshNode*>(
            mdl.model.nodes[static_cast<size_t>(primitive.node)].get());
        ASSERT_NE(source, nullptr);
        EXPECT_NE(source->bitmap, "zz_hidden_missing");
    }
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

    EXPECT_EQ(nwn::dangly_deform_policy_for(&foliage_node), nwn::DanglyDeformPolicy::foliage_sway);
    EXPECT_EQ(nwn::dangly_deform_policy_name(nwn::DanglyDeformPolicy::foliage_sway), "foliage_sway");
    EXPECT_EQ(nwn::model_deformer_kind_for(nwn::DanglyDeformPolicy::foliage_sway),
        nw::render::ModelDeformerKind::vertex_shader_sway);

    nw::model::DanglymeshNode palm_node{"Leaves01"};
    palm_node.bitmap = "plc_palmfrond";

    EXPECT_EQ(nwn::dangly_deform_policy_for(&palm_node), nwn::DanglyDeformPolicy::foliage_sway);

    nw::model::DanglymeshNode plant_node{"1_plant00"};
    plant_node.bitmap = "tcm02_plants";

    EXPECT_EQ(nwn::dangly_deform_policy_for(&plant_node), nwn::DanglyDeformPolicy::foliage_sway);

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

    EXPECT_EQ(nwn::dangly_deform_policy_for(&robe_node), nwn::DanglyDeformPolicy::secondary_motion_chain);
    EXPECT_EQ(nwn::dangly_deform_policy_name(nwn::DanglyDeformPolicy::secondary_motion_chain), "secondary_motion_chain");
    EXPECT_EQ(nwn::model_deformer_kind_for(nwn::DanglyDeformPolicy::secondary_motion_chain),
        nw::render::ModelDeformerKind::secondary_motion_chain);

    nw::model::DanglymeshNode banner_node{"banner"};
    banner_node.bitmap = "dag_flag01";

    EXPECT_EQ(nwn::dangly_deform_policy_for(&banner_node), nwn::DanglyDeformPolicy::secondary_motion_chain);
}

TEST(RenderModelLoader, FoliageDanglyMeshesDoNotCastShadows)
{
    namespace nwn = nw::render::nwn;

    nw::model::Mdl mdl{"test_data/user/development/plc_palm02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;
    ASSERT_FALSE(asset.deformers.empty());

    bool saw_foliage_mesh = false;
    bool saw_non_foliage_caster = false;
    for (const auto& primitive : asset.primitives) {
        if (primitive.deformer != nw::render::kInvalidModelDeformerIndex) {
            ASSERT_LT(primitive.deformer, asset.deformers.size());
            const auto& deformer = asset.deformers[primitive.deformer];
            if (deformer.kind == nw::render::ModelDeformerKind::vertex_shader_sway) {
                ASSERT_LT(primitive.material, asset.materials.size());
                EXPECT_TRUE(asset.materials[primitive.material].double_sided);
                EXPECT_FALSE(primitive.casts_shadow);
                saw_foliage_mesh = true;
            }
        }
        if (primitive.casts_shadow) {
            saw_non_foliage_caster = true;
        }
    }

    for (const auto& deformer : asset.deformers) {
        if (deformer.kind == nw::render::ModelDeformerKind::vertex_shader_sway) {
            saw_foliage_mesh = true;
        }
    }

    EXPECT_TRUE(saw_foliage_mesh);
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
    EXPECT_GT(result.stats.foliage_name_heuristic_count, 0u);
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

TEST(RenderModelLoader, ImportsPalmFoliageLeavesAsCutoutAlpha)
{
    namespace nwn = nw::render::nwn;

    const bool has_frond_texture = nw::kernel::resman().contains({nw::Resref{"plc_palmfrond"}, nw::ResourceType::dds})
        || nw::kernel::resman().contains({nw::Resref{"plc_palmfrond"}, nw::ResourceType::tga});
    if (!has_frond_texture) {
        GTEST_SKIP() << "plc_palmfrond texture unavailable";
    }

    nw::model::Mdl mdl{"test_data/user/development/plc_palm02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nwn::import_nwn_model_asset(mdl);
    ASSERT_TRUE(result.asset);
    const auto& asset = *result.asset;

    size_t foliage_primitive_count = 0;
    for (const auto& primitive : asset.primitives) {
        if (primitive.deformer == nw::render::kInvalidModelDeformerIndex) {
            continue;
        }
        ++foliage_primitive_count;

        ASSERT_LT(primitive.material, asset.materials.size());
        const auto& material = asset.materials[primitive.material];
        EXPECT_EQ(material.alpha_mode, nw::render::MaterialMode::cutout);
        EXPECT_GE(material.alpha_cutoff, 0.5f);

        ASSERT_LT(primitive.material, asset.material_texture_sources.size());
        const auto albedo_source = asset.material_texture_sources[primitive.material].albedo;
        ASSERT_NE(albedo_source, nw::render::kInvalidModelAssetTextureSourceIndex);
        ASSERT_LT(albedo_source, asset.texture_sources.size());
        EXPECT_EQ(asset.texture_sources[albedo_source].resource.resref, nw::Resref{"plc_palmfrond"});

        nw::render::ModelAssetTextureUploadStats decode_stats;
        const auto decoded = nw::render::decode_model_asset_texture_source_rgba8(
            asset.texture_sources[albedo_source], decode_stats);
        ASSERT_TRUE(decoded.valid());
        EXPECT_EQ(decode_stats.decode_failure_count, 0u);

        bool saw_transparent_pixel = false;
        bool saw_opaque_pixel = false;
        for (size_t i = 3; i < decoded.pixels.size(); i += 4) {
            const auto alpha = decoded.pixels[i];
            saw_transparent_pixel |= alpha == 0;
            saw_opaque_pixel |= alpha == 255;
        }
        EXPECT_TRUE(saw_transparent_pixel);
        EXPECT_TRUE(saw_opaque_pixel);
    }

    EXPECT_GT(foliage_primitive_count, 0u);
}
