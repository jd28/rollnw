#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/render/viewer/device.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/resources/assets.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

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
        context_desc.width = 256;
        context_desc.height = 256;
        context = nw::gfx::create_context(core, context_desc);
        return context != nullptr;
    }
};

std::vector<std::filesystem::path> viewer_shader_roots()
{
    std::vector<std::filesystem::path> roots;
    const std::filesystem::path candidates[] = {
        "lib/nw/render/shaders",
        "build/tools/mudl/shaders",
        "../../lib/nw/render/shaders",
        "../tools/mudl/shaders",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_directory(candidate)) {
            roots.push_back(candidate);
        }
    }
    return roots;
}

std::filesystem::path cesium_man_path()
{
    const std::filesystem::path candidates[] = {
        "tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb",
        "test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return candidates[0];
}

bool render_viewer_frame(
    nw::gfx::Context* context,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    std::string& failure)
{
    auto* cmd = nw::gfx::begin_frame(context);
    if (!cmd) {
        failure = "begin_frame failed";
        return false;
    }

    session.tick(0);
    session.render(cmd, viewport);
    nw::gfx::end_frame(context);
    nw::gfx::wait_idle(context);
    return true;
}

bool report_has_model_name(
    const nw::render::viewer::PreviewLoadReport& report,
    std::string_view name)
{
    return std::any_of(
        report.model_names.begin(),
        report.model_names.end(),
        [name](const std::string& model_name) {
            return model_name == name;
        });
}

bool report_has_event_category(
    const nw::render::viewer::PreviewLoadReport& report,
    std::string_view category)
{
    return std::any_of(
        report.events.begin(),
        report.events.end(),
        [category](const nw::render::viewer::PreviewLoadEvent& event) {
            return event.category == category;
        });
}

std::vector<std::string> item_model_resrefs_for_test(const nw::Item& item)
{
    std::vector<std::string> result;
    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        return result;
    }

    auto add = [&](std::string resref) {
        if (!resref.empty()) {
            result.push_back(std::move(resref));
        }
    };

    switch (item.model_type) {
    case nw::ItemModelType::simple:
    case nw::ItemModelType::layered:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            add(fmt::format("{}_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]));
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            add(fmt::format("{}_b_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]));
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            add(fmt::format("{}_m_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model2]));
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            add(fmt::format("{}_t_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model3]));
        }
        break;
    case nw::ItemModelType::armor:
        break;
    }
    return result;
}

} // namespace

TEST(RenderViewerPreparedDraws, NwnAppearanceHandItemPolicyReadsWeaponScaleAndArms)
{
    namespace viewer = nw::render::viewer;

    nw::StaticTwoDA appearance{std::string_view{R"(2DA V2.0

LABEL WEAPONSCALE HASARMS
0 Visible 1.55 1
1 NullScale **** 1
2 NoArms 2.30 0
3 InvalidScale -0.1 1
)"}};
    ASSERT_TRUE(appearance.is_valid());

    const auto visible = viewer::resolve_nwn_appearance_hand_item_visual_policy(
        &appearance, nw::Appearance::make(0));
    EXPECT_TRUE(visible.visible);
    EXPECT_FLOAT_EQ(visible.scale, 1.55f);
    EXPECT_EQ(visible.reason, viewer::NwnAppearanceHandItemVisualPolicyReason::visible);

    const auto null_scale = viewer::resolve_nwn_appearance_hand_item_visual_policy(
        &appearance, nw::Appearance::make(1));
    EXPECT_FALSE(null_scale.visible);
    EXPECT_EQ(null_scale.reason, viewer::NwnAppearanceHandItemVisualPolicyReason::hidden_null_weapon_scale);

    const auto no_arms = viewer::resolve_nwn_appearance_hand_item_visual_policy(
        &appearance, nw::Appearance::make(2));
    EXPECT_FALSE(no_arms.visible);
    EXPECT_EQ(no_arms.reason, viewer::NwnAppearanceHandItemVisualPolicyReason::hidden_no_arms);

    const auto invalid_scale = viewer::resolve_nwn_appearance_hand_item_visual_policy(
        &appearance, nw::Appearance::make(3));
    EXPECT_FALSE(invalid_scale.visible);
    EXPECT_EQ(invalid_scale.reason, viewer::NwnAppearanceHandItemVisualPolicyReason::hidden_invalid_weapon_scale);
}

TEST(RenderViewerPreparedDraws, NwnAppearanceHandItemPolicyMissingWeaponScaleKeepsCurrentBehavior)
{
    namespace viewer = nw::render::viewer;

    nw::StaticTwoDA appearance{std::string_view{R"(2DA V2.0

LABEL HASARMS
0 NoColumn 1
)"}};
    ASSERT_TRUE(appearance.is_valid());

    const auto policy = viewer::resolve_nwn_appearance_hand_item_visual_policy(
        &appearance, nw::Appearance::make(0));
    EXPECT_TRUE(policy.visible);
    EXPECT_FLOAT_EQ(policy.scale, 1.0f);
    EXPECT_EQ(policy.reason, viewer::NwnAppearanceHandItemVisualPolicyReason::visible);
}

TEST(RenderViewerPreparedDraws, LoadReportIncludesStaticRenderModelNames)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = "common_static_model";
    model->materials.resize(5);
    model->materials[0].albedo_index = 10u;
    model->materials[0].normal_index = 11u;
    model->materials[0].surface_index = 12u;
    model->materials[0].emissive_index = 13u;
    model->materials[1].alpha_mode = nw::render::MaterialMode::cutout;
    model->materials[1].albedo_uses_plt = true;
    model->materials[1].plt_enabled = true;
    model->materials[1].material_uses_fallback = true;
    model->materials[1].emissive = {1.0f, 0.0f, 0.0f};
    model->materials[1].double_sided = true;
    model->materials[1].roughness = 0.25f;
    model->materials[1].specular_strength = 0.2f;
    model->materials[1].normal_scale = 0.5f;
    model->materials[2].alpha_mode = nw::render::MaterialMode::transparent;
    model->materials[3].alpha_mode = nw::render::MaterialMode::water;
    model->materials[3].roughness = 0.9f;
    model->materials[3].specular_strength = 1.25f;
    model->materials[3].normal_scale = 1.5f;
    model->materials[4].alpha_mode = static_cast<nw::render::MaterialMode>(255);
    model->nodes.resize(2);
    model->sockets.resize(1);
    model->skins.resize(1);
    model->skeletons.resize(1);
    model->animations.resize(1);
    model->deformers.resize(1);
    model->particle_systems.resize(1);
    model->primitives.push_back(nw::render::Primitive{
        .vertex_count = 3,
        .index_count = 3,
        .casts_shadow = true,
    });
    model->primitives.push_back(nw::render::Primitive{
        .vertex_count = 4,
        .index_count = 6,
        .skin = 0,
        .deformer = 0,
        .skinned = true,
        .casts_shadow = false,
    });
    scene.add(std::move(model));

    scene.rebuild_load_report("common_static_model", "model");

    ASSERT_EQ(scene.load_report.model_names.size(), 1u);
    EXPECT_EQ(scene.load_report.model_names[0], "common_static_model");
    ASSERT_EQ(scene.load_report.materials.size(), 1u);
    const auto& materials = scene.load_report.materials[0];
    EXPECT_EQ(materials.owner, "common_static_model");
    EXPECT_EQ(materials.material_count, 5u);
    EXPECT_EQ(materials.fallback_material_count, 1u);
    EXPECT_EQ(materials.plt_albedo_count, 1u);
    EXPECT_EQ(materials.plt_enabled_count, 1u);
    EXPECT_EQ(materials.emissive_material_count, 1u);
    EXPECT_EQ(materials.double_sided_count, 1u);
    EXPECT_EQ(materials.opaque_count, 1u);
    EXPECT_EQ(materials.cutout_count, 1u);
    EXPECT_EQ(materials.transparent_count, 1u);
    EXPECT_EQ(materials.water_count, 1u);
    EXPECT_EQ(materials.unknown_alpha_mode_count, 1u);
    EXPECT_EQ(materials.albedo_bound_count, 1u);
    EXPECT_EQ(materials.normal_bound_count, 1u);
    EXPECT_EQ(materials.surface_bound_count, 1u);
    EXPECT_EQ(materials.emissive_bound_count, 1u);
    EXPECT_FLOAT_EQ(materials.roughness_min, 0.25f);
    EXPECT_FLOAT_EQ(materials.roughness_max, 0.9f);
    EXPECT_FLOAT_EQ(materials.specular_strength_min, 0.2f);
    EXPECT_FLOAT_EQ(materials.specular_strength_max, 1.25f);
    EXPECT_FLOAT_EQ(materials.normal_scale_min, 0.5f);
    EXPECT_FLOAT_EQ(materials.normal_scale_max, 1.5f);
    ASSERT_EQ(scene.load_report.geometries.size(), 1u);
    const auto& geometry = scene.load_report.geometries[0];
    EXPECT_EQ(geometry.owner, "common_static_model");
    EXPECT_EQ(geometry.primitive_count, 2u);
    EXPECT_EQ(geometry.static_primitive_count, 1u);
    EXPECT_EQ(geometry.skinned_primitive_count, 1u);
    EXPECT_EQ(geometry.deformed_primitive_count, 1u);
    EXPECT_EQ(geometry.shadow_caster_count, 1u);
    EXPECT_EQ(geometry.vertex_count, 7u);
    EXPECT_EQ(geometry.index_count, 9u);
    EXPECT_EQ(geometry.node_count, 2u);
    EXPECT_EQ(geometry.socket_count, 1u);
    EXPECT_EQ(geometry.skin_count, 1u);
    EXPECT_EQ(geometry.skeleton_count, 1u);
    EXPECT_EQ(geometry.animation_count, 1u);
    EXPECT_EQ(geometry.deformer_count, 1u);
    EXPECT_EQ(geometry.particle_system_count, 1u);
    EXPECT_EQ(scene.load_report.warning_count(), 0u);
    EXPECT_EQ(scene.load_report.error_count(), 0u);
}

TEST(RenderViewerPreparedDraws, StaticLoadReportIncludesNwnModelAssetMaterials)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    const auto report = viewer::build_preview_load_report(model_path.string());

    ASSERT_EQ(report.materials.size(), 1u);
    const auto& materials = report.materials[0];
    EXPECT_FALSE(materials.owner.empty());
    EXPECT_GT(materials.material_count, 0u);
    EXPECT_EQ(materials.opaque_count + materials.cutout_count + materials.transparent_count + materials.water_count
            + materials.unknown_alpha_mode_count,
        materials.material_count);
    ASSERT_EQ(report.geometries.size(), 1u);
    const auto& geometry = report.geometries[0];
    EXPECT_FALSE(geometry.owner.empty());
    EXPECT_EQ(geometry.primitive_count, 1u);
    EXPECT_EQ(geometry.static_primitive_count, 1u);
    EXPECT_EQ(geometry.skinned_primitive_count, 0u);
    EXPECT_EQ(geometry.vertex_count, 3u);
    EXPECT_EQ(geometry.index_count, 3u);
    EXPECT_EQ(geometry.normal_repair_count, 0u);
    EXPECT_EQ(geometry.tangent_repair_count, 0u);
}

TEST(RenderViewerPreparedDraws, StaticLoadReportCountsNwnGeometryRepairs)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/test_invalid_vertex_frame.mdl"};
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    const auto report = viewer::build_preview_load_report(model_path.string());

    ASSERT_EQ(report.geometries.size(), 1u);
    const auto& geometry = report.geometries[0];
    EXPECT_EQ(geometry.primitive_count, 1u);
    EXPECT_EQ(geometry.static_primitive_count, 1u);
    EXPECT_EQ(geometry.normal_repair_count, 1u);
    EXPECT_EQ(geometry.tangent_repair_count, 1u);
}

TEST(RenderViewerPreparedDraws, LoadReportCountsRenderModelMaterialOverrides)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = "common_plt_model";
    model->materials.resize(1);
    model->materials[0].albedo_uses_plt = true;
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    auto override_material = scene.static_models[0]->materials[0];
    override_material.plt_enabled = true;
    override_material.albedo_index = 21u;
    instance->material_override_handles.resize(1);
    instance->material_override_handles[0] = scene.material_overrides.create(nw::render::ModelMaterialOverride{
        .material = override_material,
    });

    scene.rebuild_load_report("common_plt_model", "model");

    ASSERT_EQ(scene.load_report.materials.size(), 1u);
    EXPECT_EQ(scene.load_report.materials[0].plt_albedo_count, 1u);
    EXPECT_EQ(scene.load_report.materials[0].plt_enabled_count, 1u);
    EXPECT_EQ(scene.load_report.materials[0].albedo_bound_count, 1u);
}

TEST(RenderViewerPreparedDraws, StaticLoadReportCountsNwnPltAlbedoMaterials)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/test_plt_material.mdl"};
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    const auto report = viewer::build_preview_load_report(model_path.string());

    ASSERT_EQ(report.materials.size(), 1u);
    EXPECT_EQ(report.materials[0].material_count, 1u);
    EXPECT_EQ(report.materials[0].albedo_source_count, 1u);
    EXPECT_EQ(report.materials[0].plt_albedo_count, 1u);
    EXPECT_EQ(report.materials[0].plt_enabled_count, 0u);
    EXPECT_EQ(report.materials[0].albedo_bound_count, 0u);
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportUsesHumanoidResolverRows)
{
    namespace viewer = nw::render::viewer;

    const auto report = viewer::build_preview_load_report("test_data/user/development/drorry.utc");

    EXPECT_EQ(report.kind, "dynamic_creature");
    EXPECT_TRUE(report_has_model_name(report, "pma0"));
    EXPECT_TRUE(report_has_model_name(report, "pma0_chest001"));
    EXPECT_TRUE(report_has_model_name(report, "pma0_head001"));
}

TEST(RenderViewerPreparedDraws, ItemLoadReportUsesItemModelResolverRows)
{
    namespace viewer = nw::render::viewer;

    const auto report = viewer::build_preview_load_report("test_data/user/development/wduersc004.uti");

    EXPECT_EQ(report.kind, "item");
    EXPECT_TRUE(report_has_model_name(report, "wswsc_b_044"));
    EXPECT_TRUE(report_has_model_name(report, "wswsc_m_054"));
    EXPECT_TRUE(report_has_model_name(report, "wswsc_t_044"));
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportUsesAttachmentLookupRows)
{
    namespace viewer = nw::render::viewer;

    auto* wingmodel = nw::kernel::twodas().get("wingmodel");
    std::string wing_model;
    if (!wingmodel || !wingmodel->get_to(1u, "MODEL", wing_model) || wing_model.empty()) {
        GTEST_SKIP() << "wingmodel row 1 unavailable";
    }
    const nw::Resource wing_resource{std::string_view{wing_model}, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(wing_resource)) {
        GTEST_SKIP() << "wingmodel row 1 model unavailable";
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(creature, nullptr);

    std::filesystem::create_directories("tmp");
    const std::filesystem::path no_wing_path{"tmp/load_report_no_wing_creature.utc.json"};
    creature->appearance.wings = 0u;
    ASSERT_TRUE(creature->save(no_wing_path, "json"));

    const std::filesystem::path wing_path{"tmp/load_report_wing_creature.utc.json"};
    creature->appearance.wings = 1u;
    ASSERT_TRUE(creature->save(wing_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    const auto no_wing_report = viewer::build_preview_load_report(no_wing_path.string());
    const auto wing_report = viewer::build_preview_load_report(wing_path.string());

    EXPECT_EQ(no_wing_report.kind, "dynamic_creature");
    EXPECT_EQ(wing_report.kind, "dynamic_creature");
    EXPECT_GT(wing_report.model_names.size(), no_wing_report.model_names.size());
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportCountsWingRowPolicy)
{
    namespace viewer = nw::render::viewer;

    auto* wingmodel = nw::kernel::twodas().get("wingmodel");
    std::string wing_model;
    if (!wingmodel || !wingmodel->get_to(1u, "MODEL", wing_model) || wing_model.empty()) {
        GTEST_SKIP() << "wingmodel row 1 unavailable";
    }
    const nw::Resource wing_resource{std::string_view{wing_model}, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(wing_resource)) {
        GTEST_SKIP() << "wingmodel row 1 model unavailable";
    }

    const auto policy = viewer::resolve_nwn_wing_attachment_visual_policy(nw::Appearance::invalid(), 1u);
    ASSERT_TRUE(policy.strip_non_render_meshes);
    EXPECT_EQ(policy.reason, viewer::NwnWingAttachmentVisualPolicyReason::strip_non_render_meshes);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(creature, nullptr);
    creature->appearance.wings = 1u;

    std::filesystem::create_directories("tmp");
    const std::filesystem::path wing_path{"tmp/load_report_wing_policy_creature.utc.json"};
    ASSERT_TRUE(creature->save(wing_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    const auto report = viewer::build_preview_load_report(wing_path.string());

    EXPECT_EQ(report.kind, "dynamic_creature");
    EXPECT_TRUE(report_has_event_category(report, "nwn_wing_attachment_policy"));
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportCountsSkinnedMindflayerAsset)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_mindflayer);
    if (!appearance || appearance->model_type == "P" || appearance->model_name.empty()) {
        GTEST_SKIP() << "single-model mindflayer appearance unavailable";
    }
    if (!nw::kernel::resman().contains({nw::Resref{appearance->model_name}, nw::ResourceType::mdl})) {
        GTEST_SKIP() << "mindflayer model resource unavailable";
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(creature, nullptr);
    creature->appearance.id = nwn1::appearance_type_mindflayer;
    creature->appearance.wings = 0u;
    creature->appearance.tail = 0u;
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    std::filesystem::create_directories("tmp");
    const std::filesystem::path creature_path{"tmp/load_report_mindflayer_creature.utc.json"};
    ASSERT_TRUE(creature->save(creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    const auto report = viewer::build_preview_load_report(creature_path.string(), "cpause1");

    EXPECT_EQ(report.kind, "dynamic_creature");
    EXPECT_TRUE(report_has_model_name(report, appearance->model_name));
    const auto geometry = std::find_if(
        report.geometries.begin(),
        report.geometries.end(),
        [&](const viewer::PreviewLoadGeometryReport& row) {
            return row.owner == appearance->model_name;
        });
    ASSERT_NE(geometry, report.geometries.end());
    EXPECT_GT(geometry->skinned_primitive_count, 0u);
    EXPECT_GT(geometry->skin_count, 0u);
    EXPECT_GT(geometry->skeleton_count, 0u);
    EXPECT_GT(geometry->animation_count, 0u);
    EXPECT_GT(geometry->particle_system_count, 0u);
}

TEST(RenderViewerPreparedDraws, PreviewSceneLoadOptionsDefaultToNwnRenderModelPath)
{
    namespace viewer = nw::render::viewer;

    const viewer::PreviewSceneLoadOptions options{};

    EXPECT_EQ(options.nwn_model_path, viewer::NwnModelPreviewPath::render_model);
}

TEST(RenderViewerPreparedDraws, PreviewSceneLoadOptionsIgnoresRetiredNwnEnvOverride)
{
    namespace viewer = nw::render::viewer;

    constexpr const char* kEnvName = "ROLLNW_VIEWER_NWN_RENDER_MODEL_PATH";
    const char* previous = std::getenv(kEnvName);
    const bool had_previous = previous != nullptr;
    const std::string previous_value = had_previous ? previous : "";

    ASSERT_EQ(::setenv(kEnvName, "0", 1), 0);
    const viewer::PreviewSceneLoadOptions options = viewer::default_preview_scene_load_options();
    if (had_previous) {
        ASSERT_EQ(::setenv(kEnvName, previous_value.c_str(), 1), 0);
    } else {
        ASSERT_EQ(::unsetenv(kEnvName), 0);
    }

    EXPECT_EQ(options.nwn_model_path, viewer::NwnModelPreviewPath::render_model);
}

TEST(RenderViewerPreparedDraws, PreparedRenderModelSurfacePathSubmitsCesiumManWithoutDrops)
{
    namespace viewer = nw::render::viewer;

    const auto model_path = cesium_man_path();
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };

    auto session = device.make_session();
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->load_object_file(model_path));
    std::string failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;
    const auto& stats = session->last_frame_stats();
    EXPECT_TRUE(stats.prepared_render_model_draws_enabled);
    EXPECT_EQ(stats.render_model_animation_sample_stats.input_count, size_t{1});
    EXPECT_EQ(stats.render_model_animation_sample_stats.sampled_count, size_t{1});
    EXPECT_TRUE(stats.prepared_model_surface_stats_enabled);
    EXPECT_GT(stats.prepared_render_model_draw_count, 0u);
    EXPECT_TRUE(stats.prepared_model_surface_stats.valid());
    EXPECT_EQ(stats.prepared_model_surface_stats.render_model_draw_count,
        stats.prepared_render_model_draw_count);
    EXPECT_TRUE(stats.prepared_model_surface_material_bindings.valid());
    EXPECT_GT(stats.prepared_render_model_surface_submission.submitted_surface_count, 0u);
    EXPECT_GT(stats.prepared_render_model_surface_submission.submitted_run_count, 0u);
    EXPECT_EQ(stats.prepared_render_model_surface_submission.dropped_invalid_surface_count, 0u);
    EXPECT_TRUE(stats.prepared_render_model_surface_submission.valid());
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathCreatesStaticRenderModelPreview)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/test_mtr_material.mdl"};
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_model(model_path.string()));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->models.empty());
    ASSERT_EQ(scene->static_models.size(), 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), 1u);

    const auto* instance = scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->kind, nw::render::ModelInstanceKind::render_model);
    EXPECT_TRUE(instance->visible);
    EXPECT_GT(scene->static_models[0]->primitives.size(), 0u);
    ASSERT_EQ(scene->load_report.model_names.size(), 1u);
    EXPECT_EQ(scene->load_report.model_names[0], scene->static_models[0]->name);
    const auto render_model_path_event = std::find_if(
        scene->load_report.events.begin(),
        scene->load_report.events.end(),
        [](const viewer::PreviewLoadEvent& event) {
            return event.category == "nwn_render_model_path";
        });
    EXPECT_NE(render_model_path_event, scene->load_report.events.end());

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    std::string failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;

    const auto& stats = session->last_frame_stats();
    EXPECT_EQ(stats.static_model_count, 1u);
    EXPECT_TRUE(stats.prepared_render_model_draws_enabled);
    EXPECT_GT(stats.prepared_render_model_draw_count, 0u);
    EXPECT_GT(stats.prepared_render_model_surface_submission.submitted_surface_count, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathCreatesStaticRenderModelCreaturePreview)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_bodak);
    if (!appearance || appearance->model_type == "P" || appearance->model_name.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const nw::Resource model_resource{std::string_view{appearance->model_name}, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(model_resource)) {
        GTEST_SKIP() << "bodak model resource unavailable";
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(creature, nullptr);
    creature->appearance.id = nwn1::appearance_type_bodak;
    creature->appearance.wings = 1;
    creature->appearance.tail = 0;
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    const std::filesystem::path creature_path{"tmp/render_model_single_creature.utc.json"};
    ASSERT_TRUE(creature->save(creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->models.empty());
    ASSERT_GE(scene->static_models.size(), 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());
    EXPECT_EQ(scene->load_report.kind, "dynamic_creature");
    ASSERT_GE(scene->load_report.model_names.size(), 1u);
    EXPECT_EQ(scene->load_report.model_names[0], scene->static_models[0]->name);

    const auto* instance = scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->kind, nw::render::ModelInstanceKind::render_model);
    EXPECT_TRUE(instance->visible);

    const auto render_model_path_event = std::find_if(
        scene->load_report.events.begin(),
        scene->load_report.events.end(),
        [](const viewer::PreviewLoadEvent& event) {
            return event.category == "nwn_render_model_path";
        });
    EXPECT_NE(render_model_path_event, scene->load_report.events.end());
    const auto skipped_addons_event = std::find_if(
        scene->load_report.events.begin(),
        scene->load_report.events.end(),
        [](const viewer::PreviewLoadEvent& event) {
            return event.category == "nwn_render_model_creature_addons"
                && event.severity == viewer::PreviewLoadEventSeverity::warning;
        });
    const bool common_addon_attached = scene->static_models.size() > 1 && !scene->model_attachments.empty();
    EXPECT_TRUE(common_addon_attached || skipped_addons_event != scene->load_report.events.end());

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    std::string failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;

    const auto& stats = session->last_frame_stats();
    EXPECT_EQ(stats.static_model_count, scene->static_models.size());
    EXPECT_TRUE(stats.prepared_render_model_draws_enabled);
    EXPECT_GT(stats.prepared_render_model_draw_count, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathAttachesEquippedHandItems)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_bodak);
    if (!appearance || appearance->model_type == "P" || appearance->model_name.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const nw::Resource model_resource{std::string_view{appearance->model_name}, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(model_resource)) {
        GTEST_SKIP() << "bodak model resource unavailable";
    }

    constexpr std::string_view item_resref = "nw_wswbs001";
    auto* item = nw::kernel::objects().load<nw::Item>(item_resref);
    if (!item) {
        GTEST_SKIP() << "test hand item unavailable";
    }
    const auto item_models = item_model_resrefs_for_test(*item);
    if (item_models.empty()) {
        GTEST_SKIP() << "test hand item has no model parts";
    }
    for (const auto& item_model : item_models) {
        if (!nw::kernel::resman().contains({nw::Resref{item_model}, nw::ResourceType::mdl})) {
            GTEST_SKIP() << "test hand item model unavailable: " << item_model;
        }
    }

    auto* creature = nw::kernel::objects().load<nw::Creature>("nw_chicken");
    if (!creature) {
        GTEST_SKIP() << "nw_chicken creature blueprint unavailable";
    }
    creature->appearance.id = nwn1::appearance_type_bodak;
    creature->appearance.wings = 0;
    creature->appearance.tail = 0;
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }
    creature->equipment.equips[static_cast<size_t>(nw::EquipIndex::righthand)] = nw::Resref{item_resref};

    std::filesystem::create_directories("tmp");
    const std::filesystem::path creature_path{"tmp/render_model_single_creature_hand_item.utc.json"};
    ASSERT_TRUE(creature->save(creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_object_file(creature_path));

    auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->models.empty());
    ASSERT_EQ(scene->static_models.size(), item_models.size() + 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());
    ASSERT_EQ(scene->model_attachments.size(), item_models.size());

    for (size_t i = 0; i < scene->model_attachments.size(); ++i) {
        const auto& binding = scene->model_attachments[i];
        EXPECT_EQ(binding.owner_kind, nw::render::ModelInstanceKind::render_model);
        EXPECT_EQ(binding.child_kind, nw::render::ModelInstanceKind::render_model);
        EXPECT_EQ(binding.owner_model_index, 0u);
        EXPECT_EQ(binding.child_model_index, static_cast<uint32_t>(i + 1u));
        EXPECT_EQ(binding.owner_instance_handle, scene->static_model_instance_handles[0]);
        EXPECT_EQ(binding.child_instance_handle, scene->static_model_instance_handles[i + 1u]);
        EXPECT_NE(binding.owner_socket_index, nw::render::kInvalidModelNodeIndex);
        EXPECT_FLOAT_EQ(binding.child_local_scale, 1.0f);
    }

    const auto sync_stats = viewer::sync_model_instance_runtime_state(*scene);
    EXPECT_EQ(sync_stats.render_model_attachment_binding_count, item_models.size());
    EXPECT_EQ(sync_stats.render_model_attachment_root_resolved_count, item_models.size());
    EXPECT_EQ(sync_stats.render_model_attachment_root_failed_count, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelDynamicSkinnedCreatureSamplesSkinMatricesAndParticles)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_mindflayer);
    if (!appearance || appearance->model_type == "P" || appearance->model_name.empty()) {
        GTEST_SKIP() << "single-model mindflayer appearance unavailable";
    }
    const nw::Resource model_resource{std::string_view{appearance->model_name}, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(model_resource)) {
        GTEST_SKIP() << "mindflayer model resource unavailable";
    }

    auto* creature = nw::kernel::objects().load<nw::Creature>("nw_chicken");
    if (!creature) {
        GTEST_SKIP() << "nw_chicken creature blueprint unavailable";
    }
    creature->appearance.id = nwn1::appearance_type_mindflayer;
    creature->appearance.wings = 0;
    creature->appearance.tail = 0;
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    std::filesystem::create_directories("tmp");
    const std::filesystem::path creature_path{"tmp/render_model_skinned_mindflayer.utc.json"};
    ASSERT_TRUE(creature->save(creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->models.empty());
    ASSERT_GE(scene->static_models.size(), 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());

    const auto* model = scene->static_models.front().get();
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(std::any_of(model->primitives.begin(), model->primitives.end(), [](const auto& primitive) {
        return primitive.skinned;
    }));
    EXPECT_GT(model->particle_systems.size(), 0u);

    const auto* instance = scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_TRUE(instance->animation.backend);
    const auto cpause1 = std::find_if(model->animations.begin(), model->animations.end(), [](const auto& clip) {
        return clip.name == "cpause1";
    });
    ASSERT_NE(cpause1, model->animations.end());
    EXPECT_EQ(instance->animation.clip, static_cast<uint32_t>(std::distance(model->animations.begin(), cpause1)));
    EXPECT_FLOAT_EQ(instance->animation.time, 0.033f);
    EXPECT_GT(scene->particles.size(), 0u);

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    std::string failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;

    const auto& stats = session->last_frame_stats();
    EXPECT_EQ(stats.model_count, 0u);
    EXPECT_EQ(stats.static_model_count, scene->static_models.size());
    EXPECT_GT(stats.render_model_animation_sample_stats.input_count, 0u);
    EXPECT_GT(stats.render_model_animation_sample_stats.sampled_count, 0u);
    EXPECT_EQ(stats.render_model_animation_sample_stats.invalid_skeleton_count, 0u);
    EXPECT_EQ(stats.render_model_animation_sample_stats.failed_sample_count, 0u);
    EXPECT_GT(stats.prepared_render_model_skin_table_stats.render_model_skinned_surface_count, 0u);
    EXPECT_EQ(stats.prepared_render_model_skin_table_stats.assigned_surface_count,
        stats.prepared_render_model_skin_table_stats.render_model_skinned_surface_count);
    EXPECT_EQ(stats.prepared_render_model_skin_table_stats.bind_pose_fallback_surface_count, 0u);
    EXPECT_EQ(stats.prepared_render_model_skin_table_stats.invalid_skin_index_count, 0u);
    EXPECT_GT(stats.particle_system_count, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathWarnsAndFallsBackForHumanoidCreature)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_human);
    if (!appearance || appearance->model_type != "P") {
        GTEST_SKIP() << "humanoid human appearance unavailable";
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(creature, nullptr);
    creature->appearance.id = nwn1::appearance_type_human;
    creature->appearance.wings = 0;
    creature->appearance.tail = 0;
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    const std::filesystem::path creature_path{"tmp/render_model_humanoid_creature.utc.json"};
    ASSERT_TRUE(creature->save(creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_FALSE(scene->models.empty());
    EXPECT_TRUE(scene->static_models.empty());

    const auto fallback_event = std::find_if(
        scene->load_report.events.begin(),
        scene->load_report.events.end(),
        [](const viewer::PreviewLoadEvent& event) {
            return event.category == "nwn_render_model_humanoid_fallback"
                && event.severity == viewer::PreviewLoadEventSeverity::warning;
        });
    EXPECT_NE(fallback_event, scene->load_report.events.end());
}

TEST(RenderViewerPreparedDraws, NwnRenderModelUnsupportedDanglyDeformersRenderStatic)
{
    namespace viewer = nw::render::viewer;

    if (!nw::kernel::resman().contains({nw::Resref{"c_aribeth"}, nw::ResourceType::mdl})) {
        GTEST_SKIP() << "c_aribeth installed-game resource unavailable";
    }

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_model("c_aribeth"));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->models.empty());
    ASSERT_EQ(scene->static_models.size(), 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());

    const auto static_deformer_event = std::find_if(
        scene->load_report.events.begin(),
        scene->load_report.events.end(),
        [](const viewer::PreviewLoadEvent& event) {
            return event.category == "nwn_render_model_static_deformer"
                && event.severity == viewer::PreviewLoadEventSeverity::warning;
        });
    EXPECT_NE(static_deformer_event, scene->load_report.events.end());
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathCanSampleNamedAnimation)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/c_bodak.mdl"};
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_model(model_path.string()));
    ASSERT_TRUE(session->select_animation("walk"));

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    std::string failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->static_models.size(), 1u);
    const auto* model = scene->static_models.front().get();
    ASSERT_NE(model, nullptr);
    const auto clip_it = std::find_if(model->animations.begin(), model->animations.end(), [](const auto& clip) {
        return clip.name == "walk";
    });
    ASSERT_NE(clip_it, model->animations.end());

    const auto* instance = scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_EQ(instance->animation.clip, static_cast<uint32_t>(std::distance(model->animations.begin(), clip_it)));
    EXPECT_FALSE(instance->attachment_node_world_transforms.empty());
    EXPECT_EQ(instance->attachment_node_world_transforms.size(), instance->attachment_node_transform_valid.size());

    size_t valid_node_transform_count = 0;
    for (const auto valid : instance->attachment_node_transform_valid) {
        valid_node_transform_count += valid != 0u ? 1u : 0u;
    }
    EXPECT_GT(valid_node_transform_count, 0u);
}

TEST(RenderViewerPreparedDraws, AreaLoadUsesRenderModelPathForNonHumanoidCreatures)
{
    namespace viewer = nw::render::viewer;

    const nw::Resref area_resref{"test_area"};
    if (!nw::kernel::resman().contains({area_resref, nw::ResourceType::are})) {
        GTEST_SKIP() << "test_area fixture unavailable";
    }

    auto* area = nw::kernel::objects().make_area(area_resref);
    if (!area || !area->instantiate()) {
        if (area) {
            nw::kernel::objects().destroy(area->handle());
        }
        GTEST_SKIP() << "test_area fixture failed to instantiate";
    }

    bool has_render_model_creature_candidate = false;
    for (auto* creature : area->creatures) {
        if (!creature) {
            continue;
        }
        creature->instantiate();
        creature->update_appearance(creature->appearance.id);
        auto* appearance = nw::kernel::rules().appearances.get(creature->appearance.id);
        if (!appearance || appearance->model_type == "P" || appearance->model_name.empty()) {
            continue;
        }
        if (nw::kernel::resman().contains({nw::Resref{appearance->model_name}, nw::ResourceType::mdl})) {
            has_render_model_creature_candidate = true;
            break;
        }
    }
    nw::kernel::objects().destroy(area->handle());
    if (!has_render_model_creature_candidate) {
        GTEST_SKIP() << "area fixture has no non-humanoid creature with an available model";
    }

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto shader_roots = viewer_shader_roots();
    if (shader_roots.empty()) {
        GTEST_SKIP() << "viewer shader roots unavailable";
    }

    viewer::ViewerDevice device{gfx.context, nw::kernel::resman()};
    if (!device.initialize(viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    session->set_preview_scene_load_options(viewer::PreviewSceneLoadOptions{
        .nwn_model_path = viewer::NwnModelPreviewPath::render_model,
    });
    ASSERT_TRUE(session->load_area(area_resref.view()));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->is_area);
    EXPECT_GT(scene->static_models.size(), 0u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());

    bool has_render_model_instance = false;
    for (size_t model_index = 0; model_index < scene->static_models.size(); ++model_index) {
        const auto* instance = scene->static_model_instance(model_index);
        if (instance && instance->kind == nw::render::ModelInstanceKind::render_model) {
            has_render_model_instance = true;
            break;
        }
    }
    EXPECT_TRUE(has_render_model_instance);
}
