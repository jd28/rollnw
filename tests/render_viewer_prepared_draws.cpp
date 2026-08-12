#include <gtest/gtest.h>

#include "../tools/client/object_edits.hpp"

#include <nw/formats/Image.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/viewer/device.hpp>
#include <nw/render/viewer/preview_model_animation.hpp>
#include <nw/render/viewer/preview_nwn_creature.hpp>
#include <nw/render/viewer/preview_object.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/resources/assets.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
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

TEST(RenderViewerPreparedDraws, AreaStaticModelCacheDoesNotExtendModelLifetime)
{
    nw::render::viewer::PreviewRenderResources resources{nullptr};
    auto model = std::make_shared<nw::render::RenderModel>();
    std::weak_ptr<nw::render::RenderModel> observed = model;
    const nw::Resref cache_key;

    resources.prepare_area_static_models(1);
    resources.store_area_static_model(cache_key, model);
    {
        const auto cached = resources.find_area_static_model(cache_key);
        EXPECT_EQ(cached, model);
    }
    resources.prepare_area_static_models(2);
    EXPECT_FALSE(resources.find_area_static_model(cache_key));
    resources.store_area_static_model(cache_key, model);

    model.reset();
    EXPECT_TRUE(observed.expired());
    resources.prepare_area_static_models(2);
    EXPECT_FALSE(resources.find_area_static_model(cache_key));
}

bool set_creature_appearance_propset_int(nw::Creature* creature, const char* field, int32_t value)
{
    if (!creature) { return false; }

    auto& rt = nw::kernel::runtime();
    rt.init_object_propsets(creature->handle());
    const auto tid = rt.type_id("nwn1.propsets.CreatureAppearance", false);
    if (tid == nw::smalls::invalid_type_id) { return false; }

    auto ref = rt.get_or_create_propset_ref(tid, creature->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return false; }

    const auto* def = rt.get_struct_def(ref.type_id);
    if (!def) { return false; }

    const uint32_t index = def->field_index(field);
    if (index == std::numeric_limits<uint32_t>::max()) { return false; }

    const auto& field_def = def->fields[index];
    return rt.write_value_field_at_offset(ref, field_def.offset, rt.int_type(),
        nw::smalls::Value::make_int(value));
}

bool render_viewer_frame(
    nw::gfx::Context* context,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    std::string& failure,
    int32_t dt_ms = 0)
{
    auto* cmd = nw::gfx::begin_frame(context);
    if (!cmd) {
        failure = "begin_frame failed";
        return false;
    }

    session.tick(dt_ms);
    session.render(cmd, viewport);
    nw::gfx::end_frame(context);
    nw::gfx::wait_idle(context);
    return true;
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

std::vector<std::string> item_model_resrefs_for_test(nw::Item& item)
{
    std::vector<std::string> result;
    if (!item.instantiate()) {
        return result;
    }

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item.handle());
    item_value.type_id = rt.object_subtype_for_tag(item.handle().type);
    args.push_back(item_value);
    args.push_back(nw::smalls::Value::make_bool(false));

    auto update = rt.execute_script("nwn1.item", "update_standalone_visual", args);
    if (!update.ok() || update.value.type_id != rt.bool_type() || !update.value.data.bval) {
        return result;
    }

    const auto* visual = nw::kernel::objects().components().find_visual(item.handle());
    if (!visual) {
        return result;
    }

    result.reserve(visual->models.size());
    for (const auto& row : visual->models) {
        if (row.kind == nw::ObjectVisualModelKind::item_model && !row.model.empty()) {
            result.push_back(row.model.string());
        }
    }
    return result;
}

} // namespace

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
    if (!std::filesystem::exists(model_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << model_path.string();
    }

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
    if (!std::filesystem::exists(model_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << model_path.string();
    }

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
    if (!std::filesystem::exists(model_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << model_path.string();
    }

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

    const std::filesystem::path creature_path{"test_data/user/development/drorry.utc"};
    if (!std::filesystem::exists(creature_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_path.string();
    }

    const auto report = viewer::build_preview_load_report(creature_path.string());

    EXPECT_EQ(report.kind, "dynamic_creature");
    EXPECT_TRUE(report_has_model_name(report, "pma0"));
    EXPECT_TRUE(report_has_model_name(report, "pma0_chest001"));
    EXPECT_TRUE(report_has_model_name(report, "pma0_head001"));
}

TEST(RenderViewerPreparedDraws, ItemLoadReportUsesVisualRows)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path item_path{"test_data/user/development/wduersc004.uti"};
    if (!std::filesystem::exists(item_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << item_path.string();
    }

    const auto report = viewer::build_preview_load_report(item_path.string());

    EXPECT_EQ(report.kind, "item");
    EXPECT_TRUE(report_has_model_name(report, "wswsc_b_044"));
    EXPECT_TRUE(report_has_model_name(report, "wswsc_m_054"));
    EXPECT_TRUE(report_has_model_name(report, "wswsc_t_044"));
    EXPECT_EQ(report.error_count(), 0u);
}

TEST(RenderViewerPreparedDraws, PlaceableLoadReportUsesVisualComponentRows)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path path{"test_data/user/development/arrowcorpse001.utp"};
    const auto visual = viewer::load_placeable_visual_from_file(path);
    ASSERT_TRUE(visual.loaded) << visual.error;
    EXPECT_EQ(visual.visual.hold_animation, nw::Resref{"default"});

    const auto report = viewer::build_preview_load_report(path.string());

    EXPECT_EQ(report.kind, "Placeable");
    EXPECT_TRUE(report_has_model_name(report, "plc_o01"));
    EXPECT_EQ(report.error_count(), 0u);
}

TEST(RenderViewerPreparedDraws, StandalonePlaceablePreviewIgnoresPersistedGameplayAnimationState)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path path{"test_data/user/development/placeable_deactivated.utp.json"};
    const auto visual = viewer::load_placeable_visual_from_file(path);

    ASSERT_TRUE(visual.loaded) << visual.error;
    EXPECT_EQ(visual.visual.hold_animation, nw::Resref{"default"});
}

TEST(RenderViewerPreparedDraws, DoorLoadReportUsesSmallsResolverRows)
{
    namespace viewer = nw::render::viewer;

    const auto report = viewer::build_preview_load_report("test_data/user/development/door_ttr_002.utd");

    EXPECT_EQ(report.kind, "Door");
    EXPECT_FALSE(report.model_names.empty());
    EXPECT_EQ(report.error_count(), 0u);
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportUsesVisualAttachmentRows)
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

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }

    std::filesystem::create_directories("tmp");
    const std::filesystem::path no_wing_path{"tmp/load_report_no_wing_creature.utc.json"};
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 0));
    ASSERT_TRUE(creature->save(no_wing_path, "json"));

    const std::filesystem::path wing_path{"tmp/load_report_wing_creature.utc.json"};
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 1));
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

    const std::filesystem::path creature_fixture{"test_data/user/development/drorry.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 1));

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
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model mindflayer appearance unavailable";
    }
    if (!nw::kernel::resman().contains({appearance->model, nw::ResourceType::mdl})) {
        GTEST_SKIP() << "mindflayer model resource unavailable";
    }

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nwn1::appearance_type_mindflayer));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 0));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 0));
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    std::filesystem::create_directories("tmp");
    const std::filesystem::path creature_path{"tmp/load_report_mindflayer_creature.utc.json"};
    ASSERT_TRUE(creature->save(creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());

    const auto report = viewer::build_preview_load_report(creature_path.string(), "cpause1");

    EXPECT_EQ(report.kind, "dynamic_creature");
    EXPECT_TRUE(report_has_model_name(report, appearance->model.view()));
    const auto geometry = std::find_if(
        report.geometries.begin(),
        report.geometries.end(),
        [&](const viewer::PreviewLoadGeometryReport& row) {
            return row.owner == appearance->model.view();
        });
    ASSERT_NE(geometry, report.geometries.end());
    EXPECT_GT(geometry->skinned_primitive_count, 0u);
    EXPECT_GT(geometry->skin_count, 0u);
    EXPECT_GT(geometry->skeleton_count, 0u);
    EXPECT_GT(geometry->animation_count, 0u);
    EXPECT_GT(geometry->particle_system_count, 0u);
}

TEST(RenderViewerPreparedDraws, PreviewSceneLoadOptionsHaveExplicitDefaults)
{
    namespace viewer = nw::render::viewer;

    const viewer::PreviewSceneLoadOptions options{};

    EXPECT_EQ(options.visual_render_mode, nw::ObjectVisualRenderMode::toolset);
    EXPECT_FALSE(options.area_object_editing);
}

TEST(RenderViewerPreparedDraws, PreviewSceneDestroysOwnedRootObject)
{
    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    const auto handle = creature->handle();

    {
        nw::render::viewer::PreviewScene scene;
        scene.root_object = handle;
        scene.active_object = handle;
        EXPECT_TRUE(nw::kernel::objects().valid(handle));
    }

    EXPECT_FALSE(nw::kernel::objects().valid(handle));
}

TEST(RenderViewerPreparedDraws, PreviewSceneDestroysAreaRootAndContainedObjects)
{
    auto* area = nw::kernel::objects().make<nw::Area>();
    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(creature, nullptr);
    area->creatures.push_back(creature);
    const auto area_handle = area->handle();
    const auto creature_handle = creature->handle();

    {
        nw::render::viewer::PreviewScene scene;
        scene.root_object = area_handle;
        scene.active_object = creature_handle;
    }

    EXPECT_FALSE(nw::kernel::objects().valid(creature_handle));
    EXPECT_FALSE(nw::kernel::objects().valid(area_handle));
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

TEST(RenderViewerPreparedDraws, ParticleMeshCacheImportsModernRenderModelOnce)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "offscreen graphics runtime unavailable";
    }

    nw::render::nwn::RenderAssetCache cache{gfx.context};
    const nw::render::ModelAssetTextureUploadDesc texture_upload{
        .ctx = gfx.context,
    };

    auto* model = cache.get_or_load_particle_mesh("plc_chunk_w01", texture_upload);
    EXPECT_NE(model, nullptr);
    if (model) {
        EXPECT_EQ(model->source_kind, nw::render::ModelAssetSourceKind::nwn);
        EXPECT_FALSE(model->primitives.empty());
        EXPECT_EQ(cache.get_or_load_particle_mesh("PLC_CHUNK_W01", texture_upload), model);
    }

    const auto populated = cache.stats();
    EXPECT_EQ(populated.particle_mesh_count, 1u);
    EXPECT_GT(populated.particle_mesh_payload_bytes, 0u);

    cache.clear();
    EXPECT_TRUE(cache.stats().empty());
}

TEST(RenderViewerPreparedDraws, MeshParticlePacketSubmitsModernCachedModel)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/plc_cndl02.mdl"};
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path.string();

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "offscreen graphics runtime unavailable";
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
    ASSERT_TRUE(session->load_model(model_path.string()));
    ASSERT_TRUE(session->select_animation("die"));
    session->tick(33);

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    std::string failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;

    const auto& stats = session->last_frame_stats();
    EXPECT_GT(stats.particle_mesh_packet_count, 0u);
    EXPECT_GT(stats.particle_mesh_particle_count, 0u);
    EXPECT_EQ(stats.particle_mesh_submitted_particle_count, stats.particle_mesh_particle_count);
    EXPECT_EQ(stats.particle_mesh_dropped_particle_count, 0u);
    EXPECT_EQ(stats.particle_mesh_missing_resref_packet_count, 0u);
    EXPECT_EQ(stats.particle_mesh_missing_model_packet_count, 0u);
    EXPECT_EQ(stats.particle_mesh_invalid_particle_index_count, 0u);
    EXPECT_EQ(stats.particle_mesh_invalid_particle_data_count, 0u);

    const auto cache_stats = nw::render::render_service().asset_cache().stats();
    EXPECT_EQ(cache_stats.particle_mesh_count, 1u);
    EXPECT_GT(cache_stats.particle_mesh_payload_bytes, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathCreatesStaticRenderModelPreview)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path model_path{"test_data/user/development/test_mtr_material.mdl"};
    if (!std::filesystem::exists(model_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << model_path.string();
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
    ASSERT_TRUE(session->load_model(model_path.string()));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->static_models.size(), 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), 1u);

    const auto* instance = scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
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
    EXPECT_EQ(stats.model_count, 1u);
    EXPECT_TRUE(stats.prepared_render_model_draws_enabled);
    EXPECT_GT(stats.prepared_render_model_draw_count, 0u);
    EXPECT_GT(stats.prepared_render_model_surface_submission.submitted_surface_count, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathCreatesStaticRenderModelCreaturePreview)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_bodak);
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const nw::Resource model_resource{appearance->model, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(model_resource)) {
        GTEST_SKIP() << "bodak model resource unavailable";
    }

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nwn1::appearance_type_bodak));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 0));
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
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    const auto active_object = session->active_object();
    EXPECT_EQ(active_object, scene->active_object);
    EXPECT_EQ(active_object.type, nw::ObjectType::creature);
    EXPECT_TRUE(nw::kernel::objects().valid(active_object));
    ASSERT_GE(scene->static_models.size(), 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());
    EXPECT_EQ(scene->load_report.kind, "dynamic_creature");
    ASSERT_GE(scene->load_report.model_names.size(), 1u);
    EXPECT_EQ(scene->load_report.model_names[0], scene->static_models[0]->name);

    const auto* instance = scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
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
    EXPECT_EQ(stats.model_count, scene->static_models.size());
    EXPECT_TRUE(stats.prepared_render_model_draws_enabled);
    EXPECT_GT(stats.prepared_render_model_draw_count, 0u);

    ASSERT_TRUE(session->fit_to_scene(viewport));
    const glm::mat4 fitted_view = session->camera().get_view_matrix();
    session->camera().pan(32.0f, 16.0f);
    const glm::mat4 edited_view = session->camera().get_view_matrix();
    EXPECT_NE(edited_view, fitted_view);
    const auto* scene_before_refresh = session->scene();
    ASSERT_TRUE(session->refresh_live_object_visual(active_object));
    EXPECT_EQ(session->scene(), scene_before_refresh);
    EXPECT_EQ(session->camera().get_view_matrix(), edited_view);
    EXPECT_EQ(session->active_object(), active_object);
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;
    ASSERT_TRUE(session->rebuild_live_object(active_object));
    EXPECT_EQ(session->camera().get_view_matrix(), fitted_view);

    session.reset();
    EXPECT_FALSE(nw::kernel::objects().valid(active_object));
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathAttachesEquippedHandItems)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_bodak);
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const nw::Resource model_resource{appearance->model, nw::ResourceType::mdl};
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
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nwn1::appearance_type_bodak));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 0));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 0));
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
    ASSERT_TRUE(session->load_object_file(creature_path));

    auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->static_models.size(), item_models.size() + 1u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());
    ASSERT_EQ(scene->model_attachments.size(), item_models.size());

    for (size_t i = 0; i < scene->model_attachments.size(); ++i) {
        const auto& binding = scene->model_attachments[i];
        EXPECT_EQ(binding.owner_instance_handle, scene->static_model_instance_handles[0]);
        EXPECT_EQ(binding.child_instance_handle, scene->static_model_instance_handles[i + 1u]);
        const auto* owner = scene->model_instances.get(binding.owner_instance_handle);
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->render_model_index, 0u);
        const auto* child = scene->model_instances.get(binding.child_instance_handle);
        ASSERT_NE(child, nullptr);
        EXPECT_EQ(child->render_model_index, static_cast<uint32_t>(i + 1u));
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
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model mindflayer appearance unavailable";
    }
    const nw::Resource model_resource{appearance->model, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(model_resource)) {
        GTEST_SKIP() << "mindflayer model resource unavailable";
    }

    auto* creature = nw::kernel::objects().load<nw::Creature>("nw_chicken");
    if (!creature) {
        GTEST_SKIP() << "nw_chicken creature blueprint unavailable";
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nwn1::appearance_type_mindflayer));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 0));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 0));
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
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
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
    EXPECT_EQ(stats.model_count, scene->static_models.size());
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

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathAssemblesHumanoidCreature)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_human);
    if (!appearance || appearance->model_type != nw::AppearanceModelType::parts) {
        GTEST_SKIP() << "humanoid human appearance unavailable";
    }

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nwn1::appearance_type_human));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "body_part_head", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "body_part_pelvis", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "body_part_torso", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 0));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 0));
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    const std::filesystem::path creature_path{"tmp/render_model_humanoid_creature.utc.json"};
    std::filesystem::create_directories(creature_path.parent_path());
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
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_GE(scene->static_models.size(), 4u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());

    const auto* base_rig = scene->static_models.front().get();
    const auto* base_rig_instance = scene->static_model_instance(0);
    ASSERT_NE(base_rig, nullptr);
    ASSERT_NE(base_rig_instance, nullptr);
    EXPECT_FALSE(base_rig_instance->visible);
    EXPECT_TRUE(base_rig_instance->scene_animation_enabled);
    EXPECT_TRUE(base_rig_instance->animation.enabled);
    EXPECT_TRUE(base_rig->primitives.empty());

    size_t body_attachment_count = 0;
    std::vector<glm::mat4> body_attachment_roots;
    for (const auto& binding : scene->model_attachments) {
        if (binding.owner_instance_handle != scene->static_model_instance_handles.front()) {
            continue;
        }
        const auto* child = scene->model_instances.get(binding.child_instance_handle);
        ASSERT_NE(child, nullptr);
        ASSERT_LT(child->render_model_index, scene->static_models.size());
        EXPECT_FALSE(child->scene_animation_enabled);
        EXPECT_FALSE(child->animation.enabled);
        body_attachment_roots.push_back(child->root_transform);
        ++body_attachment_count;
    }
    EXPECT_GE(body_attachment_count, 3u);

    const auto fallback_event = std::find_if(
        scene->load_report.events.begin(),
        scene->load_report.events.end(),
        [](const viewer::PreviewLoadEvent& event) {
            return event.category == "nwn_render_model_humanoid_fallback"
                && event.severity == viewer::PreviewLoadEventSeverity::warning;
        });
    EXPECT_EQ(fallback_event, scene->load_report.events.end());

    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    std::string failure;
    ASSERT_TRUE(session->select_animation("walk"));
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure, 100)) << failure;

    std::vector<glm::mat4> rendered_attachment_roots;
    rendered_attachment_roots.reserve(body_attachment_count);
    for (const auto& binding : scene->model_attachments) {
        if (binding.owner_instance_handle != scene->static_model_instance_handles.front()) {
            continue;
        }
        const auto* child = scene->model_instances.get(binding.child_instance_handle);
        ASSERT_NE(child, nullptr);
        rendered_attachment_roots.push_back(child->root_transform);
    }
    ASSERT_EQ(rendered_attachment_roots.size(), body_attachment_roots.size());

    const auto resync_stats = viewer::sync_model_instance_runtime_state(*session->scene());
    EXPECT_EQ(resync_stats.render_model_attachment_root_failed_count, 0u);
    bool animated_attachment_moved = false;
    size_t body_attachment_index = 0;
    for (const auto& binding : scene->model_attachments) {
        if (binding.owner_instance_handle != scene->static_model_instance_handles.front()) {
            continue;
        }
        const auto* child = scene->model_instances.get(binding.child_instance_handle);
        ASSERT_NE(child, nullptr);
        ASSERT_LT(body_attachment_index, rendered_attachment_roots.size());
        EXPECT_LT(
            max_abs_matrix_delta(child->root_transform, rendered_attachment_roots[body_attachment_index]),
            1.0e-5f);
        animated_attachment_moved = animated_attachment_moved
            || max_abs_matrix_delta(
                   rendered_attachment_roots[body_attachment_index],
                   body_attachment_roots[body_attachment_index])
                > 1.0e-4f;
        ++body_attachment_index;
    }
    EXPECT_TRUE(animated_attachment_moved);

    const auto& stats = session->last_frame_stats();
    EXPECT_EQ(stats.model_count, scene->static_models.size());
    EXPECT_GT(stats.prepared_render_model_draw_count, 0u);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathKeepsSideSpecificThighAttachments)
{
    namespace viewer = nw::render::viewer;

    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false);
    if (!module) {
        GTEST_SKIP() << "DockerDemo module fixture unavailable";
    }

    const std::filesystem::path creature_path{"test_data/user/development/drorry.utc.json"};
    if (!std::filesystem::exists(creature_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_path.string();
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
    ASSERT_TRUE(session->load_object_file(creature_path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_FALSE(scene->static_models.empty());
    ASSERT_FALSE(scene->static_model_instance_handles.empty());

    const auto* owner = scene->static_models.front().get();
    ASSERT_NE(owner, nullptr);
    const uint32_t left_thigh_socket = owner->socket_index("lthigh_g");
    const uint32_t right_thigh_socket = owner->socket_index("rthigh_g");
    ASSERT_NE(left_thigh_socket, nw::render::kInvalidModelNodeIndex);
    ASSERT_NE(right_thigh_socket, nw::render::kInvalidModelNodeIndex);

    using AttachedModel = std::pair<const nw::render::RenderModel*, const nw::render::ModelInstance*>;
    const auto model_at_socket = [scene](uint32_t socket) -> AttachedModel {
        for (const auto& binding : scene->model_attachments) {
            if (binding.owner_instance_handle != scene->static_model_instance_handles.front()
                || binding.owner_socket_index != socket) {
                continue;
            }
            const auto* child = scene->model_instances.get(binding.child_instance_handle);
            if (!child || child->render_model_index >= scene->static_models.size()) {
                return {};
            }
            return {scene->static_models[child->render_model_index].get(), child};
        }
        return {};
    };

    const auto assert_plt_material = [scene](const AttachedModel& attached) {
        const auto [model, instance] = attached;
        ASSERT_NE(model, nullptr);
        ASSERT_NE(instance, nullptr);
        ASSERT_EQ(model->materials.size(), 1u);
        EXPECT_TRUE(model->materials[0].albedo_uses_plt);
        EXPECT_TRUE(nw::gfx::bindless_texture_index_valid(model->materials[0].albedo_index));
        ASSERT_EQ(instance->material_override_handles.size(), 1u);
        const auto* material_override = scene->material_overrides.get(instance->material_override_handles[0]);
        ASSERT_NE(material_override, nullptr);
        EXPECT_TRUE(material_override->material.albedo_uses_plt);
        EXPECT_TRUE(material_override->material.plt_enabled);
    };

    const auto left_thigh = model_at_socket(left_thigh_socket);
    ASSERT_NE(left_thigh.first, nullptr);
    EXPECT_EQ(left_thigh.first->name, "pma0_legl004");
    assert_plt_material(left_thigh);

    const auto right_thigh = model_at_socket(right_thigh_socket);
    ASSERT_NE(right_thigh.first, nullptr);
    EXPECT_EQ(right_thigh.first->name, "pma0_legr004");
    assert_plt_material(right_thigh);
}

TEST(RenderViewerPreparedDraws, HumanoidCreatureWingAndTailUseAnimatedSocketAttachments)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nwn1::appearance_type_human);
    if (!appearance || appearance->model_type != nw::AppearanceModelType::parts) {
        GTEST_SKIP() << "humanoid human appearance unavailable";
    }

    for (const std::string_view table_name : {"wingmodel", "tailmodel"}) {
        auto* table = nw::kernel::twodas().get(table_name);
        std::string model_name;
        if (!table || !table->get_to(1u, "MODEL", model_name) || model_name.empty()
            || !nw::kernel::resman().contains({nw::Resref{model_name}, nw::ResourceType::mdl})) {
            GTEST_SKIP() << table_name << " row 1 model unavailable";
        }
    }

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nwn1::appearance_type_human));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "body_part_head", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "body_part_pelvis", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "body_part_torso", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 1));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 1));
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }

    std::filesystem::create_directories("tmp");
    const std::filesystem::path creature_path{"tmp/animated_humanoid_accessories.utc.json"};
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
    ASSERT_TRUE(session->load_object_file(creature_path));

    auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_GE(scene->static_models.size(), 6u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());

    const auto* owner = scene->static_models[0].get();
    ASSERT_NE(owner, nullptr);
    const uint32_t wing_socket = owner->socket_index("wings");
    const uint32_t tail_socket = owner->socket_index("tail");
    ASSERT_NE(wing_socket, nw::render::kInvalidModelNodeIndex);
    ASSERT_NE(tail_socket, nw::render::kInvalidModelNodeIndex);

    struct AnimatedAttachment {
        uint32_t model_index = 0;
        float animation_time = 0.0f;
    };
    std::array<AnimatedAttachment, 2> animated_attachments{};
    size_t animated_attachment_count = 0;
    for (const auto& binding : scene->model_attachments) {
        if (binding.owner_instance_handle != scene->static_model_instance_handles[0]
            || (binding.owner_socket_index != wing_socket && binding.owner_socket_index != tail_socket)) {
            continue;
        }

        ASSERT_LT(animated_attachment_count, animated_attachments.size());
        const auto* child_instance = scene->model_instances.get(binding.child_instance_handle);
        ASSERT_NE(child_instance, nullptr);
        ASSERT_LT(child_instance->render_model_index, scene->static_models.size());

        const auto* child = scene->static_models[child_instance->render_model_index].get();
        ASSERT_NE(child, nullptr);
        EXPECT_TRUE(child_instance->scene_animation_enabled);
        EXPECT_TRUE(child_instance->animation.enabled);
        ASSERT_LT(child_instance->animation.clip, child->animations.size());
        EXPECT_EQ(child->animations[child_instance->animation.clip].name,
            binding.owner_socket_index == wing_socket ? "default" : "pause1");
        EXPECT_EQ(binding.source_offset,
            nw::render::ModelAttachmentSourceOffsetPolicy::socket_bind);

        animated_attachments[animated_attachment_count++] = {
            .model_index = child_instance->render_model_index,
            .animation_time = child_instance->animation.time,
        };
    }
    ASSERT_EQ(animated_attachment_count, animated_attachments.size());

    const auto sync_stats = viewer::sync_model_instance_runtime_state(*scene);
    EXPECT_EQ(sync_stats.render_model_attachment_root_failed_count, 0u);

    session->tick(137);
    for (const auto& attachment : animated_attachments) {
        const auto* instance = scene->static_model_instance(attachment.model_index);
        ASSERT_NE(instance, nullptr);
        EXPECT_NEAR(instance->animation.time, attachment.animation_time + 0.137f, 1.0e-5f);
    }
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
    ASSERT_TRUE(session->load_model("c_aribeth"));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
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
    if (!std::filesystem::exists(model_path)) {
        GTEST_SKIP() << "development fixture unavailable: " << model_path.string();
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

    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false);
    if (!module) {
        GTEST_SKIP() << "DockerDemo module fixture unavailable";
    }

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
        const auto* visual = nw::kernel::objects().components().find_visual(creature->handle());
        if (!visual || visual->appearance < 0) {
            continue;
        }
        auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(visual->appearance));
        if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
            continue;
        }
        if (nw::kernel::resman().contains({appearance->model, nw::ResourceType::mdl})) {
            has_render_model_creature_candidate = true;
            break;
        }
    }
    area->clear();
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
    ASSERT_TRUE(session->load_area(area_resref.view()));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->is_area);
    EXPECT_EQ(scene->root_object.type, nw::ObjectType::area);
    ASSERT_TRUE(nw::kernel::objects().valid(scene->root_object));
    auto* live_area = nw::kernel::objects().get<nw::Area>(scene->root_object);
    ASSERT_NE(live_area, nullptr);
    EXPECT_GT(scene->static_models.size(), 0u);
    ASSERT_EQ(scene->static_model_instance_handles.size(), scene->static_models.size());

    ASSERT_NE(scene->area_render_scene, nullptr);
    const auto model_indices = scene->area_render_scene->model_indices();
    const auto record_kinds = scene->area_render_scene->kinds();
    ASSERT_EQ(model_indices.size(), record_kinds.size());

    size_t tile_record_count = 0;
    std::unordered_set<const nw::render::RenderModel*> unique_tile_models;
    for (size_t record_index = 0; record_index < record_kinds.size(); ++record_index) {
        if (record_kinds[record_index] != viewer::AreaRenderRecordKind::tile) {
            continue;
        }

        ++tile_record_count;
        const uint32_t model_index = model_indices[record_index];
        ASSERT_LT(model_index, scene->static_models.size());
        const auto* instance = scene->static_model_instance(model_index);
        ASSERT_NE(instance, nullptr);
        EXPECT_FALSE(instance->scene_animation_enabled);
        unique_tile_models.insert(scene->static_models[model_index].get());
    }
    EXPECT_EQ(tile_record_count, live_area->tiles.size());
    EXPECT_FALSE(unique_tile_models.empty());
    EXPECT_LT(unique_tile_models.size(), tile_record_count);

    const auto& area_prepared_draws = scene->area_render_scene->prepared_model_draw_list();
    EXPECT_EQ(scene->area_render_scene->stats().prepared_draw_count, area_prepared_draws.draws.size());
    EXPECT_GT(area_prepared_draws.stats.render_model_draw_count, 0u);

    const auto surface_ranges = scene->area_render_scene->surface_ranges();
    const auto surface_triangles = scene->area_render_scene->surface_triangles();
    ASSERT_FALSE(surface_ranges.empty());
    ASSERT_FALSE(surface_triangles.empty());
    EXPECT_EQ(scene->area_render_scene->stats().surface_range_count, surface_ranges.size());
    EXPECT_EQ(scene->area_render_scene->stats().surface_triangle_count, surface_triangles.size());
    const auto& surface_triangle = surface_triangles[surface_ranges.front().first_triangle];
    const glm::vec3 surface_centroid = (surface_triangle.v0 + surface_triangle.v1 + surface_triangle.v2) / 3.0f;
    const auto surface_hit = scene->area_render_scene->trace_surface(viewer::AreaObjectRay{
        .origin = {surface_centroid.x, surface_centroid.y, surface_ranges.front().bounds.max.z + 100.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    });
    ASSERT_EQ(surface_hit.status, viewer::AreaSurfaceHitStatus::hit);
    EXPECT_TRUE(std::isfinite(surface_hit.position.z));

    const auto kinds = scene->area_render_scene->kinds();
    const auto objects = scene->area_render_scene->object_handles();
    const auto flags = scene->area_render_scene->flags();
    ASSERT_EQ(objects.size(), kinds.size());
    ASSERT_EQ(objects.size(), flags.size());
    uint32_t selectable_record_count = 0;
    for (size_t record_index = 0; record_index < objects.size(); ++record_index) {
        const auto object = objects[record_index];
        if (kinds[record_index] == viewer::AreaRenderRecordKind::tile) {
            EXPECT_EQ(object.type, nw::ObjectType::invalid);
            continue;
        }
        if (kinds[record_index] == viewer::AreaRenderRecordKind::unknown) {
            continue;
        }
        if ((flags[record_index] & viewer::AreaRenderScene::RecordFlag::render_enabled) != 0u) {
            ++selectable_record_count;
        }
        EXPECT_TRUE(nw::kernel::objects().valid(object));
        const auto contains_object = [object](const auto& entries) {
            return std::any_of(entries.begin(), entries.end(), [object](const auto* entry) {
                return entry && entry->handle() == object;
            });
        };
        switch (kinds[record_index]) {
        case viewer::AreaRenderRecordKind::creature:
            EXPECT_EQ(object.type, nw::ObjectType::creature);
            EXPECT_TRUE(contains_object(live_area->creatures));
            break;
        case viewer::AreaRenderRecordKind::door:
            EXPECT_EQ(object.type, nw::ObjectType::door);
            EXPECT_TRUE(contains_object(live_area->doors));
            break;
        case viewer::AreaRenderRecordKind::item:
            EXPECT_EQ(object.type, nw::ObjectType::item);
            EXPECT_TRUE(contains_object(live_area->items));
            break;
        case viewer::AreaRenderRecordKind::placeable:
            EXPECT_EQ(object.type, nw::ObjectType::placeable);
            EXPECT_TRUE(contains_object(live_area->placeables));
            break;
        case viewer::AreaRenderRecordKind::tile:
        case viewer::AreaRenderRecordKind::unknown:
            FAIL() << "non-selectable render record reached object validation";
            break;
        }
    }
    EXPECT_EQ(selectable_record_count, scene->area_render_scene->stats().selectable_object_record_count);
    EXPECT_EQ(scene->area_render_scene->stats().object_handle_bytes,
        objects.size() * sizeof(nw::ObjectHandle));

    std::vector<float> render_model_animation_times;
    render_model_animation_times.reserve(scene->static_models.size());
    for (size_t model_index = 0; model_index < scene->static_models.size(); ++model_index) {
        const auto* instance = scene->static_model_instance(model_index);
        render_model_animation_times.push_back(instance ? instance->animation.time : 0.0f);
    }
    session->tick(137);
    for (size_t model_index = 0; model_index < scene->static_models.size(); ++model_index) {
        const auto& model = scene->static_models[model_index];
        const auto* instance = scene->static_model_instance(model_index);
        if (!instance) {
            continue;
        }
        const bool advances = instance->scene_animation_enabled
            && model
            && !model->animations.empty();
        const float expected_time = render_model_animation_times[model_index]
            + (advances ? 0.137f : 0.0f);
        EXPECT_NEAR(instance->animation.time, expected_time, 1.0e-5f);
        if (!advances) {
            EXPECT_FALSE(instance->animation.enabled);
        }
    }

    const viewer::ViewerViewport viewport{0, 0, 800, 600};
    ASSERT_TRUE(session->fit_to_scene(viewport));
    const glm::mat4 view_projection = session->camera().get_projection_matrix()
        * session->camera().get_view_matrix();
    const glm::vec4 tile_clip = view_projection * glm::vec4{surface_centroid, 1.0f};
    ASSERT_TRUE(std::isfinite(tile_clip.x));
    ASSERT_TRUE(std::isfinite(tile_clip.y));
    ASSERT_TRUE(std::isfinite(tile_clip.z));
    ASSERT_TRUE(std::isfinite(tile_clip.w));
    ASSERT_GT(tile_clip.w, 0.0f);
    const glm::vec3 tile_ndc = glm::vec3{tile_clip} / tile_clip.w;
    ASSERT_GE(tile_ndc.x, -1.0f);
    ASSERT_LE(tile_ndc.x, 1.0f);
    ASSERT_GE(tile_ndc.y, -1.0f);
    ASSERT_LE(tile_ndc.y, 1.0f);
    ASSERT_GE(tile_ndc.z, 0.0f);
    ASSERT_LE(tile_ndc.z, 1.0f);
    const auto tile_selection = session->select_area_object(
        (tile_ndc.x * 0.5f + 0.5f) * static_cast<float>(viewport.width),
        (tile_ndc.y * 0.5f + 0.5f) * static_cast<float>(viewport.height),
        viewport,
        viewer::AreaObjectSelectionTarget::tile);
    ASSERT_EQ(tile_selection.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(tile_selection.kind, viewer::AreaRenderRecordKind::tile);
    EXPECT_EQ(tile_selection.source, viewer::AreaObjectSelectionSource::area_record);
    EXPECT_EQ(tile_selection.object.type, nw::ObjectType::invalid);
    EXPECT_EQ(session->active_object().type, nw::ObjectType::invalid);
    const auto tile_selection_bounds = viewer::area_tile_selection_bounds(
        tile_selection, *scene->area_render_scene);
    ASSERT_TRUE(tile_selection_bounds);
    ASSERT_LT(tile_selection.record_index, scene->area_render_scene->root_transforms().size());
    EXPECT_FLOAT_EQ(
        tile_selection_bounds->min.z,
        scene->area_render_scene->root_transforms()[tile_selection.record_index][3].z);
    EXPECT_FLOAT_EQ(tile_selection_bounds->max.z, tile_selection_bounds->min.z + 1.0f);
    std::string render_failure;
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, render_failure)) << render_failure;

    const auto selected_object = std::find_if(objects.begin(), objects.end(), [](nw::ObjectHandle object) {
        return nw::kernel::objects().valid(object);
    });
    ASSERT_NE(selected_object, objects.end());
    const nw::ObjectHandle selected_handle = *selected_object;
    ASSERT_TRUE(session->set_area_object_selection(selected_handle));
    EXPECT_EQ(session->active_object(), selected_handle);
    const auto selected_bounds = viewer::collect_area_object_bounds(
        selected_handle,
        scene->area_render_scene->bounds(),
        scene->area_render_scene->flags(),
        scene->area_render_scene->kinds(),
        scene->area_render_scene->object_handles());
    ASSERT_EQ(selected_bounds.status, viewer::AreaObjectBoundsStatus::found);
    ASSERT_TRUE(session->focus_area_object_selection());
    EXPECT_EQ(session->camera().get_target(), selected_bounds.bounds.center());
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, render_failure)) << render_failure;
    EXPECT_TRUE(session->clear_area_object_selection());
    EXPECT_EQ(session->active_object().type, nw::ObjectType::invalid);
    EXPECT_FALSE(session->clear_area_object_selection());
    EXPECT_FALSE(session->set_area_object_selection(nw::ObjectHandle{}));
    EXPECT_EQ(session->active_object().type, nw::ObjectType::invalid);
    EXPECT_TRUE(session->set_area_object_selection(selected_handle));
    EXPECT_EQ(session->active_object(), selected_handle);
    EXPECT_FALSE(session->set_area_object_selection(scene->root_object));
    EXPECT_EQ(session->active_object(), selected_handle);

    ASSERT_FALSE(scene->debug_shape_selection_ranges.empty());
    const nw::ObjectHandle debug_selection_handle = scene->debug_shape_selection_ranges.front().object;
    EXPECT_TRUE(session->set_area_object_selection(debug_selection_handle));
    EXPECT_EQ(session->active_object(), debug_selection_handle);
    ASSERT_TRUE(session->focus_area_object_selection());
    EXPECT_EQ(
        session->camera().get_target(),
        scene->debug_shape_selection_ranges.front().bounds.center());
    EXPECT_TRUE(session->clear_area_object_selection());

    bool cleared_from_viewport = false;
    for (int32_t y = 0; y < 8 && !cleared_from_viewport; ++y) {
        for (int32_t x = 0; x < 8; ++x) {
            const float pixel_x = (static_cast<float>(x) + 0.5f)
                * static_cast<float>(viewport.width) / 8.0f;
            const float pixel_y = (static_cast<float>(y) + 0.5f)
                * static_cast<float>(viewport.height) / 8.0f;
            const auto selected = session->select_area_object(pixel_x, pixel_y, viewport);
            if (selected.status == viewer::AreaObjectSelectionStatus::miss) {
                EXPECT_EQ(session->active_object().type, nw::ObjectType::invalid);
                cleared_from_viewport = true;
                break;
            }
        }
    }
    EXPECT_TRUE(cleared_from_viewport);

    bool has_render_model_instance = false;
    for (size_t model_index = 0; model_index < scene->static_models.size(); ++model_index) {
        const auto* instance = scene->static_model_instance(model_index);
        if (instance) {
            has_render_model_instance = true;
            break;
        }
    }
    EXPECT_TRUE(has_render_model_instance);

    const nw::ObjectHandle area_handle = scene->root_object;
    const auto selected_for_rebuild = std::find_if(objects.begin(), objects.end(), [](nw::ObjectHandle object) {
        return object.type == nw::ObjectType::creature;
    });
    ASSERT_NE(selected_for_rebuild, objects.end());
    const nw::ObjectHandle selected_for_rebuild_handle = *selected_for_rebuild;
    const size_t record_count_before_rebuild = scene->area_render_scene->stats().record_count;
    const glm::mat4 view_before_rebuild = session->camera().get_view_matrix();
    ASSERT_TRUE(session->refresh_live_object_visual(selected_for_rebuild_handle));
    EXPECT_EQ(session->scene(), scene);
    EXPECT_EQ(session->camera().get_view_matrix(), view_before_rebuild);
    EXPECT_EQ(session->active_object(), selected_for_rebuild_handle);
    EXPECT_EQ(scene->area_render_scene->stats().record_count, record_count_before_rebuild);
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, render_failure)) << render_failure;
    const std::array selected_objects{selected_for_rebuild_handle};
    nw::toolset::CommandContext command_context;
    const auto duplicated = nw::toolset::duplicate_area_objects(
        area_handle, selected_objects, "Duplicate area object", command_context);
    ASSERT_TRUE(duplicated.ok()) << duplicated.message;
    const nw::ObjectHandle duplicated_handle = nw::toolset::object_mutation_state().object;
    ASSERT_TRUE(nw::kernel::objects().valid(duplicated_handle));

    ASSERT_TRUE(session->rebuild_live_area(area_handle, duplicated_handle));
    ASSERT_NE(session->scene(), nullptr);
    EXPECT_EQ(session->scene()->root_object, area_handle);
    EXPECT_EQ(session->active_object(), duplicated_handle);
    EXPECT_TRUE(nw::kernel::objects().valid(area_handle));
    ASSERT_NE(session->scene()->area_render_scene, nullptr);
    EXPECT_EQ(session->scene()->area_render_scene->stats().record_count, record_count_before_rebuild + 1);
    EXPECT_EQ(session->camera().get_view_matrix(), view_before_rebuild);
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, render_failure)) << render_failure;
    EXPECT_GT(session->last_frame_stats().area_frame_visible_record_count, 0u);
    EXPECT_GT(session->last_frame_stats().area_cache_prepared_draw_count, 0u);

    ASSERT_TRUE(duplicated.undo_action);
    const auto undone = duplicated.undo_action->undo(command_context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::toolset::object_mutation_state().object, selected_for_rebuild_handle);
    ASSERT_TRUE(session->rebuild_live_area(area_handle, selected_for_rebuild_handle));
    EXPECT_EQ(session->active_object(), selected_for_rebuild_handle);
    EXPECT_EQ(session->scene()->area_render_scene->stats().record_count, record_count_before_rebuild);

    ASSERT_TRUE(session->rebuild_live_area(area_handle, debug_selection_handle));
    EXPECT_EQ(session->active_object(), debug_selection_handle);

    auto* list_only_sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(list_only_sound, nullptr);
    ASSERT_TRUE(nw::kernel::objects().components().set_area(
        list_only_sound->handle(), area_handle.id));
    const glm::vec3 list_only_sound_position{14.0f, 25.0f, 3.0f};
    ASSERT_TRUE(nw::kernel::objects().components().set_position(
        list_only_sound->handle(), list_only_sound_position));
    live_area->sounds.push_back(list_only_sound);
    ASSERT_TRUE(session->set_area_object_selection(list_only_sound->handle()));
    EXPECT_EQ(session->active_object(), list_only_sound->handle());
    const glm::vec2 list_only_sound_approach = glm::normalize(
        glm::vec2{session->camera().get_position() - list_only_sound_position});
    ASSERT_TRUE(session->focus_area_object_selection());
    EXPECT_EQ(session->camera().get_target(), list_only_sound_position);
    EXPECT_NEAR(
        glm::dot(
            glm::normalize(glm::vec2{
                session->camera().get_position() - session->camera().get_target()}),
            list_only_sound_approach),
        1.0f,
        1.0e-5f);
    ASSERT_TRUE(session->rebuild_live_area(area_handle, list_only_sound->handle()));
    EXPECT_EQ(session->active_object(), list_only_sound->handle());

    session->clear();
    EXPECT_FALSE(nw::kernel::objects().valid(area_handle));
}
