#include <gtest/gtest.h>

#include "../tools/client/object_edits.hpp"
#include "../tools/ui/smalls_creature_properties.hpp"

#include <nw/formats/Image.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Waypoint.hpp>
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
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using namespace std::literals;

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

testing::AssertionResult resource_payloads_available(std::span<const nw::Resource> resources)
{
    for (const auto& resource : resources) {
        const auto data = nw::kernel::resman().demand(resource);
        if (data.bytes.size() == 0u) {
            return testing::AssertionFailure()
                << "resource is indexed but has no payload: " << resource.filename();
        }
    }
    return testing::AssertionSuccess();
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

bool set_object_propset_int(
    nw::ObjectHandle object, const char* propset_name, const char* field, int32_t value)
{
    if (!nw::kernel::objects().valid(object)) { return false; }

    auto& rt = nw::kernel::runtime();
    rt.init_object_propsets(object);
    const auto tid = rt.type_id(propset_name, false);
    if (tid == nw::smalls::invalid_type_id) { return false; }

    auto ref = rt.get_or_create_propset_ref(tid, object);
    if (ref.type_id == nw::smalls::invalid_type_id) { return false; }

    const auto* def = rt.get_struct_def(ref.type_id);
    if (!def) { return false; }

    const uint32_t index = def->field_index(field);
    if (index == std::numeric_limits<uint32_t>::max()) { return false; }

    const auto& field_def = def->fields[index];
    return rt.write_value_field_at_offset(ref, field_def.offset, rt.int_type(),
        nw::smalls::Value::make_int(value));
}

bool set_creature_appearance_propset_int(nw::Creature* creature, const char* field, int32_t value)
{
    return creature
        && set_object_propset_int(creature->handle(),
            "nwn1.propsets.CreatureAppearance", field, value);
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
            return nw::Resref{model_name} == nw::Resref{name};
        });
}

bool report_has_resource(
    const nw::render::viewer::PreviewLoadReport& report,
    nw::Resource resource)
{
    return std::any_of(
        report.resources.begin(),
        report.resources.end(),
        [resource](const nw::render::viewer::PreviewLoadResource& row) {
            return row.resource == resource;
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
    EXPECT_TRUE(report_has_resource(report, {"pma0"sv, nw::ResourceType::mdl}));
    EXPECT_TRUE(report_has_resource(report, {"pma0_chest001"sv, nw::ResourceType::mdl}));
    EXPECT_TRUE(report_has_resource(report, {"pma0_head001"sv, nw::ResourceType::mdl}));
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
    EXPECT_TRUE(report_has_resource(report, {"wswsc_b_044"sv, nw::ResourceType::mdl}));
    EXPECT_TRUE(report_has_resource(report, {"wswsc_m_054"sv, nw::ResourceType::mdl}));
    EXPECT_TRUE(report_has_resource(report, {"wswsc_t_044"sv, nw::ResourceType::mdl}));
}

TEST(RenderViewerPreparedDraws, StandaloneItemVisualEditRefreshesInPlace)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path item_path{
        "test_data/user/development/wduersc004.uti"};
    if (!std::filesystem::exists(item_path)) {
        GTEST_SKIP() << "development fixture unavailable: "
                     << item_path.string();
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
    if (!device.initialize(
            viewer::ViewerDeviceOptions{.shader_roots = shader_roots})) {
        GTEST_SKIP() << "viewer device unavailable";
    }

    auto session = device.make_session();
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->load_object_file(item_path));
    auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    const nw::ObjectHandle item = session->active_object();
    ASSERT_EQ(item.type, nw::ObjectType::item);
    ASSERT_EQ(scene->root_object, item);

    const auto* visuals
        = nw::kernel::objects().components().find_item_visuals(item);
    ASSERT_NE(visuals, nullptr);
    std::optional<nw::toolset::ObjectEditBatch> edit;
    int32_t edited_part = -1;
    int32_t edited_value = -1;
    for (size_t part = 0;
        part < nw::ObjectItemVisualState::model_part_count && !edit;
        ++part) {
        const int32_t current = visuals->model_parts[part];
        for (int32_t value = 0; value < 256; ++value) {
            if (value == current) {
                continue;
            }
            const std::array parts{static_cast<int32_t>(part)};
            const std::array values{value};
            auto candidate = nw::toolset::make_item_model_part_edits(
                nw::kernel::runtime(), item, parts, values);
            if (candidate) {
                edited_part = parts.front();
                edited_value = value;
                edit = std::move(candidate);
                break;
            }
        }
    }
    ASSERT_TRUE(edit) << "fixture exposes no alternate Item model";

    ASSERT_FALSE(scene->static_models.empty());
    const std::vector<std::string> model_names_before
        = scene->load_report.model_names;
    const auto* root_before = scene->static_model_instance(0);
    ASSERT_NE(root_before, nullptr);
    const glm::mat4 root_transform_before = root_before->root_transform;
    constexpr viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = 256,
        .height = 256,
    };
    ASSERT_TRUE(session->fit_to_scene(viewport));
    session->camera().pan(24.0f, 12.0f);
    const glm::mat4 camera_before
        = session->camera().get_view_matrix();

    const auto applied = nw::toolset::apply_object_edits(
        nw::kernel::runtime(), *edit,
        nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    ASSERT_EQ(nw::kernel::objects().components().find_item_visuals(item)->model_parts[static_cast<size_t>(edited_part)],
        edited_value);

    ASSERT_TRUE(session->refresh_live_object_visual(item));
    EXPECT_EQ(session->scene(), scene);
    EXPECT_EQ(session->active_object(), item);
    EXPECT_LT(max_abs_matrix_delta(
                  session->camera().get_view_matrix(), camera_before),
        1.0e-5f);
    ASSERT_FALSE(scene->static_models.empty());
    EXPECT_NE(scene->load_report.model_names, model_names_before);
    const auto* root_after = scene->static_model_instance(0);
    ASSERT_NE(root_after, nullptr);
    EXPECT_LT(max_abs_matrix_delta(
                  root_after->root_transform, root_transform_before),
        1.0e-5f);

    std::string failure;
    ASSERT_TRUE(render_viewer_frame(
        gfx.context, *session, viewport, failure))
        << failure;
}

TEST(RenderViewerPreparedDraws, PlaceableLoadReportUsesVisualComponentRows)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path path{"test_data/user/development/arrowcorpse001.utp"};
    const auto visual = viewer::load_placeable_visual_from_file(path);
    ASSERT_TRUE(visual.loaded) << visual.error;
    EXPECT_EQ(visual.visual.hold_animation, nw::Resref{"default"sv});

    const auto report = viewer::build_preview_load_report(path.string());

    EXPECT_EQ(report.kind, "Placeable");
    EXPECT_TRUE(report_has_resource(report, {"plc_o01"sv, nw::ResourceType::mdl}));
}

TEST(RenderViewerPreparedDraws, StandalonePlaceablePreviewIgnoresPersistedGameplayAnimationState)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path path{"test_data/user/development/placeable_deactivated.utp.json"};
    const auto visual = viewer::load_placeable_visual_from_file(path);

    ASSERT_TRUE(visual.loaded) << visual.error;
    EXPECT_EQ(visual.visual.hold_animation, nw::Resref{"default"sv});
}

TEST(RenderViewerPreparedDraws, StandalonePlaceablePreviewPublishesLivePlaceable)
{
    namespace viewer = nw::render::viewer;

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
    ASSERT_TRUE(session->load_object_file(
        "test_data/user/development/arrowcorpse001.utp"sv));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    const auto active_object = session->active_object();
    EXPECT_EQ(active_object, scene->root_object);
    EXPECT_EQ(active_object, scene->active_object);
    EXPECT_EQ(active_object.type, nw::ObjectType::placeable);
    EXPECT_TRUE(nw::kernel::objects().valid(active_object));
    EXPECT_NE(nw::kernel::objects().get<nw::Placeable>(active_object), nullptr);

    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);
    nw::toolset::ObjectDetailsSnapshot details;
    nw::toolset::build_object_details(runtime, active_object, details);
    EXPECT_EQ(details.status, nw::toolset::ObjectDetailsStatus::ready)
        << details.diagnostic;
    EXPECT_FALSE(details.rows.empty());

    session.reset();
    EXPECT_FALSE(nw::kernel::objects().valid(active_object));
}

TEST(RenderViewerPreparedDraws, DoorLoadReportUsesSmallsResolverRows)
{
    namespace viewer = nw::render::viewer;

    const std::filesystem::path path{"test_data/user/development/door_ttr_002.utd"};
    const auto resolved = viewer::resolve_door_model_from_file(path);
    ASSERT_TRUE(resolved.valid) << resolved.error;
    const auto report = viewer::build_preview_load_report(path.string());

    EXPECT_EQ(report.kind, "Door");
    EXPECT_TRUE(report_has_resource(report, {resolved.model, nw::ResourceType::mdl}));
}

TEST(RenderViewerPreparedDraws, StandaloneDoorPreviewPublishesLiveDoor)
{
    namespace viewer = nw::render::viewer;

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

    struct GenericDoorCandidate {
        int32_t row = 0;
        nw::Resref model;
    };

    auto* genericdoors = nw::kernel::twodas().get("genericdoors"sv);
    ASSERT_NE(genericdoors, nullptr);

    std::vector<GenericDoorCandidate> candidates;
    candidates.reserve(2);
    for (size_t row = 0; row < genericdoors->rows() && candidates.size() < 2; ++row) {
        std::string model_name;
        if (!genericdoors->get_to(row, "ModelName", model_name, false)) { continue; }
        const nw::Resref candidate{model_name};
        if (candidate.empty()
            || !nw::kernel::resman().contains(
                {candidate, nw::ResourceType::mdl})) {
            continue;
        }
        if (std::any_of(candidates.begin(), candidates.end(),
                [candidate](const GenericDoorCandidate& existing) {
                    return existing.model == candidate;
                })) {
            continue;
        }

        auto probe = device.make_session();
        if (!probe || !probe->load_model(candidate.view())) { continue; }
        candidates.push_back({static_cast<int32_t>(row), candidate});
    }
    ASSERT_EQ(candidates.size(), 2u)
        << "dedicated-server data has fewer than two renderable Generic Doors";

    const std::filesystem::path source_path{
        "test_data/user/development/door_ttr_002.utd"};
    auto* source_door = nw::kernel::objects().load_file<nw::Door>(source_path);
    ASSERT_NE(source_door, nullptr);

    auto& runtime = nw::kernel::runtime();
    const auto source_appearance = nw::toolset::door_appearance(
        runtime, source_door->handle());
    ASSERT_TRUE(source_appearance);
    const size_t initial_candidate = source_appearance->appearance == 0
            && source_appearance->generic_type == candidates[0].row
        ? 1u
        : 0u;
    const size_t alternate_candidate = initial_candidate == 0u ? 1u : 0u;
    auto initial_edit = nw::toolset::make_door_appearance_edit(
        runtime, source_door->handle(), 0, candidates[initial_candidate].row);
    ASSERT_TRUE(initial_edit);
    const auto initial_applied = nw::toolset::apply_object_appearance_edit(
        runtime, *initial_edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(initial_applied.ok()) << initial_applied.diagnostic;

    std::filesystem::create_directories("tmp");
    const std::filesystem::path path{
        "tmp/standalone_generic_door.utd.json"};
    ASSERT_TRUE(source_door->save(path, "json"));
    nw::kernel::objects().destroy(source_door->handle());

    auto session = device.make_session();
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->load_object_file(path));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    const auto active_object = session->active_object();
    EXPECT_EQ(active_object, scene->root_object);
    EXPECT_EQ(active_object, scene->active_object);
    EXPECT_EQ(active_object.type, nw::ObjectType::door);
    EXPECT_TRUE(nw::kernel::objects().valid(active_object));
    EXPECT_NE(nw::kernel::objects().get<nw::Door>(active_object), nullptr);

    const size_t initial_light_count = scene->local_lights.size();
    ASSERT_TRUE(session->rebuild_live_object(active_object));
    scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->local_lights.size(), initial_light_count);

    const auto before = nw::toolset::door_appearance(runtime, active_object);
    ASSERT_TRUE(before);
    EXPECT_EQ(before->appearance, 0);
    EXPECT_EQ(before->generic_type, candidates[initial_candidate].row);
    EXPECT_TRUE(report_has_model_name(
        scene->load_report, candidates[initial_candidate].model.view()));

    auto edit = nw::toolset::make_door_appearance_edit(
        runtime, active_object, 0, candidates[alternate_candidate].row);
    ASSERT_TRUE(edit);
    const auto applied = nw::toolset::apply_object_appearance_edit(
        runtime, *edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    ASSERT_TRUE(session->rebuild_live_object(active_object));
    scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(session->active_object(), active_object);
    EXPECT_TRUE(report_has_model_name(
        scene->load_report, candidates[alternate_candidate].model.view()));

    session.reset();
    EXPECT_FALSE(nw::kernel::objects().valid(active_object));
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportUsesVisualAttachmentRows)
{
    namespace viewer = nw::render::viewer;

    auto* wingmodel = nw::kernel::twodas().get("wingmodel"sv);
    std::string wing_model;
    if (!wingmodel || !wingmodel->get_to(1u, "MODEL", wing_model) || wing_model.empty()) {
        GTEST_SKIP() << "wingmodel row 1 unavailable";
    }
    const nw::Resource wing_resource{std::string_view{wing_model}, nw::ResourceType::mdl};
    const std::array wing_resources{wing_resource};
    ASSERT_TRUE(resource_payloads_available(wing_resources));

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
    EXPECT_FALSE(report_has_resource(no_wing_report, wing_resource));
    EXPECT_TRUE(report_has_resource(wing_report, wing_resource));
}

TEST(RenderViewerPreparedDraws, DynamicCreatureLoadReportCountsWingRowPolicy)
{
    namespace viewer = nw::render::viewer;

    auto* wingmodel = nw::kernel::twodas().get("wingmodel"sv);
    std::string wing_model;
    if (!wingmodel || !wingmodel->get_to(1u, "MODEL", wing_model) || wing_model.empty()) {
        GTEST_SKIP() << "wingmodel row 1 unavailable";
    }
    const nw::Resource wing_resource{std::string_view{wing_model}, nw::ResourceType::mdl};
    const std::array wing_resources{wing_resource};
    ASSERT_TRUE(resource_payloads_available(wing_resources));

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

    auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(413));
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model mindflayer appearance unavailable";
    }
    const std::array model_resources{
        nw::Resource{appearance->model, nw::ResourceType::mdl},
    };
    ASSERT_TRUE(resource_payloads_available(model_resources));

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(413)));
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

TEST(RenderViewerPreparedDraws, NonVisualDataBlueprintPreviewsPublishOwnedLiveObjects)
{
    namespace viewer = nw::render::viewer;

    struct BlueprintCase {
        std::string_view path;
        nw::ObjectType type = nw::ObjectType::invalid;
    };

    constexpr std::array cases{
        BlueprintCase{"test_data/user/development/blue_bell.uts"sv,
            nw::ObjectType::sound},
        BlueprintCase{"test_data/user/development/storethief002.utm"sv,
            nw::ObjectType::store},
        BlueprintCase{"test_data/user/development/pl_spray_sewage.utt"sv,
            nw::ObjectType::trigger},
    };

    viewer::PreviewRenderResources resources{nullptr};
    for (const auto& blueprint : cases) {
        SCOPED_TRACE(blueprint.path);

        auto scene = viewer::load_preview_scene(resources, blueprint.path);
        ASSERT_NE(scene, nullptr);
        EXPECT_TRUE(scene->static_models.empty());
        EXPECT_EQ(scene->root_object, scene->active_object);
        EXPECT_EQ(scene->root_object.type, blueprint.type);
        EXPECT_TRUE(nw::kernel::objects().valid(scene->root_object));

        const auto handle = scene->root_object;
        scene.reset();
        EXPECT_FALSE(nw::kernel::objects().valid(handle));
    }
}

TEST(RenderViewerPreparedDraws, EncounterBlueprintPreviewsAndRebuildsSpawnGroup)
{
    namespace viewer = nw::render::viewer;

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
    ASSERT_TRUE(session->load_object_file(
        "test_data/user/development/boundelementallo.ute"sv));

    const auto encounter = session->active_object();
    ASSERT_EQ(encounter.type, nw::ObjectType::encounter);
    ASSERT_TRUE(nw::kernel::objects().valid(encounter));
    const auto before = nw::toolset::snapshot_encounter_spawns(
        nw::kernel::runtime(), encounter);
    ASSERT_TRUE(before);
    ASSERT_FALSE(before->empty());

    auto two_spawns = std::vector<nw::toolset::EncounterSpawnRecord>(
        2, before->front());
    for (auto& spawn : two_spawns) {
        spawn.resref = nw::Resref{"nw_chicken"sv};
    }
    const auto two_applied = nw::toolset::apply_encounter_spawn_edit(
        nw::kernel::runtime(),
        {encounter, *before, two_spawns},
        nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(two_applied.ok()) << two_applied.diagnostic;
    ASSERT_TRUE(session->rebuild_live_object(encounter));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->root_object, encounter);
    ASSERT_EQ(scene->active_object, encounter);
    ASSERT_FALSE(scene->static_models.empty());
    ASSERT_EQ(scene->static_models.size() % 2, 0u);
    const size_t models_per_creature = scene->static_models.size() / 2;
    ASSERT_GT(models_per_creature, 0u);
    const auto* first = scene->static_model_instance(0);
    const auto* second = scene->static_model_instance(models_per_creature);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_GT(glm::distance(
                  glm::vec3{first->root_transform[3]},
                  glm::vec3{second->root_transform[3]}),
        0.5f);

    auto three_spawns = two_spawns;
    three_spawns.push_back(two_spawns.front());
    const auto three_applied = nw::toolset::apply_encounter_spawn_edit(
        nw::kernel::runtime(),
        {encounter, two_spawns, three_spawns},
        nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(three_applied.ok()) << three_applied.diagnostic;
    ASSERT_TRUE(session->rebuild_live_object(encounter));
    ASSERT_NE(session->scene(), nullptr);
    EXPECT_EQ(session->scene()->static_models.size(), models_per_creature * 3);
    EXPECT_EQ(session->active_object(), encounter);

    session.reset();
    EXPECT_FALSE(nw::kernel::objects().valid(encounter));
}

TEST(RenderViewerPreparedDraws, WaypointBlueprintLoadsAppearanceModel)
{
    namespace viewer = nw::render::viewer;

    constexpr auto waypoint_model = "gi_waypoint02"sv;
    const nw::Resource model_resource{
        nw::Resref{waypoint_model}, nw::ResourceType::mdl};
    const std::array model_resources{model_resource};
    ASSERT_TRUE(resource_payloads_available(model_resources));

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
    ASSERT_TRUE(session->load_object_file(
        "test_data/user/development/wp_behexit001.utw"sv));

    const auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_FALSE(scene->static_models.empty());
    EXPECT_TRUE(report_has_model_name(scene->load_report, waypoint_model));
    EXPECT_EQ(scene->root_object.type, nw::ObjectType::waypoint);
    EXPECT_EQ(scene->root_object, scene->active_object);
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
    constexpr auto model_name = "plc_cndl02"sv;
    const std::array resources{nw::Resource{model_name, nw::ResourceType::mdl}};
    ASSERT_TRUE(resource_payloads_available(resources));

    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "offscreen graphics runtime unavailable";
    }

    nw::render::nwn::RenderAssetCache cache{gfx.context};
    const nw::render::ModelAssetTextureUploadDesc texture_upload{
        .ctx = gfx.context,
    };

    auto* model = cache.get_or_load_particle_mesh(model_name, texture_upload);
    EXPECT_NE(model, nullptr);
    if (model) {
        EXPECT_EQ(model->source_kind, nw::render::ModelAssetSourceKind::nwn);
        EXPECT_FALSE(model->primitives.empty());
        EXPECT_EQ(cache.get_or_load_particle_mesh("PLC_CNDL02", texture_upload), model);
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
    const std::array resources{
        nw::Resource{"plc_chunk_w01"sv, nw::ResourceType::mdl},
    };
    ASSERT_TRUE(resource_payloads_available(resources));

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

    auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(23));
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const nw::Resource model_resource{appearance->model, nw::ResourceType::mdl};
    const std::array model_resources{model_resource};
    ASSERT_TRUE(resource_payloads_available(model_resources));

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(23)));
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
    const glm::mat4 root_transform_before_refresh = instance->root_transform;

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
    EXPECT_LT(max_abs_matrix_delta(session->camera().get_view_matrix(), edited_view), 1.0e-5f);
    EXPECT_EQ(session->active_object(), active_object);
    const auto* instance_after_refresh = session->scene()->static_model_instance(0);
    ASSERT_NE(instance_after_refresh, nullptr);
    EXPECT_LT(max_abs_matrix_delta(
                  instance_after_refresh->root_transform,
                  root_transform_before_refresh),
        1.0e-5f);
    ASSERT_TRUE(render_viewer_frame(gfx.context, *session, viewport, failure)) << failure;
    ASSERT_TRUE(session->rebuild_live_object(active_object));
    // Rebuilding refits from a freshly primed animated model. Its sampled
    // bounds may move by a small amount, so test stable framing rather than
    // byte-identical floating-point matrices.
    EXPECT_LT(max_abs_matrix_delta(session->camera().get_view_matrix(), fitted_view), 5.0e-3f);

    session.reset();
    EXPECT_FALSE(nw::kernel::objects().valid(active_object));
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathAttachesEquippedHandItems)
{
    namespace viewer = nw::render::viewer;

    auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(23));
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const nw::Resource model_resource{appearance->model, nw::ResourceType::mdl};
    const std::array model_resources{model_resource};
    ASSERT_TRUE(resource_payloads_available(model_resources));

    constexpr auto item_resref = "nw_wswbs001"sv;
    auto* item = nw::kernel::objects().load<nw::Item>(item_resref);
    if (!item) {
        GTEST_SKIP() << "test hand item unavailable";
    }
    const auto item_models = item_model_resrefs_for_test(*item);
    if (item_models.empty()) {
        GTEST_SKIP() << "test hand item has no model parts";
    }
    for (const auto& item_model : item_models) {
        ASSERT_GT(nw::kernel::resman()
                      .demand({nw::Resref{item_model}, nw::ResourceType::mdl})
                      .bytes.size(),
            0u)
            << "test hand item model unavailable: " << item_model;
    }

    auto* creature = nw::kernel::objects().load<nw::Creature>("nw_chicken"sv);
    if (!creature) {
        GTEST_SKIP() << "nw_chicken creature blueprint unavailable";
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(23)));
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

    auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(413));
    if (!appearance || appearance->model_type == nw::AppearanceModelType::parts || appearance->model.empty()) {
        GTEST_SKIP() << "single-model mindflayer appearance unavailable";
    }
    const nw::Resource model_resource{appearance->model, nw::ResourceType::mdl};
    const std::array model_resources{model_resource};
    ASSERT_TRUE(resource_payloads_available(model_resources));

    auto* creature = nw::kernel::objects().load<nw::Creature>("nw_chicken"sv);
    if (!creature) {
        GTEST_SKIP() << "nw_chicken creature blueprint unavailable";
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(413)));
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

    const std::array model_resources{
        nw::Resource{"pfh0"sv, nw::ResourceType::mdl},
        nw::Resource{"pfh0_head001"sv, nw::ResourceType::mdl},
        nw::Resource{"pfh0_pelvis001"sv, nw::ResourceType::mdl},
        nw::Resource{"pfh0_chest001"sv, nw::ResourceType::mdl},
    };
    ASSERT_TRUE(resource_payloads_available(model_resources));

    auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(6));
    if (!appearance || appearance->model_type != nw::AppearanceModelType::parts) {
        GTEST_SKIP() << "humanoid human appearance unavailable";
    }
    auto* replacement_appearance = nw::kernel::rules().appearances.get(
        nw::Appearance::make(23));
    if (!replacement_appearance
        || replacement_appearance->model_type == nw::AppearanceModelType::parts
        || replacement_appearance->model.empty()) {
        GTEST_SKIP() << "single-model bodak appearance unavailable";
    }
    const std::array replacement_resources{
        nw::Resource{replacement_appearance->model, nw::ResourceType::mdl},
    };
    ASSERT_TRUE(resource_payloads_available(replacement_resources));

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(6)));
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
    const glm::mat4 root_transform_before_refresh = base_rig_instance->root_transform;
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

    const auto active_object = session->active_object();
    auto* active_creature = nw::kernel::objects().get<nw::Creature>(active_object);
    ASSERT_NE(active_creature, nullptr);
    ASSERT_TRUE(session->fit_to_scene(viewport));
    session->camera().orbit(35.0f, -7.0f);
    const glm::mat4 camera_before_refresh = session->camera().get_view_matrix();
    const std::string model_name_before_refresh = scene->static_models.front()->name;
    auto appearance_edit = nw::toolset::make_object_appearance_edit(
        nw::kernel::runtime(), active_object, *nw::Appearance::make(23));
    ASSERT_TRUE(appearance_edit);
    const auto appearance_result = nw::toolset::apply_object_appearance_edit(
        nw::kernel::runtime(), *appearance_edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(appearance_result.ok()) << appearance_result.diagnostic;
    ASSERT_TRUE(session->refresh_live_object_visual(active_object));
    EXPECT_EQ(session->scene(), scene);
    EXPECT_LT(max_abs_matrix_delta(
                  session->camera().get_view_matrix(), camera_before_refresh),
        1.0e-5f);
    ASSERT_FALSE(scene->static_models.empty());
    EXPECT_NE(scene->static_models.front()->name, model_name_before_refresh);
    const auto* root_after_refresh = scene->static_model_instance(0);
    ASSERT_NE(root_after_refresh, nullptr);
    EXPECT_LT(max_abs_matrix_delta(
                  root_after_refresh->root_transform,
                  root_transform_before_refresh),
        1.0e-5f);
}

TEST(RenderViewerPreparedDraws, NwnRenderModelLoadPathKeepsSideSpecificThighAttachments)
{
    namespace viewer = nw::render::viewer;

    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false);
    if (!module) {
        GTEST_SKIP() << "DockerDemo module fixture unavailable";
    }

    const std::array required_resources{
        nw::Resource{"pma0"sv, nw::ResourceType::mdl},
        nw::Resource{"pma0_legl004"sv, nw::ResourceType::mdl},
        nw::Resource{"pma0_legr004"sv, nw::ResourceType::mdl},
        nw::Resource{"pmh0_legl004"sv, nw::ResourceType::plt},
        nw::Resource{"pmh0_legr004"sv, nw::ResourceType::plt},
    };
    ASSERT_TRUE(resource_payloads_available(required_resources));

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

    auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(6));
    if (!appearance || appearance->model_type != nw::AppearanceModelType::parts) {
        GTEST_SKIP() << "humanoid human appearance unavailable";
    }

    std::array<nw::Resource, 2> attachment_resources{};
    size_t attachment_resource_count = 0;
    for (const std::string_view table_name : {"wingmodel"sv, "tailmodel"sv}) {
        auto* table = nw::kernel::twodas().get(table_name);
        std::string model_name;
        if (!table || !table->get_to(1u, "MODEL", model_name) || model_name.empty()) {
            GTEST_SKIP() << table_name << " row 1 model unavailable";
        }
        attachment_resources[attachment_resource_count++] = {model_name, nw::ResourceType::mdl};
    }
    ASSERT_EQ(attachment_resource_count, attachment_resources.size());
    ASSERT_TRUE(resource_payloads_available(attachment_resources));

    const std::filesystem::path creature_fixture{"test_data/user/development/nw_chicken.utc"};
    if (!std::filesystem::exists(creature_fixture)) {
        GTEST_SKIP() << "development fixture unavailable: " << creature_fixture.string();
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(creature_fixture.string());
    if (!creature) {
        GTEST_SKIP() << "development fixture failed to load: " << creature_fixture.string();
    }
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(6)));
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

    const std::array resources{
        nw::Resource{"c_aribeth"sv, nw::ResourceType::mdl},
    };
    ASSERT_TRUE(resource_payloads_available(resources));

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
    ASSERT_NE(module, nullptr) << "DockerDemo module fixture unavailable";

    const nw::Resref area_resref{"test_area"sv};
    const std::array area_resources{nw::Resource{area_resref, nw::ResourceType::are}};
    ASSERT_TRUE(resource_payloads_available(area_resources));

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

    auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(scene->is_area);
    EXPECT_EQ(scene->root_object.type, nw::ObjectType::area);
    ASSERT_TRUE(nw::kernel::objects().valid(scene->root_object));
    auto* live_area = nw::kernel::objects().get<nw::Area>(scene->root_object);
    ASSERT_NE(live_area, nullptr);
    const nw::ObjectHandle area_handle = scene->root_object;

    auto* area_waypoint = nw::kernel::objects().make<nw::Waypoint>();
    ASSERT_NE(area_waypoint, nullptr);
    ASSERT_TRUE(nw::kernel::objects().components().set_area(
        area_waypoint->handle(), area_handle.id));
    ASSERT_TRUE(nw::kernel::objects().components().set_position(
        area_waypoint->handle(), glm::vec3{15.0f, 15.0f, 0.0f}));
    ASSERT_TRUE(nw::kernel::objects().components().set_orientation(
        area_waypoint->handle(), glm::vec3{1.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(set_object_propset_int(
        area_waypoint->handle(), "nwn1.propsets.WaypointState", "appearance", 2));
    live_area->waypoints.push_back(area_waypoint);
    ASSERT_TRUE(session->rebuild_live_area(area_handle, area_waypoint->handle()));
    ASSERT_TRUE(session->clear_area_object_selection());
    scene = session->scene();
    ASSERT_NE(scene, nullptr);
    live_area = nw::kernel::objects().get<nw::Area>(scene->root_object);
    ASSERT_NE(live_area, nullptr);
    ASSERT_NE(scene->area_render_scene, nullptr);
    EXPECT_EQ(scene->area_render_scene->stats().waypoint_record_count,
        live_area->waypoints.size());
    bool found_area_waypoint = false;
    const auto area_record_kinds = scene->area_render_scene->kinds();
    const auto area_record_objects = scene->area_render_scene->object_handles();
    ASSERT_EQ(area_record_kinds.size(), area_record_objects.size());
    for (size_t record_index = 0; record_index < area_record_kinds.size(); ++record_index) {
        if (area_record_kinds[record_index] == viewer::AreaRenderRecordKind::waypoint
            && area_record_objects[record_index] == area_waypoint->handle()) {
            found_area_waypoint = true;
            ASSERT_LT(record_index, scene->area_render_scene->root_transforms().size());
            const glm::vec3 forward = scene->area_render_scene->root_transforms()[record_index]
                * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
            EXPECT_NEAR(forward.x, 1.0f, 1.0e-5f);
            EXPECT_NEAR(forward.y, 0.0f, 1.0e-5f);
            break;
        }
    }
    EXPECT_TRUE(found_area_waypoint);
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
    const viewer::ViewerRay surface_ray{
        .origin = {surface_centroid.x, surface_centroid.y, surface_ranges.front().bounds.max.z + 100.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    const auto surface_hit = scene->area_render_scene->trace_surface(surface_ray);
    ASSERT_EQ(surface_hit.status, viewer::AreaSurfaceHitStatus::hit);
    EXPECT_TRUE(std::isfinite(surface_hit.position.z));

    const auto kinds = scene->area_render_scene->kinds();
    const auto objects = scene->area_render_scene->object_handles();
    const auto flags = scene->area_render_scene->flags();
    ASSERT_EQ(objects.size(), kinds.size());
    ASSERT_EQ(objects.size(), flags.size());
    uint32_t selectable_record_count = 0;
    bool found_non_humanoid_creature = false;
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
            if (const auto* visual = nw::kernel::objects().components().find_visual(object);
                visual && visual->appearance >= 0) {
                const auto* appearance = nw::kernel::rules().appearances.get(
                    nw::Appearance::make(visual->appearance));
                found_non_humanoid_creature = found_non_humanoid_creature
                    || (appearance && appearance->model_type != nw::AppearanceModelType::parts);
            }
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
        case viewer::AreaRenderRecordKind::waypoint:
            EXPECT_EQ(object.type, nw::ObjectType::waypoint);
            EXPECT_TRUE(contains_object(live_area->waypoints));
            break;
        case viewer::AreaRenderRecordKind::tile:
        case viewer::AreaRenderRecordKind::unknown:
            FAIL() << "non-selectable render record reached object validation";
            break;
        }
    }
    EXPECT_TRUE(found_non_humanoid_creature);
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
    const auto tile_selection = viewer::select_area_object(
        surface_ray,
        *scene->area_render_scene,
        *scene,
        {.target = viewer::AreaObjectSelectionTarget::tile});
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

TEST(RenderViewerPreparedDraws, AreaTransientVisualsPreserveEditorSelectionAndRemoveAsBatch)
{
    namespace viewer = nw::render::viewer;

    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false);
    ASSERT_NE(module, nullptr) << "DockerDemo module fixture unavailable";

    const nw::Resref area_resref{"test_area"sv};
    const std::array area_resources{nw::Resource{area_resref, nw::ResourceType::are}};
    ASSERT_TRUE(resource_payloads_available(area_resources));

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

    auto* scene = session->scene();
    ASSERT_NE(scene, nullptr);
    const auto editor_selection = scene->active_object;
    const size_t baseline_model_count = scene->static_models.size();
    const size_t baseline_record_count = scene->area_render_scene->kinds().size();

    auto* creature = nw::kernel::objects().load<nw::Creature>("nw_chicken"sv);
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "appearance", *nw::Appearance::make(23)));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "wings", 0));
    ASSERT_TRUE(set_creature_appearance_propset_int(creature, "tail", 0));
    for (auto& equip : creature->equipment.equips) {
        equip = nw::Resref{};
    }
    const std::filesystem::path transient_creature_path{"tmp/animated_transient_creature.utc.json"};
    std::filesystem::create_directories(transient_creature_path.parent_path());
    ASSERT_TRUE(creature->save(transient_creature_path, "json"));
    nw::kernel::objects().destroy(creature->handle());
    creature = nw::kernel::objects().load_file<nw::Creature>(transient_creature_path.string());
    ASSERT_NE(creature, nullptr);
    const auto creature_handle = creature->handle();
    ASSERT_TRUE(nw::kernel::objects().components().set_area(
        creature_handle, scene->root_object.id));
    ASSERT_TRUE(nw::kernel::objects().components().set_position(
        creature_handle, glm::vec3{15.0f, 15.0f, 0.0f}));

    const std::array transient_objects{creature_handle};
    const auto append = session->append_area_transient_visuals(transient_objects);
    ASSERT_TRUE(append.ok()) << append.diagnostic;
    EXPECT_EQ(append.object_count, 1u);
    EXPECT_GT(append.model_count, 0u);
    EXPECT_EQ(scene->active_object, editor_selection);
    EXPECT_EQ(scene->static_models.size(), baseline_model_count + append.model_count);
    EXPECT_GT(scene->area_render_scene->kinds().size(), baseline_record_count);

    bool found_transient_record = false;
    const auto record_objects = scene->area_render_scene->object_handles();
    for (const auto object : record_objects) {
        found_transient_record |= object == creature_handle;
    }
    EXPECT_TRUE(found_transient_record);

    nw::ObjectSpatialState moved_spatial{
        .owner = creature_handle,
        .area = scene->root_object.id,
        .position = {15.0f, 15.0f, 0.0f},
        .orientation = {1.0f, 0.0f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
        .velocity = {1.0f, 0.0f, 0.0f},
    };
    const std::array spatial_rows{moved_spatial};
    const auto spatial_stats = session->update_area_object_spatial_states(spatial_rows);
    EXPECT_GT(spatial_stats.render_model_root_count, 0u);

    bool found_oriented_root = false;
    for (size_t model_index = baseline_model_count; model_index < scene->static_models.size(); ++model_index) {
        if (scene->static_area_model_info[model_index].object != creature_handle
            || scene->static_model_attachment_binding_indices[model_index]
                != viewer::kInvalidSceneModelAttachmentBindingIndex) {
            continue;
        }
        const auto* instance = scene->static_model_instance(model_index);
        ASSERT_NE(instance, nullptr);
        const glm::vec3 rendered_forward = glm::normalize(
            glm::vec3{instance->root_transform * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}});
        EXPECT_NEAR(rendered_forward.x, 1.0f, 1.0e-5f);
        EXPECT_NEAR(rendered_forward.y, 0.0f, 1.0e-5f);
        found_oriented_root = true;
    }
    EXPECT_TRUE(found_oriented_root);

    const std::array walking_inputs{viewer::AreaCreatureLocomotionAnimationInput{
        .owner = creature_handle,
        .locomotion = viewer::AreaCreatureLocomotion::walking_forward,
    }};
    const auto walking_stats = session->update_area_creature_locomotion_animations(walking_inputs);
    EXPECT_EQ(walking_stats.rejected_input_count, 0u);
    EXPECT_GT(walking_stats.matched_model_count, 0u);
    EXPECT_GT(walking_stats.changed_model_count, 0u);

    bool found_walk_clip = false;
    for (size_t model_index = baseline_model_count; model_index < scene->static_models.size(); ++model_index) {
        if (scene->static_area_model_info[model_index].object != creature_handle) continue;
        const auto& model = scene->static_models[model_index];
        const auto* instance = scene->static_model_instance(model_index);
        if (!model || !instance) continue;
        if (!instance->animation.enabled) continue;
        ASSERT_LT(instance->animation.clip, model->animations.size());
        found_walk_clip |= model->animations[instance->animation.clip].name == "walk";
    }
    EXPECT_TRUE(found_walk_clip);

    const auto repeated_walking_stats = session->update_area_creature_locomotion_animations(walking_inputs);
    EXPECT_EQ(repeated_walking_stats.changed_model_count, 0u);

    const std::array strafing_inputs{viewer::AreaCreatureLocomotionAnimationInput{
        .owner = creature_handle,
        .locomotion = viewer::AreaCreatureLocomotion::strafing_right,
    }};
    const auto strafing_stats = session->update_area_creature_locomotion_animations(strafing_inputs);
    EXPECT_GT(strafing_stats.changed_model_count, 0u);
    bool found_strafe_clip = false;
    for (size_t model_index = baseline_model_count; model_index < scene->static_models.size(); ++model_index) {
        if (scene->static_area_model_info[model_index].object != creature_handle) continue;
        const auto& model = scene->static_models[model_index];
        const auto* instance = scene->static_model_instance(model_index);
        if (!model || !instance || !instance->animation.enabled) continue;
        ASSERT_LT(instance->animation.clip, model->animations.size());
        const auto& name = model->animations[instance->animation.clip].name;
        found_strafe_clip |= name == "cwalkr" || name == "ccwalkr";
    }
    EXPECT_TRUE(found_strafe_clip);

    const std::array idle_inputs{viewer::AreaCreatureLocomotionAnimationInput{
        .owner = creature_handle,
        .locomotion = viewer::AreaCreatureLocomotion::idle,
    }};
    const auto idle_stats = session->update_area_creature_locomotion_animations(idle_inputs);
    EXPECT_GT(idle_stats.changed_model_count, 0u);
    bool found_idle_clip = false;
    for (size_t model_index = baseline_model_count; model_index < scene->static_models.size(); ++model_index) {
        if (scene->static_area_model_info[model_index].object != creature_handle) continue;
        const auto& model = scene->static_models[model_index];
        const auto* instance = scene->static_model_instance(model_index);
        if (!model || !instance || !instance->animation.enabled) continue;
        ASSERT_LT(instance->animation.clip, model->animations.size());
        const auto& name = model->animations[instance->animation.clip].name;
        found_idle_clip |= name == "cpause1" || name == "pause1";
    }
    EXPECT_TRUE(found_idle_clip);

    nw::kernel::objects().destroy(creature_handle);
    ASSERT_FALSE(nw::kernel::objects().valid(creature_handle));

    const auto removal = session->remove_area_transient_visuals(transient_objects);
    ASSERT_TRUE(removal.ok()) << removal.diagnostic;
    EXPECT_EQ(removal.object_count, 1u);
    EXPECT_EQ(removal.model_count, append.model_count);
    EXPECT_EQ(scene->active_object, editor_selection);
    EXPECT_EQ(scene->static_models.size(), baseline_model_count);
    EXPECT_EQ(scene->area_render_scene->kinds().size(), baseline_record_count);
}
