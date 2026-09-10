#include "blueprint_edits.hpp"
#include "blueprint_operations.hpp"
#include "project.hpp"
#include "smalls_creature_properties.hpp"
#include "workspace.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/component_propset_json.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace nw::toolset {
namespace {

using namespace std::literals;

class ClientBlueprints : public testing::Test {
protected:
    std::filesystem::path project{"tmp/client_blueprint_authoring"};
    WorkspaceState workspace;
    nlohmann::json sample_area;

    void SetUp() override
    {
        ASSERT_NE(kernel::load_module("test_data/user/modules/DockerDemo.mod", false), nullptr);
        kernel::runtime().add_module_path("stdlib/toolset");
        {
            auto* area = kernel::objects().make_area(Resref{"test_area"});
            ASSERT_NE(area, nullptr);
            ObjectDocument owner;
            ASSERT_TRUE(owner.adopt(area->handle()));
            serialize(area, sample_area);
        }
        std::filesystem::remove_all(project);
        for (const auto& definition : blueprint_types()) {
            std::filesystem::create_directories(project / "shared"
                / default_blueprint_directory(definition.resource_type));
        }
        std::ofstream{project / "shared/module.ifo.json"} << "{}";
        auto& resources = kernel::resman();
        resources.unfreeze();
        ASSERT_TRUE(resources.load_module(project / "shared"));
        resources.build_registry();
        workspace.ensure_default_tabs();
    }

    BlueprintCreationRequest item(std::string_view reference)
    {
        return {Resource{reference, ResourceType::uti}, 0};
    }

    BlueprintCreationRequest creature(std::string_view reference,
        int32_t race = 6, int32_t class_id = 4)
    {
        BlueprintCreationRequest result{Resource{reference, ResourceType::utc}};
        result.race = race;
        result.class_id = class_id;
        return result;
    }
};

TEST_F(ClientBlueprints, NewItemPublishesAndOpensWithoutAnActiveObject)
{
    const std::array creation{item("auth_new_item")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    ASSERT_EQ(initialized.roots.size(), 1u);
    const auto source = initialized.roots[0].object();
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::create,
        source, creation[0].destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_EQ(prepared.rows.size(), 1u);
    EXPECT_FALSE(std::filesystem::exists(prepared.rows[0].target));
    const auto results = publish_blueprint_writes(workspace, prepared);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].saved) << results[0].error;
    ASSERT_TRUE(results[0].published) << results[0].error;
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_EQ(workspace.active_tab()->kind, WorkspaceTabKind::preview);
    EXPECT_FALSE(workspace.active_tab()->dirty);
    EXPECT_NE(workspace.active_tab()->document.object(), source);
    auto* placed = kernel::objects().load<Item>(creation[0].destination.resref);
    ASSERT_NE(placed, nullptr);
    ObjectDocument owner;
    ASSERT_TRUE(owner.adopt(placed->handle()));
    EXPECT_EQ(placed->resref, creation[0].destination.resref);
    EXPECT_EQ(placed->tag.view(), "auth_new_item");
    EXPECT_TRUE(placed->uuid.is_nil());
    EXPECT_NE(kernel::objects().components().find_item_layout(placed->handle()), nullptr);
}

TEST_F(ClientBlueprints, CreationFailureReleasesTheDetachedBatch)
{
    const auto count = kernel::objects().object_count();
    const BlueprintCreationRequest invalid{Resource{"auth_invalid"sv, ResourceType::caf}};
    const std::array creation{item("auth_valid"), invalid};
    auto initialized = initialize_blueprints(creation);
    EXPECT_FALSE(initialized.ok());
    EXPECT_TRUE(initialized.roots.empty());
    EXPECT_EQ(kernel::objects().object_count(), count);
    EXPECT_FALSE(kernel::resman().contains(creation[0].destination));
}

TEST_F(ClientBlueprints, AllAuthoredTypesPublishReloadAndExposeTheirNames)
{
    std::vector<BlueprintCreationRequest> creation;
    creation.reserve(blueprint_types().size());
    for (const auto& definition : blueprint_types()) {
        const auto extension = ResourceType::to_string(definition.resource_type);
        BlueprintCreationRequest row{Resource{
            "auth_all_" + std::string{extension}, definition.resource_type}};
        row.name = "Named " + std::string{definition.label};
        if (definition.object_type == ObjectType::creature) {
            row.last_name = "Blueprint";
            row.race = 6;
            row.class_id = 4;
        } else if (definition.object_type == ObjectType::item) {
            row.base_item = 0;
        }
        creation.push_back(std::move(row));
    }

    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    ASSERT_EQ(initialized.roots.size(), blueprint_types().size());
    std::vector<BlueprintWriteRequest> requests;
    requests.reserve(creation.size());
    for (size_t index = 0; index < creation.size(); ++index) {
        EXPECT_EQ(initialized.roots[index].object().type,
            blueprint_object_type(creation[index].destination.type));
        requests.push_back({BlueprintWriteKind::create,
            initialized.roots[index].object(), creation[index].destination,
            std::filesystem::path{"shared"}
                / default_blueprint_directory(creation[index].destination.type)});
    }
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    const auto results = publish_blueprint_writes(workspace, prepared);
    ASSERT_EQ(results.size(), creation.size());
    for (size_t index = 0; index < results.size(); ++index) {
        ASSERT_TRUE(results[index].published) << results[index].error;
        auto expected_name = creation[index].name;
        if (!creation[index].last_name.empty()) {
            expected_name += " " + creation[index].last_name;
        }
        EXPECT_EQ(project_resource_display_name(project,
                      results[index].relative_path),
            expected_name);
    }

    std::vector<Resource> resources;
    resources.reserve(creation.size());
    for (const auto& row : creation) {
        resources.push_back(row.destination);
    }
    absl::flat_hash_map<Resource, nlohmann::json> snapshots;
    std::string error;
    ASSERT_TRUE(snapshot_blueprints(resources, snapshots, error)) << error;
    EXPECT_EQ(snapshots.size(), resources.size());
    const auto& door_state = snapshots.at(Resource{"auth_all_utd"sv, ResourceType::utd})
                                 .at("nwn1.propsets.DoorState");
    EXPECT_EQ(door_state.at("appearance"), 0);
    EXPECT_EQ(door_state.at("generic_type"), 0);
    for (const auto& [resource, snapshot] : snapshots) {
        SCOPED_TRACE(resource.filename());
        EXPECT_FALSE(snapshot.at("object").contains("uuid"));
    }

    ObjectDocument area_owner;
    auto* area = kernel::objects().make<Area>();
    ASSERT_TRUE(area_owner.adopt(area->handle()));
    ASSERT_TRUE(deserialize(area, sample_area));
    for (size_t index = 0; index < initialized.roots.size(); ++index) {
        const auto handle = initialized.roots[index].release();
        auto* object = kernel::objects().get_object_base(handle);
        ASSERT_NE(object, nullptr);
        object->comment = "Instance override";
        auto* spatial = kernel::objects().components().get_or_create_spatial(handle);
        spatial->area = area->handle().id;
        spatial->position = {5.0f, 5.0f, 0.0f};
        spatial->orientation = {0, 1, 0};
        spatial->scale = {1.1f, 1.2f, 1.3f};
        switch (handle.type) {
        case ObjectType::creature:
            area->creatures.push_back(kernel::objects().get<Creature>(handle));
            break;
        case ObjectType::door:
            area->doors.push_back(kernel::objects().get<Door>(handle));
            break;
        case ObjectType::encounter:
            area->encounters.push_back(kernel::objects().get<Encounter>(handle));
            break;
        case ObjectType::item:
            area->items.push_back(kernel::objects().get<Item>(handle));
            break;
        case ObjectType::placeable:
            area->placeables.push_back(kernel::objects().get<Placeable>(handle));
            break;
        case ObjectType::sound:
            area->sounds.push_back(kernel::objects().get<Sound>(handle));
            break;
        case ObjectType::store:
            area->stores.push_back(kernel::objects().get<Store>(handle));
            break;
        case ObjectType::trigger:
            area->triggers.push_back(kernel::objects().get<Trigger>(handle));
            break;
        case ObjectType::waypoint:
            area->waypoints.push_back(kernel::objects().get<Waypoint>(handle));
            break;
        default:
            FAIL() << "Unexpected authored object type";
        }
    }
    for (size_t index = 0; index < resources.size(); ++index) {
        LiveBlueprintUpdates updates;
        ASSERT_TRUE(collect_live_blueprint_references(
            area->handle(), resources[index], updates, error))
            << error;
        ASSERT_EQ(updates.rows.size(), 1u);
        const auto previous = updates.rows[0].object;
        const auto placement = *kernel::objects().components().find_spatial(previous);
        ASSERT_TRUE(prepare_live_blueprint_updates(updates, 1, error)) << error;
        auto selection = previous;
        ASSERT_TRUE(publish_live_blueprint_updates(updates, selection, error)) << error;
        EXPECT_FALSE(kernel::objects().valid(previous));
        ASSERT_TRUE(kernel::objects().valid(selection));
        EXPECT_EQ(selection.type, blueprint_types()[index].object_type);
        EXPECT_TRUE(kernel::objects().get_object_base(selection)->comment.empty());
        const auto* replacement = kernel::objects().components().find_spatial(selection);
        ASSERT_NE(replacement, nullptr);
        EXPECT_EQ(replacement->area, placement.area);
        EXPECT_EQ(replacement->position, placement.position);
        EXPECT_EQ(replacement->orientation, placement.orientation);
        EXPECT_EQ(replacement->scale, placement.scale);
    }

    for (const auto resource : resources) {
        LiveBlueprintUpdates located;
        ASSERT_TRUE(collect_live_blueprint_references(
            area->handle(), resource, located, error))
            << error;
        ASSERT_EQ(located.rows.size(), 1u);
        kernel::objects().get_object_base(located.rows[0].object)->comment
            = "Closed document override";
    }
    nlohmann::json original_area;
    serialize(area, original_area);
    for (const auto resource : resources) {
        // Creature navigation admission has its own walkmesh fixtures; this
        // batch verifies every newly enabled CAF member category here.
        if (resource.type == ResourceType::utc) { continue; }
        BlueprintUpdateDocument document{ResourceType::caf, original_area,
            collect_blueprint_references(
                original_area, ResourceType::caf, resource)};
        ASSERT_TRUE(document.references.error.empty())
            << document.references.error;
        ASSERT_EQ(document.references.rows.size(), 1u);
        const auto path = nlohmann::json::json_pointer{
            document.references.rows[0].path};
        const auto location = document.value.at(path).at("components").at("location");
        ASSERT_TRUE(prepare_blueprint_updates(
            std::span{&document, 1}, resource, snapshots, error))
            << error;
        EXPECT_TRUE(document.value.at(path).at("object").at("comment").get<std::string>().empty());
        EXPECT_EQ(document.value.at(path).at("components").at("location"),
            location);
    }
}

TEST_F(ClientBlueprints, InvalidCreatureSelectionsReleaseTheDetachedBatch)
{
    const auto count = kernel::objects().object_count();
    const std::array creation{creature("auth_invalid_race", 9999, 4)};
    const auto initialized = initialize_blueprints(creation);
    EXPECT_FALSE(initialized.ok());
    EXPECT_TRUE(initialized.roots.empty());
    EXPECT_EQ(kernel::objects().object_count(), count);
}

TEST_F(ClientBlueprints, WhitespaceNameRejectsTheDetachedBatch)
{
    const auto count = kernel::objects().object_count();
    auto request = item("auth_blank_name");
    request.name = " \t";
    const std::array creation{request};
    const auto initialized = initialize_blueprints(creation);
    EXPECT_FALSE(initialized.ok());
    EXPECT_TRUE(initialized.roots.empty());
    EXPECT_EQ(kernel::objects().object_count(), count);

    auto creature_request = creature("auth_blank_last_name");
    creature_request.name = "Named";
    creature_request.last_name = " \t";
    const std::array creature_creation{creature_request};
    const auto creature_initialized = initialize_blueprints(creature_creation);
    EXPECT_FALSE(creature_initialized.ok());
    EXPECT_TRUE(creature_initialized.roots.empty());
    EXPECT_EQ(kernel::objects().object_count(), count);
}

TEST_F(ClientBlueprints, SelectedRaceAndClassDriveSmallsCreatureDefaults)
{
    const std::array creation{creature("auth_dwarf_bard", 0, 1)};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    nlohmann::json value;
    ASSERT_TRUE(object_to_component_propset_json(
        kernel::objects().get_object_base(initialized.roots[0].object()),
        value, &kernel::runtime(), SerializationProfile::blueprint));
    EXPECT_EQ(value.at("nwn1.propsets.CreatureStats").at("race"), 0);
    EXPECT_EQ(value.at("nwn1.propsets.CreatureStats").at("abilities"),
        (nlohmann::json::array({12, 14, 14, 12, 10, 15})));
    EXPECT_EQ(value.at("nwn1.propsets.CreatureAppearance").at("appearance"), 0);
    EXPECT_EQ(value.at("nwn1.propsets.CreatureLevels").at("classes").at(0), 1);
    EXPECT_EQ(value.at("nwn1.propsets.CreatureLevels").at("class_levels").at(0), 1);
}

TEST_F(ClientBlueprints, NewItemsRetainChosenBaseTypesAndNativeVisualsThroughReload)
{
    // The fixture's four model families: composite, simple, armor, layered.
    const std::array creation{
        BlueprintCreationRequest{Resource{"auth_sword"sv, ResourceType::uti}, 0},
        BlueprintCreationRequest{Resource{"auth_shield"sv, ResourceType::uti}, 14},
        BlueprintCreationRequest{Resource{"auth_armor"sv, ResourceType::uti}, 16},
        BlueprintCreationRequest{Resource{"auth_helmet"sv, ResourceType::uti}, 17}};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    std::array<BlueprintWriteRequest, 4> requests;
    for (size_t index = 0; index < requests.size(); ++index) {
        requests[index] = {BlueprintWriteKind::create, initialized.roots[index].object(), creation[index].destination, "shared/blueprints/items"};
    }
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    const auto results = publish_blueprint_writes(workspace, prepared);
    for (size_t index = 0; index < results.size(); ++index) {
        ASSERT_TRUE(results[index].published) << results[index].error;
        auto* loaded = kernel::objects().load<Item>(creation[index].destination.resref);
        ASSERT_NE(loaded, nullptr);
        ObjectDocument owner;
        ASSERT_TRUE(owner.adopt(loaded->handle()));
        nlohmann::json saved;
        ASSERT_TRUE(object_to_component_propset_json(loaded, saved, &kernel::runtime(), SerializationProfile::blueprint));
        EXPECT_EQ(saved.at("nwn1.propsets.ItemStats").at("base_item"), creation[index].base_item);
        const auto* layout = kernel::objects().components().find_item_layout(loaded->handle());
        ASSERT_NE(layout, nullptr);
        EXPECT_GT(layout->inventory_width, 0);
        EXPECT_GT(layout->inventory_height, 0);
        const auto* visual = kernel::objects().components().find_visual(loaded->handle());
        ASSERT_NE(visual, nullptr);
        EXPECT_FALSE(visual->models.empty());
    }
}

TEST_F(ClientBlueprints, ReferencesReplaceValuesAndPreservePlacementThroughRestore)
{
    const std::array creation{item("auth_reference")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::create,
        initialized.roots[0].object(), creation[0].destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_TRUE(publish_blueprint_writes(workspace, prepared)[0].published);

    auto* area = kernel::objects().make<Area>();
    ASSERT_NE(area, nullptr);
    ObjectDocument area_owner;
    ASSERT_TRUE(area_owner.adopt(area->handle()));
    ASSERT_TRUE(deserialize(area, sample_area));
    auto* placed = kernel::objects().load<Item>(creation[0].destination.resref);
    ASSERT_NE(placed, nullptr);
    placed->comment = "Local comment must be replaced";
    placed->tag = kernel::strings().intern("instance_tag");
    auto* spatial = kernel::objects().components().get_or_create_spatial(placed->handle());
    spatial->position = {5, 5, 0.75f};
    spatial->orientation = {0, 1, 0};
    spatial->scale = {1.2f, 1.3f, 1.4f};
    spatial->area = area->handle().id;
    area->items.push_back(placed);
    std::filesystem::create_directories(project / "shared/areas");
    const auto path = project / "shared/areas/auth_area.caf.json";
    nlohmann::json original;
    serialize(area, original);
    original["items"].back()["object"]["uuid"] = "8b2a6483-aa40-49ef-8f98-f18e2568ecb7";
    const auto original_bytes = original.dump(2) + "\n";
    std::ofstream{path} << original_bytes;
    std::string error;
    const auto operation = create_blueprint_update_operation(project, creation[0].destination, "shared/areas/auth_area.caf.json", error);
    ASSERT_FALSE(operation.empty()) << error;
    ASSERT_TRUE(run_blueprint_update_operation(operation, "prepare", error)) << error;
    EXPECT_EQ(read_blueprint_operation_progress(operation).stage, "ready");
    EXPECT_EQ(read_blueprint_operation_progress(operation).instances, 1u);
    auto read = [](const auto& file) { std::ifstream stream{file}; return std::string{std::istreambuf_iterator<char>{stream}, {}}; };
    EXPECT_EQ(read(path), original_bytes);
    ASSERT_TRUE(run_blueprint_update_operation(operation, "commit", error)) << error;
    const auto updated = nlohmann::json::parse(read(path));
    const auto& replacement = updated["items"].back();
    EXPECT_EQ(replacement["object"]["comment"], "");
    EXPECT_EQ(replacement["object"]["tag"], "auth_reference");
    EXPECT_EQ(replacement["object"]["uuid"], original["items"].back()["object"]["uuid"]);
    EXPECT_EQ(replacement["components"]["location"], original["items"].back()["components"]["location"]);
    EXPECT_EQ(replacement["components"]["scale"], original["items"].back()["components"]["scale"]);
    EXPECT_EQ(updated["creatures"], original["creatures"]);
    ASSERT_TRUE(finish_blueprint_update_operation(operation, error)) << error;
    EXPECT_TRUE(find_unfinished_blueprint_operations(project).empty());
    EXPECT_EQ(latest_blueprint_operation(std::filesystem::canonical(project)), operation);
    ASSERT_TRUE(run_blueprint_update_operation(operation, "restore", error)) << error;
    EXPECT_EQ(read(path), original_bytes);
}

TEST_F(ClientBlueprints, LiveReplacementsPreserveOwnersAndReplaceNestedInventoryEquipmentAndStoreItems)
{
    const std::array creation{item("auth_live_item"), creature("auth_live_owner")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::create,
        initialized.roots[0].object(), creation[0].destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_TRUE(publish_blueprint_writes(workspace, prepared)[0].published);

    ObjectDocument area_owner;
    auto* area = kernel::objects().make<Area>();
    ASSERT_TRUE(area_owner.adopt(area->handle()));
    area->comment = "Unsaved area edit";
    auto* creature = kernel::objects().get<Creature>(initialized.roots[1].release());
    area->creatures.push_back(creature);
    creature->comment = "Unsaved owner edit";
    auto* store = kernel::objects().make<Store>();
    area->stores.push_back(store);
    store->comment = "Unsaved merchant edit";
    const auto area_handle = area->handle();
    const auto creature_handle = creature->handle();
    const auto store_handle = store->handle();
    std::vector<ObjectHandle> old;
    const auto load = [&]() {
        auto* result = kernel::objects().load<Item>(creation[0].destination.resref);
        EXPECT_NE(result, nullptr);
        if (result) {
            result->comment = "Instance override";
            old.push_back(result->handle());
        }
        return result;
    };
    auto* placed = load();
    ASSERT_NE(placed, nullptr);
    area->items.push_back(placed);
    placed->uuid = *uuids::uuid::from_string("8b2a6483-aa40-49ef-8f98-f18e2568ecb7");
    const auto uuid = placed->uuid;
    auto* spatial = kernel::objects().components().get_or_create_spatial(placed->handle());
    spatial->area = area_handle.id;
    spatial->position = {5, 6, 7};
    spatial->orientation = {0, 1, 0};
    spatial->scale = {1.2f, 1.3f, 1.4f};
    // A matching nested instance is covered by its matching parent's replacement.
    auto* nested = load();
    ASSERT_NE(nested, nullptr);
    ASSERT_TRUE(kernel::objects().components().get_or_create_inventory(*placed, 1, 10, 10)->add_item(nested));
    auto* inventory_item = load();
    ASSERT_NE(inventory_item, nullptr);
    ASSERT_TRUE(creature->inventory().add_item(inventory_item));
    creature->inventory().items[0].infinite = true;
    const auto inventory_position = creature->inventory().items[0];
    auto* equipped = load();
    ASSERT_NE(equipped, nullptr);
    ASSERT_TRUE(equip_item_in_slot(creature, equipped, EquipIndex::righthand));
    const auto equipment_version = creature->equipment.equip_version;
    auto* merchant_item = load();
    ASSERT_NE(merchant_item, nullptr);
    ASSERT_TRUE(store->inventory().weapons.add_item(merchant_item));
    store->inventory().weapons.items[0].infinite = true;
    const auto merchant_position = store->inventory().weapons.items[0];

    std::string error;
    LiveBlueprintUpdates updates;
    ASSERT_TRUE(collect_live_blueprint_references(area_handle, creation[0].destination, updates, error)) << error;
    EXPECT_EQ(updates.rows.size(), 4u);
    EXPECT_EQ(updates.covered, 1u);
    const auto before_count = kernel::objects().object_count();
    ASSERT_TRUE(prepare_live_blueprint_updates(updates, 1, error)) << error;
    EXPECT_EQ(updates.replacements.size(), 1u);
    EXPECT_EQ(area->items[0]->handle(), old[0]);
    updates = {}; // Cancelling releases preparation, never an attached instance.
    EXPECT_EQ(kernel::objects().object_count(), before_count);
    for (const auto handle : old) {
        EXPECT_TRUE(kernel::objects().valid(handle));
    }

    ASSERT_TRUE(collect_live_blueprint_references(area_handle, creation[0].destination, updates, error)) << error;
    ASSERT_TRUE(prepare_live_blueprint_updates(updates, updates.rows.size(), error)) << error;
    ASSERT_TRUE(validate_live_blueprint_updates(updates, error)) << error;
    auto selection = old[0];
    ASSERT_TRUE(publish_live_blueprint_updates(updates, selection, error)) << error;
    EXPECT_EQ(area_owner.object(), area_handle);
    EXPECT_EQ(area->creatures[0]->handle(), creature_handle);
    EXPECT_EQ(area->stores[0]->handle(), store_handle);
    EXPECT_EQ(area->comment, "Unsaved area edit");
    EXPECT_EQ(creature->comment, "Unsaved owner edit");
    EXPECT_EQ(store->comment, "Unsaved merchant edit");
    for (const auto handle : old) {
        EXPECT_FALSE(kernel::objects().valid(handle));
    }
    EXPECT_EQ(kernel::objects().object_count(), before_count - 1); // Covered child removed.
    EXPECT_EQ(selection, area->items[0]->handle());
    EXPECT_EQ(area->items[0]->uuid, uuid);
    EXPECT_TRUE(area->items[0]->comment.empty());
    const auto* replacement_spatial = kernel::objects().components().find_spatial(selection);
    ASSERT_NE(replacement_spatial, nullptr);
    EXPECT_EQ(replacement_spatial->area, area_handle.id);
    EXPECT_EQ(replacement_spatial->position, (glm::vec3{5, 6, 7}));
    EXPECT_EQ(replacement_spatial->orientation, (glm::vec3{0, 1, 0}));
    EXPECT_EQ(replacement_spatial->scale, (glm::vec3{1.2f, 1.3f, 1.4f}));
    EXPECT_TRUE(inventory_item_ptr(creature->inventory().items[0])->comment.empty());
    EXPECT_EQ(creature->inventory().items[0].pos_x, inventory_position.pos_x);
    EXPECT_EQ(creature->inventory().items[0].pos_y, inventory_position.pos_y);
    EXPECT_EQ(creature->inventory().items[0].infinite, inventory_position.infinite);
    EXPECT_TRUE(get_equipped_item(creature, EquipIndex::righthand)->comment.empty());
    EXPECT_GT(creature->equipment.equip_version, equipment_version);
    EXPECT_TRUE(inventory_item_ptr(store->inventory().weapons.items[0])->comment.empty());
    EXPECT_EQ(store->inventory().weapons.items[0].pos_x, merchant_position.pos_x);
    EXPECT_EQ(store->inventory().weapons.items[0].pos_y, merchant_position.pos_y);
    EXPECT_EQ(store->inventory().weapons.items[0].infinite, merchant_position.infinite);
}

TEST_F(ClientBlueprints, LiveCreatureAndPlaceableReplacementDestroysTheirOldContents)
{
    const std::array creation{creature("auth_live_npc"),
        BlueprintCreationRequest{Resource{"auth_live_prop"sv, ResourceType::utp}}};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    std::array<BlueprintWriteRequest, 2> requests;
    for (size_t index = 0; index < requests.size(); ++index) {
        kernel::objects().get_object_base(initialized.roots[index].object())->comment = "Saved parent value";
        requests[index] = {BlueprintWriteKind::create, initialized.roots[index].object(), creation[index].destination,
            std::filesystem::path{"shared"} / default_blueprint_directory(creation[index].destination.type)};
    }
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    const auto results = publish_blueprint_writes(workspace, prepared);
    for (const auto& result : results) {
        ASSERT_TRUE(result.published) << result.error;
    }
    ObjectDocument owner;
    auto* area = kernel::objects().make<Area>();
    ASSERT_TRUE(owner.adopt(area->handle()));
    auto* creature = kernel::objects().load<Creature>(creation[0].destination.resref);
    auto* placeable = kernel::objects().load<Placeable>(creation[1].destination.resref);
    ASSERT_NE(creature, nullptr);
    ASSERT_NE(placeable, nullptr);
    area->creatures.push_back(creature);
    area->placeables.push_back(placeable);
    const std::array old{creature->handle(), placeable->handle()};
    for (size_t index = 0; index < old.size(); ++index) {
        auto* object = kernel::objects().get_object_base(old[index]);
        object->comment = "Instance parent override";
        auto* spatial = kernel::objects().components().get_or_create_spatial(old[index]);
        spatial->area = area->handle().id;
        spatial->position = {5, 6, 7};
        auto* child = kernel::objects().make<Item>();
        const auto child_handle = child->handle();
        // An instance-only inventory child must disappear with its old parent.
        auto* inventory = kernel::objects().components().get_or_create_inventory(*object, 1, 10, 10);
        inventory->items.push_back(InventoryItem{false, 0, 0, child_handle});
        LiveBlueprintUpdates updates;
        std::string error;
        ASSERT_TRUE(collect_live_blueprint_references(area->handle(), creation[index].destination, updates, error)) << error;
        ASSERT_EQ(updates.rows.size(), 1u);
        ASSERT_TRUE(prepare_live_blueprint_updates(updates, 1, error)) << error;
        auto selection = child_handle;
        ASSERT_TRUE(publish_live_blueprint_updates(updates, selection, error)) << error;
        EXPECT_FALSE(kernel::objects().valid(old[index]));
        EXPECT_FALSE(kernel::objects().valid(child_handle));
        EXPECT_EQ(selection, ObjectHandle{});
        const auto replacement = index == 0 ? area->creatures[0]->handle() : area->placeables[0]->handle();
        EXPECT_EQ(kernel::objects().get_object_base(replacement)->comment, "Saved parent value");
        EXPECT_EQ(kernel::objects().get_by_tag(creation[index].destination.resref.view()), kernel::objects().get_object_base(replacement));
        const auto* after = kernel::objects().components().find_spatial(replacement);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(after->area, area->handle().id);
        EXPECT_EQ(after->position, (glm::vec3{5, 6, 7}));
    }
}

TEST_F(ClientBlueprints, LiveReplacementsRejectInvalidSlotsAndChangedOwnersBeforeDestruction)
{
    const std::array creation{item("auth_live_reject"), creature("auth_live_creature")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::create,
        initialized.roots[0].object(), creation[0].destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_TRUE(publish_blueprint_writes(workspace, prepared)[0].published);
    ObjectDocument owner;
    auto* area = kernel::objects().make<Area>();
    ASSERT_TRUE(owner.adopt(area->handle()));
    auto* creature = kernel::objects().get<Creature>(initialized.roots[1].release());
    area->creatures.push_back(creature);
    auto* equipped = kernel::objects().load<Item>(creation[0].destination.resref);
    ASSERT_NE(equipped, nullptr);
    const auto old = equipped->handle();
    // Malformed existing equipment must reject a fresh sword in a head slot.
    ASSERT_TRUE(equip_item_in_slot(creature, equipped, EquipIndex::head));
    LiveBlueprintUpdates updates;
    std::string error;
    ASSERT_TRUE(collect_live_blueprint_references(area->handle(), creation[0].destination, updates, error)) << error;
    ASSERT_TRUE(prepare_live_blueprint_updates(updates, updates.rows.size(), error)) << error;
    auto selection = old;
    EXPECT_FALSE(publish_live_blueprint_updates(updates, selection, error));
    EXPECT_NE(error.find("equipment slot"), std::string::npos) << error;
    EXPECT_TRUE(kernel::objects().valid(old));
    EXPECT_EQ(get_equipped_item(creature, EquipIndex::head), equipped);
    ASSERT_EQ(unequip_item_in_slot(creature, EquipIndex::head), equipped);
    ASSERT_TRUE(equip_item_in_slot(creature, equipped, EquipIndex::righthand));
    EXPECT_FALSE(publish_live_blueprint_updates(updates, selection, error));
    EXPECT_NE(error.find("ownership changed"), std::string::npos) << error;
    EXPECT_EQ(get_equipped_item(creature, EquipIndex::righthand), equipped);
    ASSERT_EQ(unequip_item_in_slot(creature, EquipIndex::righthand), equipped);
    ASSERT_TRUE(creature->inventory().add_item(equipped));
    ASSERT_TRUE(collect_live_blueprint_references(area->handle(), creation[0].destination, updates, error)) << error;
    ASSERT_TRUE(prepare_live_blueprint_updates(updates, updates.rows.size(), error)) << error;
    creature->inventory().items[0].pos_y = UINT16_MAX;
    EXPECT_FALSE(publish_live_blueprint_updates(updates, selection, error));
    EXPECT_NE(error.find("inventory positions"), std::string::npos) << error;
    EXPECT_TRUE(kernel::objects().valid(old));
    EXPECT_EQ(inventory_item_ptr(creature->inventory().items[0]), equipped);
    const auto object_count = kernel::objects().object_count();
    EXPECT_EQ(kernel::objects().load<Item>(Resref{"auth_missing_blueprint"}), nullptr);
    EXPECT_EQ(kernel::objects().object_count(), object_count);
}

TEST_F(ClientBlueprints, ReferenceTraversalCoversMatchingDescendantsAndRejectsUnknownSlots)
{
    nlohmann::json item{{"object", {{"resref", "auth_nested"}}}, {"components", {{"inventory", nlohmann::json::array()}}}};
    auto parent = item;
    parent["components"]["inventory"].push_back({{"position", {0, 0}}, {"item", item}});
    nlohmann::json area{{"$type", "CAF"}, {"$version", 1}};
    for (const auto* category : {"creatures", "doors", "encounters", "items", "placeables", "sounds", "stores", "triggers", "waypoints"}) {
        area[category] = nlohmann::json::array();
    }
    area["items"].push_back(parent);
    const Resource source{"auth_nested"sv, ResourceType::uti};
    auto references = collect_blueprint_references(area, ResourceType::caf, source);
    ASSERT_TRUE(references.error.empty()) << references.error;
    ASSERT_EQ(references.rows.size(), 2u);
    EXPECT_FALSE(references.rows[0].covered);
    EXPECT_TRUE(references.rows[1].covered);
    area["items"][0]["components"]["equipment"]["invalid_slot"] = item;
    EXPECT_FALSE(collect_blueprint_references(area, ResourceType::caf, source).error.empty());
}

TEST_F(ClientBlueprints, FreshCreatureAndPlaceableUseProfileDefaultsAndReload)
{
    std::array creation{creature("auth_creature"), BlueprintCreationRequest{Resource{"auth_placeable"sv, ResourceType::utp}}};
    creation[0].name = "Named";
    creation[0].last_name = "Creature";
    creation[1].name = "Named Placeable";
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    ASSERT_EQ(initialized.roots.size(), 2u);
    std::array<BlueprintWriteRequest, 2> requests;
    for (size_t i = 0; i < requests.size(); ++i) {
        requests[i] = {BlueprintWriteKind::create, initialized.roots[i].object(), creation[i].destination,
            std::filesystem::path{"shared"} / default_blueprint_directory(creation[i].destination.type)};
    }
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    const auto results = publish_blueprint_writes(workspace, prepared);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].published) << results[0].error;
    EXPECT_TRUE(results[1].published) << results[1].error;
    EXPECT_EQ(project_resource_display_name(project, results[0].relative_path),
        "Named Creature");
    EXPECT_EQ(project_resource_display_name(project, results[1].relative_path),
        "Named Placeable");
    auto* fresh = kernel::objects().load<Creature>(creation[0].destination.resref);
    ASSERT_NE(fresh, nullptr);
    ObjectDocument owner;
    ASSERT_TRUE(owner.adopt(fresh->handle()));
    EXPECT_TRUE(fresh->inventory().items.empty());
    const auto* visual = kernel::objects().components().find_visual(fresh->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_FALSE(visual->models.empty());
    nlohmann::json saved;
    ASSERT_TRUE(object_to_component_propset_json(fresh, saved, &kernel::runtime(), SerializationProfile::blueprint));
    const auto& appearance = saved.at("nwn1.propsets.CreatureAppearance");
    const auto& descriptor = saved.at("nwn1.propsets.CreatureDescriptor");
    const auto& stats = saved.at("nwn1.propsets.CreatureStats");
    const auto& levels = saved.at("nwn1.propsets.CreatureLevels");
    EXPECT_EQ(appearance.at("appearance"), 6);
    EXPECT_EQ(descriptor.at("name_first").at("strings").at(0).at("string"),
        "Named");
    EXPECT_EQ(descriptor.at("name_last").at("strings").at(0).at("string"),
        "Creature");
    EXPECT_EQ(stats.at("race"), 6);
    EXPECT_EQ(stats.at("abilities"), (nlohmann::json::array({16, 13, 16, 10, 10, 9})));
    ASSERT_EQ(stats.at("skills").size(), kernel::rules().skill_count());
    EXPECT_TRUE(std::ranges::all_of(stats.at("skills"), [](const auto& rank) { return rank == 0; }));
    EXPECT_EQ(levels.at("classes").at(0), 4);
    EXPECT_EQ(levels.at("class_levels").at(0), 1);
    for (const auto* field : {"body_part_bicep_left", "body_part_bicep_right",
             "body_part_foot_left", "body_part_foot_right", "body_part_forearm_left",
             "body_part_forearm_right", "body_part_hand_left", "body_part_hand_right",
             "body_part_head", "body_part_neck", "body_part_pelvis", "body_part_shin_left",
             "body_part_shin_right", "body_part_thigh_left", "body_part_thigh_right", "body_part_torso"}) {
        EXPECT_EQ(appearance.at(field), 1) << field;
    }
    for (const auto* field : {"body_part_belt", "body_part_shoulder_left",
             "body_part_shoulder_right", "body_part_robe"}) {
        EXPECT_EQ(appearance.at(field), 0) << field;
    }
    ObjectDetailsSnapshot sheet;
    build_creature_sheet(kernel::runtime(), fresh->handle(), sheet);
    EXPECT_EQ(sheet.status, ObjectDetailsStatus::ready) << sheet.diagnostic;
    const auto stats_type = kernel::runtime().type_id(
        "nwn1.propsets.CreatureStats", false);
    const auto* stats_definition = kernel::runtime().get_struct_def(stats_type);
    ASSERT_NE(stats_definition, nullptr);
    const auto stats_ref = kernel::runtime().find_propset_ref(
        stats_type, fresh->handle());
    const auto skills_index = stats_definition->field_index("skills");
    ASSERT_NE(skills_index, UINT32_MAX);
    auto* legacy_empty_skills = kernel::runtime().resolve_array(
        kernel::runtime().read_struct_value_field(
            stats_ref, stats_definition, skills_index));
    ASSERT_NE(legacy_empty_skills, nullptr);
    legacy_empty_skills->clear();
    build_creature_sheet(kernel::runtime(), fresh->handle(), sheet);
    EXPECT_EQ(sheet.status, ObjectDetailsStatus::ready) << sheet.diagnostic;
    EXPECT_EQ(live_object_display_name(fresh->handle()), "Named Creature");
    auto* fresh_placeable = kernel::objects().load<Placeable>(creation[1].destination.resref);
    ASSERT_NE(fresh_placeable, nullptr);
    ObjectDocument placeable_owner;
    ASSERT_TRUE(placeable_owner.adopt(fresh_placeable->handle()));
    EXPECT_EQ(live_object_display_name(fresh_placeable->handle()), "Named Placeable");
    const auto* placeable_visual = kernel::objects().components().find_visual(initialized.roots[1].object());
    ASSERT_NE(placeable_visual, nullptr);
    EXPECT_FALSE(placeable_visual->models.empty());
}

TEST_F(ClientBlueprints, UpdateReconcilesCleanDestinationAndProtectsDirtyTabs)
{
    const std::array creation{item("auth_update")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const std::array create{BlueprintWriteRequest{BlueprintWriteKind::create,
        initialized.roots[0].object(), creation[0].destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, create);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_TRUE(publish_blueprint_writes(workspace, prepared)[0].published);
    auto* placed = kernel::objects().load<Item>(creation[0].destination.resref);
    ASSERT_NE(placed, nullptr);
    ObjectDocument owner;
    ASSERT_TRUE(owner.adopt(placed->handle()));
    placed->comment = "Updated from instance";
    const auto old_root = workspace.active_tab()->document.object();
    workspace.active_tab()->dirty = true;
    const std::array update{BlueprintWriteRequest{BlueprintWriteKind::update, placed->handle(), creation[0].destination, {}}};
    auto rejected = prepare_blueprint_writes(project, workspace, update);
    EXPECT_FALSE(rejected.ok());
    EXPECT_TRUE(kernel::objects().valid(old_root));
    workspace.active_tab()->dirty = false;
    auto ready = prepare_blueprint_writes(project, workspace, update);
    ASSERT_TRUE(ready.ok()) << ready.error;
    const auto updated = publish_blueprint_writes(workspace, ready);
    ASSERT_TRUE(updated[0].published) << updated[0].error;
    EXPECT_FALSE(kernel::objects().valid(old_root));
    EXPECT_EQ(kernel::objects().get<Item>(workspace.active_tab()->document.object())->comment, placed->comment);
    EXPECT_TRUE(kernel::objects().valid(placed->handle()));
}

TEST_F(ClientBlueprints, SaveAsPreservesSourceAndRejectsFolderLocalCollisions)
{
    const std::array creation{item("auth_source")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const auto source = initialized.roots[0].object();
    auto* original = kernel::objects().get<Item>(source);
    original->comment = "Copied authored comment";
    original->uuid = *uuids::uuid::from_string("8b2a6483-aa40-49ef-8f98-f18e2568ecb7");
    auto& source_tab = workspace.open_or_replace_tab("preview:source", "Source", WorkspaceTabKind::preview,
        "shared/blueprints/items/auth_source.uti.json");
    source_tab.document = std::move(initialized.roots[0]);
    source_tab.dirty = true;
    const auto tab_count = workspace.tabs().size();
    const auto object_count = kernel::objects().object_count();
    nlohmann::json before;
    ASSERT_TRUE(object_to_component_propset_json(original, before, &kernel::runtime(), SerializationProfile::instance));
    const Resource destination{"auth_copy"sv, ResourceType::uti};
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::save_as,
        source, destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    const auto copy_handle = prepared.rows[0].document.object();
    auto* copy = kernel::objects().get<Item>(copy_handle);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy_handle, source);
    EXPECT_EQ(copy->resref, destination.resref);
    EXPECT_TRUE(copy->uuid.is_nil());
    const auto results = publish_blueprint_writes(workspace, prepared);
    ASSERT_TRUE(results[0].published) << results[0].error;
    EXPECT_FALSE(kernel::objects().valid(copy_handle));
    EXPECT_EQ(kernel::objects().object_count(), object_count);
    EXPECT_EQ(workspace.tabs().size(), tab_count);
    EXPECT_EQ(workspace.active_tab()->document.object(), source);
    EXPECT_TRUE(workspace.active_tab()->dirty);
    EXPECT_TRUE(kernel::resman().contains(destination));
    nlohmann::json after;
    ASSERT_TRUE(object_to_component_propset_json(original, after, &kernel::runtime(), SerializationProfile::instance));
    EXPECT_EQ(before, after);
    const auto saved = nlohmann::json::parse(kernel::resman().demand(destination).bytes.string_view());
    EXPECT_FALSE(saved.at("object").contains("uuid"));
    EXPECT_EQ(saved.at("object").at("resref"), destination.resref.string());
    EXPECT_EQ(saved.at("object").at("comment"), original->comment);
    {
        ObjectDocument instance;
        auto* placed = kernel::objects().load<Item>(destination.resref);
        ASSERT_NE(placed, nullptr);
        ASSERT_TRUE(instance.adopt(placed->handle()));
        EXPECT_TRUE(placed->uuid.is_nil());
        EXPECT_EQ(placed->resref, destination.resref);
        EXPECT_EQ(placed->comment, original->comment);
    }
    EXPECT_EQ(original->resref, creation[0].destination.resref);
    EXPECT_EQ(original->tag.view(), "auth_source");
    EXPECT_EQ(original->comment, "Copied authored comment");
    std::filesystem::create_directory(project / "shared/blueprints/items/another");
    auto collision = requests;
    collision[0].directory = "shared/blueprints/items/another";
    auto rejected = prepare_blueprint_writes(project, workspace, collision);
    EXPECT_FALSE(rejected.ok());
    EXPECT_TRUE(rejected.rows.empty());
}

TEST_F(ClientBlueprints, ChangedSourceInvalidatesPreparedCreation)
{
    const std::array creation{item("auth_changed")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::create,
        initialized.roots[0].object(), creation[0].destination, "shared/blueprints/items"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    kernel::objects().get<Item>(initialized.roots[0].object())->comment = "changed after review";
    const auto results = publish_blueprint_writes(workspace, prepared);
    EXPECT_FALSE(results[0].saved);
    EXPECT_FALSE(std::filesystem::exists(prepared.rows[0].target));
}

TEST_F(ClientBlueprints, SaveAsCopiesAuthoredCreatureStateWithoutActivatingTheCopy)
{
    const std::array creation{creature("auth_copy_cre")};
    auto initialized = initialize_blueprints(creation);
    ASSERT_TRUE(initialized.ok()) << initialized.error;
    const auto source = initialized.roots[0].object();
    auto& runtime = kernel::runtime();
    const auto tid = runtime.type_id("nwn1.propsets.CreatureHealth", false);
    const auto* definition = runtime.get_struct_def(tid);
    ASSERT_NE(definition, nullptr);
    const auto health = runtime.find_propset_ref(tid, source);
    ASSERT_TRUE(runtime.write_struct_value_field(health, definition, definition->field_index("hp_current"), smalls::Value::make_int(17)));
    nlohmann::json before;
    ASSERT_TRUE(object_to_component_propset_json(kernel::objects().get_object_base(source), before, &runtime, SerializationProfile::blueprint));
    const Resource destination{"auth_cre_copy"sv, ResourceType::utc};
    const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::save_as, source, destination, "shared/blueprints/creatures"}};
    auto prepared = prepare_blueprint_writes(project, workspace, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_TRUE(publish_blueprint_writes(workspace, prepared)[0].published);
    auto saved = nlohmann::json::parse(kernel::resman().demand(destination).bytes.string_view());
    EXPECT_EQ(saved.at("nwn1.propsets.CreatureHealth").at("hp_current"), 17);
    EXPECT_FALSE(saved.at("object").contains("uuid"));
    before.at("object").erase("uuid");
    before.at("object").erase("resref");
    saved.at("object").erase("resref");
    EXPECT_EQ(saved, before);
}

TEST(ClientBlueprintNames, ValidatesBeforeInterningAndUsesTheTypedNamespace)
{
    std::string normalized;
    std::string error;
    EXPECT_TRUE(validate_blueprint_resref("Town_Guard", normalized, error));
    EXPECT_EQ(normalized, "town_guard");
    for (const std::string_view name : {"", "../guard", "town/guard", "guard.utc", "guard ", "CON", "Lpt1", "a:b"}) {
        EXPECT_FALSE(validate_blueprint_resref(name, normalized, error)) << name;
        EXPECT_FALSE(error.empty());
    }
    EXPECT_FALSE(validate_blueprint_resref(std::string(247, 'a'), normalized, error));
    EXPECT_NE((Resource{"guard"sv, ResourceType::utc}), (Resource{"guard"sv, ResourceType::uti}));
}

} // namespace
} // namespace nw::toolset
