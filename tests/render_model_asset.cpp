#include <nw/formats/Plt.hpp>
#include <nw/render/model_asset.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

nw::render::ModelAsset make_minimal_valid_asset()
{
    nw::render::ModelAsset asset;
    asset.materials.push_back(nw::render::Material{});
    asset.nodes.push_back(nw::render::Node{});

    nw::render::ModelAssetPrimitive primitive;
    primitive.vertices = {nw::render::Vertex{}, nw::render::Vertex{}, nw::render::Vertex{}};
    primitive.indices = {0u, 1u, 2u};
    primitive.material = 0u;
    primitive.node = 0;
    asset.primitives.push_back(std::move(primitive));
    return asset;
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

std::vector<uint8_t> make_test_tga_1x2(uint8_t descriptor, std::array<uint8_t, 3> first_row_rgb,
    std::array<uint8_t, 3> second_row_rgb)
{
    std::vector<uint8_t> bytes(18 + 2 * 3);
    bytes[2] = 2; // Uncompressed true-color image.
    bytes[12] = 1;
    bytes[14] = 2;
    bytes[16] = 24;
    bytes[17] = descriptor;

    auto write_bgr = [&bytes](size_t offset, std::array<uint8_t, 3> rgb) {
        bytes[offset + 0] = rgb[2];
        bytes[offset + 1] = rgb[1];
        bytes[offset + 2] = rgb[0];
    };
    write_bgr(18, first_row_rgb);
    write_bgr(21, second_row_rgb);
    return bytes;
}

nw::render::ModelAssetTextureSource make_encoded_tga_source(std::vector<uint8_t> bytes)
{
    return nw::render::ModelAssetTextureSource{
        .kind = nw::render::ModelAssetTextureSourceKind::encoded_bytes,
        .resource = nw::Resource{nw::Resref{"rows"}, nw::ResourceType::tga},
        .path = {},
        .encoded_bytes = std::move(bytes),
    };
}

std::vector<uint8_t> make_test_plt_1x2(uint8_t first_color, uint8_t first_layer,
    uint8_t second_color, uint8_t second_layer)
{
    std::vector<uint8_t> bytes(24 + 2 * 2, 0);
    const char header[] = "PLT V1  ";
    std::copy(header, header + 8, bytes.begin());
    bytes[16] = 1;
    bytes[20] = 2;
    bytes[24] = first_color;
    bytes[25] = first_layer;
    bytes[26] = second_color;
    bytes[27] = second_layer;
    return bytes;
}

nw::render::ModelAssetTextureSource make_encoded_plt_source(std::vector<uint8_t> bytes)
{
    return nw::render::ModelAssetTextureSource{
        .kind = nw::render::ModelAssetTextureSourceKind::encoded_bytes,
        .resource = nw::Resource{nw::Resref{"rows"}, nw::ResourceType::plt},
        .path = {},
        .encoded_bytes = std::move(bytes),
    };
}

void expect_rgba_pixel(const nw::render::ModelAssetDecodedTexture& texture, size_t row,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const size_t offset = row * 4;
    ASSERT_GE(texture.pixels.size(), offset + 4);
    EXPECT_EQ(texture.pixels[offset + 0], r);
    EXPECT_EQ(texture.pixels[offset + 1], g);
    EXPECT_EQ(texture.pixels[offset + 2], b);
    EXPECT_EQ(texture.pixels[offset + 3], a);
}

TEST(RenderModelAsset, PrimitiveCountsStaticAndSkinnedPayloads)
{
    nw::render::ModelAssetPrimitive primitive;
    primitive.vertices.resize(3);
    primitive.indices = {0u, 1u, 2u};

    EXPECT_FALSE(primitive.uses_skinned_vertices());
    EXPECT_EQ(primitive.vertex_count(), 3u);
    EXPECT_EQ(primitive.index_count(), 3u);

    primitive.skinned = true;
    primitive.skinned_vertices.resize(4);
    primitive.indices = {0u, 1u, 2u, 2u, 3u, 0u};

    EXPECT_TRUE(primitive.uses_skinned_vertices());
    EXPECT_EQ(primitive.vertex_count(), 4u);
    EXPECT_EQ(primitive.index_count(), 6u);
}

TEST(RenderModelAsset, OwnsSourceAgnosticModelPayloads)
{
    nw::render::ModelAsset asset;
    asset.source_kind = nw::render::ModelAssetSourceKind::nwn_legacy;
    asset.name = "c_legacy_creature";
    asset.materials.push_back(nw::render::Material{});
    asset.texture_sources.push_back(nw::render::ModelAssetTextureSource{
        .kind = nw::render::ModelAssetTextureSourceKind::external_file,
        .resource = {},
        .path = "textures/c_legacy_creature_d.png",
        .encoded_bytes = {},
    });
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 0u,
    });
    asset.nodes.push_back(nw::render::Node{});
    asset.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "hand_r",
    });
    asset.deformers.push_back(nw::render::ModelDeformer{
        .kind = nw::render::ModelDeformerKind::vertex_shader_sway,
        .source_node_index = 0u,
        .vertex_count = 12u,
    });
    nw::render::ModelAssetPrimitive primitive;
    primitive.vertices = {nw::render::Vertex{}, nw::render::Vertex{}, nw::render::Vertex{}};
    primitive.indices = {0u, 1u, 2u};
    primitive.material = 0u;
    primitive.deformer = 0u;
    primitive.node = 0;
    asset.primitives.push_back(std::move(primitive));

    nw::render::ParticleEffectDef effect;
    effect.emitters.push_back(nw::render::ParticleEmitterDef{});
    asset.particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "impact",
        .animation_name = "cast",
        .effect = std::move(effect),
        .effect_events = {{.time = 0.25f, .burst_count = 2u}},
        .animation_length = 1.0f,
    });

    EXPECT_FALSE(asset.empty());
    EXPECT_EQ(asset.source_kind, nw::render::ModelAssetSourceKind::nwn_legacy);
    ASSERT_EQ(asset.texture_sources.size(), 1u);
    EXPECT_EQ(asset.texture_sources[0].kind, nw::render::ModelAssetTextureSourceKind::external_file);
    EXPECT_EQ(asset.material_texture_sources[0].albedo, 0u);
    EXPECT_EQ(asset.sockets[0].source_node_index, 0u);
    EXPECT_EQ(asset.deformers[0].kind, nw::render::ModelDeformerKind::vertex_shader_sway);
    asset.shadow = nw::render::summarize_model_asset_shadows(asset);
    EXPECT_TRUE(asset.shadow.valid);
    EXPECT_TRUE(asset.shadow.casts_shadow);
    EXPECT_EQ(asset.shadow.caster_count, 1u);
    ASSERT_EQ(asset.particle_systems.size(), 1u);
    EXPECT_EQ(asset.particle_systems[0].animation_name, "cast");
    EXPECT_EQ(asset.particle_systems[0].effect.emitters.size(), 1u);
    ASSERT_EQ(asset.particle_systems[0].effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(asset.particle_systems[0].effect_events[0].time, 0.25f);
    EXPECT_EQ(asset.particle_systems[0].effect_events[0].burst_count, 2u);
}

TEST(RenderModelAsset, RenderModelSocketLookupUsesStableIndices)
{
    nw::render::RenderModel model;
    model.nodes.resize(2);
    model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 1u,
        .name = "Hand_R",
    });

    const uint32_t index = model.socket_index("hand_r");
    ASSERT_NE(index, nw::render::kInvalidModelNodeIndex);
    ASSERT_NE(model.socket(index), nullptr);
    EXPECT_EQ(model.socket(index)->source_node_index, 1u);
    EXPECT_EQ(model.socket_node(index), &model.nodes[1]);

    model.sockets[index].name = "renamed";

    EXPECT_EQ(model.socket(index)->source_node_index, 1u);
    EXPECT_EQ(model.socket_node(index), &model.nodes[1]);
    EXPECT_EQ(model.socket_index("hand_r"), nw::render::kInvalidModelNodeIndex);
    EXPECT_EQ(model.socket_index("renamed"), index);
    EXPECT_EQ(model.socket_index(""), nw::render::kInvalidModelNodeIndex);
    EXPECT_EQ(model.socket(99u), nullptr);
    EXPECT_EQ(model.socket_node(99u), nullptr);
}

TEST(RenderModelAsset, RenderModelSocketNodeRejectsInvalidSourceNode)
{
    nw::render::RenderModel model;
    model.nodes.resize(1);
    model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 4u,
        .name = "hand_l",
    });

    const uint32_t index = model.socket_index("HAND_L");
    ASSERT_NE(index, nw::render::kInvalidModelNodeIndex);
    ASSERT_NE(model.socket(index), nullptr);
    EXPECT_EQ(model.socket(index)->source_node_index, 4u);
    EXPECT_EQ(model.socket_node(index), nullptr);
}

TEST(RenderModelAsset, NeutralSurfaceFallbackPreservesMaterialFactors)
{
    EXPECT_EQ(static_cast<uint32_t>(nw::render::kModelSurfaceNeutralOcclusion), 255u);
    EXPECT_EQ(static_cast<uint32_t>(nw::render::kModelSurfaceNeutralRoughness), 255u);
    EXPECT_EQ(static_cast<uint32_t>(nw::render::kModelSurfaceNeutralMetallic), 0u);
    EXPECT_EQ(static_cast<uint32_t>(nw::render::kModelSurfaceNeutralAlpha), 255u);
}

TEST(RenderModelAsset, MaterialTextureUploadUsesNeutralFallbacksForMissingTextureRoles)
{
    const auto asset = make_minimal_valid_asset();

    nw::render::RenderModel model;
    const auto stats = nw::render::upload_model_asset_material_textures(asset,
        nw::render::ModelAssetTextureUploadDesc{
            .ctx = nullptr,
            .fallback_albedo = 11u,
            .fallback_normal = 12u,
            .fallback_surface = 13u,
            .fallback_emissive = 14u,
        },
        model);

    EXPECT_TRUE(stats.passed());
    EXPECT_EQ(stats.missing_context_count, 0u);
    EXPECT_EQ(stats.fallback_material_count, 0u);
    EXPECT_EQ(stats.uploaded_texture_count, 0u);
    ASSERT_EQ(model.materials.size(), 1u);
    EXPECT_EQ(model.materials[0].albedo_index, 11u);
    EXPECT_EQ(model.materials[0].normal_index, 12u);
    EXPECT_EQ(model.materials[0].surface_index, 13u);
    EXPECT_EQ(model.materials[0].emissive_index, 14u);
    EXPECT_FALSE(model.materials[0].material_uses_fallback);
}

TEST(RenderModelAsset, EmptyAllowsParticleOnlyAssets)
{
    nw::render::ModelAsset asset;
    EXPECT_TRUE(asset.empty());

    nw::render::ParticleEffectDef effect;
    effect.emitters.push_back(nw::render::ParticleEmitterDef{});
    nw::render::ModelAssetParticleSystem particles;
    particles.name = "standalone";
    particles.effect = std::move(effect);
    asset.particle_systems.push_back(std::move(particles));

    EXPECT_FALSE(asset.empty());
}

TEST(RenderModelAsset, ShadowSummaryCountsEligiblePrimitives)
{
    nw::render::ModelAsset asset = make_minimal_valid_asset();
    asset.materials.push_back(nw::render::Material{
        .alpha_mode = nw::render::MaterialMode::water,
    });

    nw::render::ModelAssetPrimitive water = asset.primitives[0];
    water.material = 1u;
    asset.primitives.push_back(water);

    nw::render::ModelAssetPrimitive disabled = asset.primitives[0];
    disabled.casts_shadow = false;
    asset.primitives.push_back(disabled);

    const auto summary = nw::render::summarize_model_asset_shadows(asset);

    EXPECT_TRUE(summary.valid);
    EXPECT_TRUE(summary.casts_shadow);
    EXPECT_EQ(summary.caster_count, 1u);
    EXPECT_FLOAT_EQ(summary.bounds.min.x, asset.bounds.min.x);
    EXPECT_FLOAT_EQ(summary.bounds.min.y, asset.bounds.min.y);
    EXPECT_FLOAT_EQ(summary.bounds.min.z, asset.bounds.min.z);
    EXPECT_FLOAT_EQ(summary.bounds.max.x, asset.bounds.max.x);
    EXPECT_FLOAT_EQ(summary.bounds.max.y, asset.bounds.max.y);
    EXPECT_FLOAT_EQ(summary.bounds.max.z, asset.bounds.max.z);
}

TEST(RenderModelAsset, ValidationAcceptsValidPrimitiveBatch)
{
    const auto asset = make_minimal_valid_asset();

    const auto stats = nw::render::validate_model_asset(asset);

    EXPECT_TRUE(stats.passed());
    EXPECT_EQ(stats.primitive_count, 1u);
    EXPECT_EQ(stats.valid_primitive_count, 1u);
    EXPECT_EQ(stats.invalid_primitive_count(), 0u);
}

TEST(RenderModelAsset, ValidationCountsInvalidPrimitiveReferences)
{
    auto asset = make_minimal_valid_asset();
    asset.materials.clear();
    asset.nodes.clear();
    asset.primitives[0].indices = {0u, 3u};
    asset.primitives[0].deformer = 0u;

    const auto stats = nw::render::validate_model_asset(asset);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.primitive_count, 1u);
    EXPECT_EQ(stats.valid_primitive_count, 0u);
    EXPECT_EQ(stats.invalid_primitive_count(), 1u);
    EXPECT_EQ(stats.index_out_of_range_count, 1u);
    EXPECT_EQ(stats.invalid_material_count, 1u);
    EXPECT_EQ(stats.invalid_node_count, 1u);
    EXPECT_EQ(stats.invalid_deformer_count, 1u);
}

TEST(RenderModelAsset, ValidationCountsInvalidModelAssetRows)
{
    auto asset = make_minimal_valid_asset();
    asset.nodes[0].parent = 7;
    asset.deformers.push_back(nw::render::ModelDeformer{
        .source_node_index = 9u,
        .vertex_count = 0u,
    });

    nw::render::Skeleton skeleton;
    skeleton.joints.push_back(nw::render::Joint{
        .parent = -1,
        .node = 0,
    });
    nw::render::build_eval_order(skeleton);
    asset.skeletons.push_back(std::move(skeleton));

    asset.skins.push_back(nw::render::Skin{
        .skeleton = 5u,
        .joints = {0, 8},
        .inverse_bind_matrices = {glm::mat4{1.0f}},
    });

    const auto stats = nw::render::validate_model_asset(asset);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.primitive_count, 1u);
    EXPECT_EQ(stats.valid_primitive_count, 1u);
    EXPECT_EQ(stats.invalid_primitive_count(), 0u);
    EXPECT_EQ(stats.node_count, 1u);
    EXPECT_EQ(stats.invalid_node_parent_count, 1u);
    EXPECT_EQ(stats.deformer_count, 1u);
    EXPECT_EQ(stats.invalid_deformer_source_node_count, 1u);
    EXPECT_EQ(stats.empty_deformer_vertex_count, 1u);
    EXPECT_EQ(stats.skin_count, 1u);
    EXPECT_EQ(stats.invalid_skin_skeleton_count, 1u);
    EXPECT_EQ(stats.invalid_skin_joint_count, 1u);
    EXPECT_EQ(stats.invalid_skin_inverse_bind_count, 1u);
    EXPECT_GT(stats.invalid_asset_row_count(), 0u);
}

TEST(RenderModelAsset, ValidationCountsInvalidSkeletonAndAnimationRows)
{
    auto asset = make_minimal_valid_asset();

    nw::render::Skeleton skeleton;
    skeleton.joints.push_back(nw::render::Joint{
        .parent = -1,
        .node = 0,
    });
    skeleton.joints.push_back(nw::render::Joint{
        .parent = 8,
        .node = 42,
    });
    skeleton.eval_order = {0u, 4u};
    asset.skeletons.push_back(std::move(skeleton));

    nw::render::AnimationClip invalid_skeleton_clip;
    invalid_skeleton_clip.duration = -1.0f;
    invalid_skeleton_clip.skeleton = 8u;
    asset.animations.push_back(std::move(invalid_skeleton_clip));

    nw::render::AnimationClip track_mismatch_clip;
    track_mismatch_clip.duration = 0.5f;
    track_mismatch_clip.skeleton = 0u;
    track_mismatch_clip.tracks = {nw::render::JointTrack{}};
    asset.animations.push_back(std::move(track_mismatch_clip));

    const auto stats = nw::render::validate_model_asset(asset);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.skeleton_count, 1u);
    EXPECT_EQ(stats.invalid_skeleton_joint_parent_count, 1u);
    EXPECT_EQ(stats.invalid_skeleton_joint_node_count, 1u);
    EXPECT_EQ(stats.invalid_skeleton_eval_order_count, 1u);
    EXPECT_EQ(stats.invalid_skeleton_node_to_joint_count, 1u);
    EXPECT_EQ(stats.animation_count, 2u);
    EXPECT_EQ(stats.invalid_animation_duration_count, 1u);
    EXPECT_EQ(stats.invalid_animation_skeleton_count, 1u);
    EXPECT_EQ(stats.invalid_animation_track_count, 1u);
}

TEST(RenderModelAsset, ValidationCountsUnsupportedSkinnedPrimitive)
{
    nw::render::ModelAsset asset;
    asset.materials.push_back(nw::render::Material{});
    nw::render::Skeleton skeleton;
    skeleton.joints.reserve(nw::render::kModelMaxSkinBones + 1u);
    for (size_t i = 0; i < nw::render::kModelMaxSkinBones + 1u; ++i) {
        skeleton.joints.push_back(nw::render::Joint{
            .parent = i == 0 ? -1 : static_cast<int32_t>(i - 1u),
            .node = 0,
        });
    }
    nw::render::build_eval_order(skeleton);
    asset.nodes.push_back(nw::render::Node{});
    asset.skeletons.push_back(std::move(skeleton));
    asset.skins.push_back(nw::render::Skin{});
    asset.skins[0].skeleton = 0u;
    asset.skins[0].joints.resize(nw::render::kModelMaxSkinBones + 1u);
    asset.skins[0].inverse_bind_matrices.resize(asset.skins[0].joints.size(), glm::mat4{1.0f});

    nw::render::ModelAssetPrimitive primitive;
    primitive.skinned = true;
    primitive.skinned_vertices = {nw::render::SkinnedVertex{}, nw::render::SkinnedVertex{}, nw::render::SkinnedVertex{}};
    primitive.indices = {0u, 1u, 2u};
    primitive.material = 0u;
    primitive.skin = 0u;
    asset.primitives.push_back(std::move(primitive));

    const auto stats = nw::render::validate_model_asset(asset);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.valid_primitive_count, 0u);
    EXPECT_EQ(stats.unsupported_skin_bone_count, 1u);
}

TEST(RenderModelAsset, ValidationCountsInvalidMaterialTextureBindings)
{
    auto asset = make_minimal_valid_asset();
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 7u,
    });

    const auto stats = nw::render::validate_model_asset(asset);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.primitive_count, 1u);
    EXPECT_EQ(stats.valid_primitive_count, 1u);
    EXPECT_EQ(stats.texture_source_count, 0u);
    EXPECT_EQ(stats.material_texture_binding_count, 1u);
    EXPECT_EQ(stats.invalid_material_texture_binding_count, 1u);
}

TEST(RenderModelAsset, UploadRejectsInvalidAssetBeforeGpuWork)
{
    auto asset = make_minimal_valid_asset();
    asset.materials.clear();

    auto result = nw::render::upload_model_asset(asset, nullptr);

    EXPECT_FALSE(result.model);
    EXPECT_EQ(result.stats.primitive_count, 1u);
    EXPECT_EQ(result.stats.invalid_primitive_count, 1u);
    EXPECT_EQ(result.stats.invalid_asset_row_count, 0u);
    EXPECT_EQ(result.stats.missing_context_count, 0u);
    EXPECT_FALSE(result.stats.passed());
}

TEST(RenderModelAsset, UploadRejectsInvalidAssetRowsBeforeGpuWork)
{
    auto asset = make_minimal_valid_asset();
    asset.nodes[0].parent = 99;

    auto result = nw::render::upload_model_asset(asset, nullptr);

    EXPECT_FALSE(result.model);
    EXPECT_EQ(result.stats.primitive_count, 1u);
    EXPECT_EQ(result.stats.invalid_primitive_count, 0u);
    EXPECT_EQ(result.stats.invalid_asset_row_count, 1u);
    EXPECT_EQ(result.stats.invalid_material_texture_binding_count, 0u);
    EXPECT_EQ(result.stats.missing_context_count, 0u);
    EXPECT_FALSE(result.stats.passed());
}

TEST(RenderModelAsset, UploadRejectsInvalidMaterialTextureBindingsBeforeGpuWork)
{
    auto asset = make_minimal_valid_asset();
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 7u,
    });

    auto result = nw::render::upload_model_asset(asset, nullptr);

    EXPECT_FALSE(result.model);
    EXPECT_EQ(result.stats.primitive_count, 1u);
    EXPECT_EQ(result.stats.invalid_primitive_count, 0u);
    EXPECT_EQ(result.stats.invalid_asset_row_count, 0u);
    EXPECT_EQ(result.stats.invalid_material_texture_binding_count, 1u);
    EXPECT_EQ(result.stats.missing_context_count, 0u);
    EXPECT_FALSE(result.stats.passed());
}

TEST(RenderModelAsset, MaterialTextureUploadRejectsInvalidBindings)
{
    auto asset = make_minimal_valid_asset();
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 7u,
    });

    nw::render::RenderModel model;
    const auto stats = nw::render::upload_model_asset_material_textures(asset, nw::render::ModelAssetTextureUploadDesc{}, model);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.invalid_material_texture_binding_count, 1u);
    EXPECT_EQ(stats.missing_context_count, 0u);
    ASSERT_EQ(model.materials.size(), 1u);
}

TEST(RenderModelAsset, MaterialTextureUploadFallsBackWhenContextMissing)
{
    auto asset = make_minimal_valid_asset();
    asset.texture_sources.push_back(nw::render::ModelAssetTextureSource{
        .kind = nw::render::ModelAssetTextureSourceKind::external_file,
        .resource = {},
        .path = "test_data/renderer/MetalRoughSpheres/glTF/Spheres_BaseColor.png",
        .encoded_bytes = {},
    });
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 0u,
    });

    nw::render::RenderModel model;
    const auto stats = nw::render::upload_model_asset_material_textures(asset,
        nw::render::ModelAssetTextureUploadDesc{
            .ctx = nullptr,
            .fallback_albedo = 42u,
        },
        model);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.missing_context_count, 1u);
    EXPECT_EQ(stats.fallback_material_count, 1u);
    EXPECT_EQ(stats.uploaded_texture_count, 0u);
    ASSERT_EQ(model.materials.size(), 1u);
    EXPECT_EQ(model.materials[0].albedo_index, 42u);
    EXPECT_TRUE(model.materials[0].material_uses_fallback);
}

TEST(RenderModelAsset, MaterialTextureUploadMarksPltAlbedoSources)
{
    auto asset = make_minimal_valid_asset();
    asset.texture_sources.push_back(make_encoded_plt_source(make_test_plt_1x2(
        7u,
        nw::plt_layer_hair,
        255u,
        nw::plt_layer_skin)));
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 0u,
    });

    nw::render::ModelAssetTextureUploadStats decode_stats;
    const auto decoded = nw::render::decode_model_asset_texture_source_rgba8(
        asset.texture_sources[0],
        decode_stats);
    ASSERT_TRUE(decoded.valid());
    ASSERT_EQ(decoded.pixels.size(), 8u);
    EXPECT_EQ(decoded.pixels[0], 7u);
    EXPECT_EQ(decoded.pixels[1], nw::plt_layer_hair);
    EXPECT_EQ(decoded.pixels[2], 0u);
    EXPECT_EQ(decoded.pixels[3], 255u);
    EXPECT_EQ(decoded.pixels[4], 255u);
    EXPECT_EQ(decoded.pixels[5], nw::plt_layer_skin);
    EXPECT_EQ(decoded.pixels[6], 0u);
    EXPECT_EQ(decoded.pixels[7], 0u);

    nw::render::RenderModel model;
    const auto stats = nw::render::upload_model_asset_material_textures(asset,
        nw::render::ModelAssetTextureUploadDesc{
            .ctx = nullptr,
            .fallback_albedo = 42u,
        },
        model);

    EXPECT_FALSE(stats.passed());
    EXPECT_EQ(stats.missing_context_count, 1u);
    ASSERT_EQ(model.materials.size(), 1u);
    EXPECT_TRUE(model.materials[0].albedo_uses_plt);
    EXPECT_FALSE(model.materials[0].plt_enabled);
    EXPECT_EQ(model.materials[0].albedo_index, 42u);
    EXPECT_TRUE(model.materials[0].material_uses_fallback);
}

TEST(RenderModelAsset, MaterialTextureUploadCreatesTexturesWhenGraphicsAvailable)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    auto asset = make_minimal_valid_asset();
    asset.texture_sources.push_back(nw::render::ModelAssetTextureSource{
        .kind = nw::render::ModelAssetTextureSourceKind::external_file,
        .resource = {},
        .path = "test_data/renderer/MetalRoughSpheres/glTF/Spheres_BaseColor.png",
        .encoded_bytes = {},
    });
    asset.material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{
        .albedo = 0u,
    });

    nw::render::RenderModel model;
    const auto stats = nw::render::upload_model_asset_material_textures(asset,
        nw::render::ModelAssetTextureUploadDesc{
            .ctx = gfx.context,
            .fallback_albedo = 42u,
        },
        model);

    EXPECT_TRUE(stats.passed());
    EXPECT_EQ(stats.uploaded_texture_count, 1u);
    EXPECT_EQ(stats.fallback_material_count, 0u);
    ASSERT_EQ(model.materials.size(), 1u);
    EXPECT_TRUE(nw::gfx::bindless_texture_index_valid(model.materials[0].albedo_index));
    EXPECT_NE(model.materials[0].albedo_index, 42u);
    EXPECT_FALSE(model.materials[0].material_uses_fallback);
    EXPECT_EQ(model.textures.size(), 1u);

    destroy_render_model_test_textures(gfx.context, model);
}

TEST(RenderModelAsset, TextureDecodeRestoresBottomOriginTgaFileRows)
{
    constexpr uint8_t bottom_origin_descriptor = 0x00;
    auto source = make_encoded_tga_source(make_test_tga_1x2(
        bottom_origin_descriptor,
        {255, 0, 0},
        {0, 255, 0}));
    nw::render::ModelAssetTextureUploadStats stats;

    const auto decoded = nw::render::decode_model_asset_texture_source_rgba8(source, stats);

    EXPECT_TRUE(stats.passed());
    ASSERT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.width, 1);
    EXPECT_EQ(decoded.height, 2);
    expect_rgba_pixel(decoded, 0, 255, 0, 0, 255);
    expect_rgba_pixel(decoded, 1, 0, 255, 0, 255);
}

TEST(RenderModelAsset, TextureDecodeKeepsTopOriginTgaRows)
{
    constexpr uint8_t top_origin_descriptor = 0x20;
    auto source = make_encoded_tga_source(make_test_tga_1x2(
        top_origin_descriptor,
        {0, 255, 0},
        {255, 0, 0}));
    nw::render::ModelAssetTextureUploadStats stats;

    const auto decoded = nw::render::decode_model_asset_texture_source_rgba8(source, stats);

    EXPECT_TRUE(stats.passed());
    ASSERT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.width, 1);
    EXPECT_EQ(decoded.height, 2);
    expect_rgba_pixel(decoded, 0, 0, 255, 0, 255);
    expect_rgba_pixel(decoded, 1, 255, 0, 0, 255);
}

TEST(RenderModelAsset, UploadRejectsMissingContextAfterValidation)
{
    const auto asset = make_minimal_valid_asset();

    auto result = nw::render::upload_model_asset(asset, nullptr);

    EXPECT_FALSE(result.model);
    EXPECT_EQ(result.stats.primitive_count, 1u);
    EXPECT_EQ(result.stats.invalid_primitive_count, 0u);
    EXPECT_EQ(result.stats.missing_context_count, 1u);
    EXPECT_FALSE(result.stats.passed());
}

TEST(RenderModelAsset, UploadsStaticPrimitiveToRenderModelWhenGraphicsAvailable)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    auto asset = make_minimal_valid_asset();
    asset.name = "upload_asset";
    asset.source_kind = nw::render::ModelAssetSourceKind::gltf;
    asset.deformers.push_back(nw::render::ModelDeformer{
        .kind = nw::render::ModelDeformerKind::vertex_shader_sway,
        .source_node_index = 0u,
        .vertex_count = 3u,
    });
    asset.primitives[0].deformer = 0u;
    nw::render::ParticleEffectDef effect;
    effect.emitters.push_back(nw::render::ParticleEmitterDef{});
    asset.particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "impact",
        .animation_name = "cast",
        .effect = std::move(effect),
        .effect_events = {{.time = 0.25f, .burst_count = 2u}},
        .animation_length = 1.0f,
    });

    auto result = nw::render::upload_model_asset(asset, gfx.context);

    ASSERT_TRUE(result.model);
    EXPECT_TRUE(result.stats.passed());
    EXPECT_EQ(result.stats.uploaded_primitive_count, 1u);
    EXPECT_EQ(result.stats.index16_primitive_count, 1u);
    EXPECT_EQ(result.stats.index32_primitive_count, 0u);
    EXPECT_EQ(result.model->name, "upload_asset");
    EXPECT_EQ(result.model->source_kind, asset.source_kind);
    ASSERT_EQ(result.model->primitives.size(), 1u);
    const auto& primitive = result.model->primitives[0];
    EXPECT_EQ(primitive.vertex_count, 3u);
    EXPECT_EQ(primitive.index_count, 3u);
    EXPECT_EQ(primitive.index_stride, 2u);
    EXPECT_EQ(primitive.material, 0u);
    EXPECT_EQ(primitive.deformer, 0u);
    EXPECT_TRUE(primitive.vertices.valid());
    EXPECT_TRUE(primitive.indices.valid());
    ASSERT_EQ(result.model->particle_systems.size(), 1u);
    EXPECT_EQ(result.model->particle_systems[0].name, "impact");
    EXPECT_EQ(result.model->particle_systems[0].animation_name, "cast");
    EXPECT_EQ(result.model->particle_systems[0].effect.emitters.size(), 1u);
    ASSERT_EQ(result.model->particle_systems[0].effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(result.model->particle_systems[0].effect_events[0].time, 0.25f);
    EXPECT_EQ(result.model->particle_systems[0].effect_events[0].burst_count, 2u);

    destroy_render_model_test_buffers(*result.model);
}

} // namespace
