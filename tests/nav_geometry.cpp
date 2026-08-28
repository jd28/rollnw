#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/nav/NavGeometry.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/AreaTransforms.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/smalls/runtime.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

using namespace std::literals;

namespace {

nw::model::Mdl parse_walkmesh(std::string_view text)
{
    nw::ResourceData data;
    data.name = {"nav_sample"sv, nw::ResourceType::wok};
    data.bytes.append(text.data(), text.size());
    return nw::model::Mdl{std::move(data)};
}

nw::model::Mdl parse_walkmesh(std::string_view text, nw::ResourceType::type type)
{
    nw::ResourceData data;
    data.name = {"nav_sample"sv, type};
    data.bytes.append(text.data(), text.size());
    return nw::model::Mdl{std::move(data)};
}

constexpr auto walkmesh = R"mdl(#NWmax WALKMESH ASCII
beginwalkmeshgeom nav_sample
node aabb walk
  parent nav_sample
  position 1 2 3
  verts 4
    0 0 0
    1 0 0
    0 1 0
    1 1 0
  faces 2
    0 1 2  1  0 0 0  5
    1 3 2  1  0 0 0  7
endnode
endwalkmeshgeom nav_sample
)mdl"sv;

constexpr auto door_walkmesh = R"mdl(#NWmax WALKMESH ASCII
node aabb nav_sample_closed
  parent NULL
  verts 3
    0 0 0
    1 0 0
    0 1 0
  faces 1
    0 1 2  1  0 0 0  5
endnode
node aabb nav_sample_open1
  parent NULL
  verts 3
    10 0 0
    11 0 0
    10 1 0
  faces 1
    0 1 2  1  0 0 0  5
endnode
node aabb nav_sample_open2
  parent NULL
  verts 3
    20 0 0
    21 0 0
    20 1 0
  faces 1
    0 1 2  1  0 0 0  5
endnode
)mdl"sv;

} // namespace

TEST(NavGeometry, BuildsAreaTileTransformsAsBatch)
{
    constexpr std::array inputs{
        nw::AreaTileTransformInput{0, 0, 0, 0},
        nw::AreaTileTransformInput{2, 3, 1, 1},
        nw::AreaTileTransformInput{0, 0, 0, 4},
    };
    std::array<glm::mat4, inputs.size()> outputs{};

    const auto stats = nw::build_area_tile_world_transforms(5.0f, inputs, outputs);

    EXPECT_EQ(stats.input_count, 3u);
    EXPECT_EQ(stats.output_count, 2u);
    EXPECT_EQ(stats.rejected_count, 1u);
    EXPECT_EQ(glm::vec3(outputs[0][3]), glm::vec3(5.0f, 5.0f, 0.0f));
    EXPECT_EQ(glm::vec3(outputs[1][3]), glm::vec3(25.0f, 35.0f, 5.0f));
    EXPECT_EQ(outputs[2], glm::mat4(1.0f));
}

TEST(NavGeometry, BuildsAreaObjectTransformsAsBatch)
{
    const std::array inputs{
        nw::AreaObjectTransformInput{{2.0f, 3.0f, 4.0f}, {0.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 1.0f}},
        nw::AreaObjectTransformInput{{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
    };
    std::array<glm::mat4, 2> outputs{};

    const auto stats = nw::build_area_object_world_transforms(inputs, outputs);

    EXPECT_EQ(stats.input_count, 2);
    EXPECT_EQ(stats.output_count, 1);
    EXPECT_EQ(stats.rejected_count, 1);
    const glm::vec3 transformed = outputs[0] * glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_NEAR(transformed.x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(transformed.y, 5.0f, 1.0e-5f);
    EXPECT_NEAR(transformed.z, 4.0f, 1.0e-5f);
    EXPECT_EQ(outputs[1], glm::mat4{1.0f});
}

TEST(NavGeometry, SelectsClosedDoorWalkmeshState)
{
    const auto parsed = parse_walkmesh(door_walkmesh, nw::ResourceType::dwk);
    ASSERT_TRUE(parsed.valid());
    const nw::nav::NavGeometryModelInput input{
        .model = &parsed.model,
        .kind = nw::nav::NavGeometryKind::door_dwk,
        .node_selection = nw::nav::NavGeometryNodeSelection::door_closed,
        .owner = 17,
    };
    nw::nav::NavGeometry geometry;

    const auto stats = nw::nav::append_nav_geometry_models(
        std::span<const nw::nav::NavGeometryModelInput>{&input, 1}, geometry);

    EXPECT_EQ(stats.mesh_count, 1);
    EXPECT_EQ(stats.triangle_count, 1);
    ASSERT_EQ(geometry.indices.size(), 3);
    EXPECT_LT(geometry.vertices[geometry.indices[0]].x, 2.0f);
    EXPECT_EQ(geometry.owner[0], 17);
}

TEST(NavGeometry, SelectsOpenDoorWalkmeshStates)
{
    const auto parsed = parse_walkmesh(door_walkmesh, nw::ResourceType::dwk);
    ASSERT_TRUE(parsed.valid());
    const std::array inputs{
        nw::nav::NavGeometryModelInput{
            .model = &parsed.model,
            .kind = nw::nav::NavGeometryKind::door_dwk,
            .node_selection = nw::nav::NavGeometryNodeSelection::door_open1,
            .owner = 1,
        },
        nw::nav::NavGeometryModelInput{
            .model = &parsed.model,
            .kind = nw::nav::NavGeometryKind::door_dwk,
            .node_selection = nw::nav::NavGeometryNodeSelection::door_open2,
            .owner = 2,
        },
    };
    nw::nav::NavGeometry geometry;

    const auto stats = nw::nav::append_nav_geometry_models(inputs, geometry);

    EXPECT_EQ(stats.mesh_count, 2u);
    EXPECT_EQ(stats.triangle_count, 2u);
    ASSERT_EQ(geometry.owner.size(), 2u);
    EXPECT_EQ(geometry.owner[0], 1u);
    EXPECT_EQ(geometry.owner[1], 2u);
    EXPECT_LT(geometry.vertices[geometry.indices[0]].x, 12.0f);
    EXPECT_GT(geometry.vertices[geometry.indices[3]].x, 19.0f);
}

TEST(NavGeometry, BuildsPlacedObjectWalkmeshesFromVisualState)
{
    auto* area = nw::kernel::objects().make<nw::Area>();
    auto* placeable = nw::kernel::objects().make<nw::Placeable>();
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(placeable, nullptr);
    ASSERT_NE(door, nullptr);
    ASSERT_TRUE(placeable->instantiate());
    ASSERT_TRUE(door->instantiate());

    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.clear_visual(placeable->handle(), 0));
    ASSERT_TRUE(components.add_visual_model(placeable->handle(), {
                                                                     .model = nw::Resref{"dag_tnocliff2a"},
                                                                 }));
    ASSERT_TRUE(components.clear_visual(door->handle(), 0));
    ASSERT_TRUE(components.add_visual_model(door->handle(), {
                                                                .model = nw::Resref{"tn_sdoor_18"},
                                                            }));
    auto* placeable_spatial = components.get_or_create_spatial(placeable->handle());
    ASSERT_NE(placeable_spatial, nullptr);
    placeable_spatial->position = {20.0f, 30.0f, 0.0f};
    auto* door_spatial = components.get_or_create_spatial(door->handle());
    ASSERT_NE(door_spatial, nullptr);
    door_spatial->position = {40.0f, 50.0f, 0.0f};
    area->placeables.push_back(placeable);
    area->doors.push_back(door);

    nw::nav::NavGeometry geometry;
    const auto stats = nw::nav::build_area_object_nav_geometry(
        *area, nw::kernel::resman(), geometry);

    EXPECT_EQ(stats.placeable_count, 1);
    EXPECT_EQ(stats.door_count, 1);
    EXPECT_EQ(stats.missing_visual_count, 0);
    EXPECT_EQ(stats.missing_walkmesh_count, 0);
    EXPECT_EQ(stats.rejected_object_count, 0);
    EXPECT_EQ(stats.unique_resource_count, 2);
    EXPECT_GT(stats.append.triangle_count, 0);
    ASSERT_TRUE(geometry.valid());
    EXPECT_NE(std::find(geometry.kind.begin(), geometry.kind.end(),
                  nw::nav::NavGeometryKind::placeable_pwk),
        geometry.kind.end());
    EXPECT_NE(std::find(geometry.kind.begin(), geometry.kind.end(),
                  nw::nav::NavGeometryKind::door_dwk),
        geometry.kind.end());
    ASSERT_FALSE(geometry.owner.empty());
    EXPECT_EQ(*std::min_element(
                  geometry.owner.begin(), geometry.owner.end()),
        0u);
    EXPECT_EQ(*std::max_element(
                  geometry.owner.begin(), geometry.owner.end()),
        1u);

    nw::kernel::objects().destroy(placeable->handle());
    nw::kernel::objects().destroy(door->handle());
    nw::kernel::objects().destroy(area->handle());
}

TEST(NavGeometry, BuildsExclusiveDoorObstacleStateCatalog)
{
    auto* area = nw::kernel::objects().make<nw::Area>();
    auto* placeable = nw::kernel::objects().make<nw::Placeable>();
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(placeable, nullptr);
    ASSERT_NE(door, nullptr);
    ASSERT_TRUE(placeable->instantiate());
    ASSERT_TRUE(door->instantiate());
    nw::kernel::runtime().init_object_propsets(door->handle());

    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.clear_visual(placeable->handle(), 0));
    ASSERT_TRUE(components.add_visual_model(placeable->handle(), {
                                                                     .model = nw::Resref{"dag_tnocliff2a"},
                                                                 }));
    ASSERT_TRUE(components.clear_visual(door->handle(), 0));
    ASSERT_TRUE(components.add_visual_model(door->handle(), {
                                                                .model = nw::Resref{"tn_sdoor_18"},
                                                            }));
    auto* placeable_spatial
        = components.get_or_create_spatial(placeable->handle());
    ASSERT_NE(placeable_spatial, nullptr);
    placeable_spatial->position = {20.0f, 30.0f, 0.0f};
    auto* door_spatial = components.get_or_create_spatial(door->handle());
    ASSERT_NE(door_spatial, nullptr);
    door_spatial->position = {40.0f, 50.0f, 0.0f};
    door_spatial->orientation = {1.0f, 0.0f, 0.0f};
    area->placeables.push_back(placeable);
    area->doors.push_back(door);

    nw::nav::NavObjectObstacleSnapshot snapshot;
    const auto stats = nw::nav::build_area_object_nav_obstacles(
        *area, nw::kernel::resman(), snapshot);

    EXPECT_EQ(stats.rejected_object_count, 0u);
    ASSERT_TRUE(snapshot.geometry.valid());
    ASSERT_EQ(snapshot.active.size(), 4u);
    EXPECT_EQ(snapshot.active, (nw::Vector<uint8_t>{1u, 1u, 0u, 0u}));
    ASSERT_EQ(snapshot.doors.size(), 1u);
    const auto& row = snapshot.doors[0];
    EXPECT_EQ(row.door, door->handle());
    EXPECT_EQ(row.door_index, 0u);
    EXPECT_EQ(row.closed_obstacle_state, 1u);
    EXPECT_EQ(row.open1_obstacle_state, 2u);
    EXPECT_EQ(row.open2_obstacle_state, 3u);
    EXPECT_EQ(row.state, nw::nav::NavDoorState::closed);
    EXPECT_EQ(row.normal, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_GT(row.closed_half_depth, 0.0f);

    nw::kernel::objects().destroy(placeable->handle());
    nw::kernel::objects().destroy(door->handle());
    nw::kernel::objects().destroy(area->handle());
}

TEST(NavGeometry, AppendsWalkmeshMaterialsAndNodeTransform)
{
    auto mdl = parse_walkmesh(walkmesh);
    ASSERT_TRUE(mdl.valid());

    nw::nav::NavGeometry geometry;
    const nw::nav::NavGeometryModelInput input{
        .model = &mdl.model,
        .transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 30.0f}),
        .kind = nw::nav::NavGeometryKind::tile_wok,
        .owner = 42,
    };

    const auto stats = nw::nav::append_nav_geometry_models(
        std::span<const nw::nav::NavGeometryModelInput>{&input, 1}, geometry);

    EXPECT_EQ(stats.mesh_count, 1u);
    EXPECT_EQ(stats.triangle_count, 2u);
    ASSERT_TRUE(geometry.valid());
    EXPECT_EQ(geometry.surface, (nw::Vector<uint32_t>{5, 7}));
    EXPECT_EQ(geometry.owner, (nw::Vector<uint32_t>{42, 42}));
    ASSERT_FALSE(geometry.vertices.empty());
    EXPECT_EQ(geometry.vertices.front(), glm::vec3(11.0f, 22.0f, 33.0f));
}

TEST(NavGeometry, WeldsAdjacencyAcrossSplitVertexIndices)
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    geometry.indices = {0, 1, 2, 3, 4, 5};
    geometry.surface = {1, 1};
    geometry.kind = {
        nw::nav::NavGeometryKind::tile_aabb,
        nw::nav::NavGeometryKind::tile_aabb,
    };
    geometry.owner = {0, 0};

    const auto stats = nw::nav::build_nav_geometry_adjacency(geometry);

    EXPECT_EQ(stats.linked_edge_count, 2u);
    EXPECT_EQ(stats.boundary_edge_count, 4u);
    EXPECT_EQ(geometry.adjacency, (nw::Vector<int32_t>{-1, 1, -1, -1, -1, 0}));
}

TEST(NavGeometry, LeavesNonManifoldEdgesAsBoundaries)
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    geometry.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    geometry.surface = {1, 1, 1};
    geometry.kind.assign(3, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner.assign(3, 0);

    const auto stats = nw::nav::build_nav_geometry_adjacency(geometry);

    EXPECT_EQ(stats.non_manifold_edge_count, 3u);
    EXPECT_EQ(stats.linked_edge_count, 0u);
    EXPECT_EQ(geometry.adjacency, nw::Vector<int32_t>(9, -1));
}

TEST(NavGeometry, BuildsSurfaceWalkabilityAsDenseRows)
{
    constexpr auto source = R"2da(2DA V2.0

Label Walk
0 floor 1
1 wall 0
2 invalid 2
3 missing ****
)2da"sv;
    const nw::StaticTwoDA table{source};
    nw::Vector<uint8_t> walkable;

    const auto stats = nw::nav::build_nav_surface_walkability(table, walkable);

    EXPECT_EQ(stats.row_count, 4u);
    EXPECT_EQ(stats.walkable_count, 1u);
    EXPECT_EQ(stats.invalid_count, 2u);
    EXPECT_EQ(walkable, (nw::Vector<uint8_t>{1, 0, 0, 0}));
}

TEST(NavGeometry, ExtractsRealNwnTileWokFromDedicatedServer)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.push_back({"tall_a01_01"});

    nw::Area area;
    area.width = 1;
    area.height = 1;
    area.tileset = &tileset;
    nw::AreaTile tile;
    tile.id = 0;
    tile.height = 0;
    tile.orientation = 0;
    area.tiles.push_back(tile);

    nw::nav::NavGeometry geometry;
    const auto stats = nw::nav::build_area_tile_nav_geometry(
        area, nw::kernel::resman(), geometry);

    EXPECT_EQ(stats.tile_count, 1u);
    EXPECT_EQ(stats.wok_tile_count, 1u);
    EXPECT_EQ(stats.fallback_tile_count, 0u);
    EXPECT_EQ(stats.rejected_tile_count, 0u);
    EXPECT_GT(stats.append.triangle_count, 0u);
    EXPECT_TRUE(geometry.valid());
}

TEST(NavGeometry, ParsesRepeatedTileWalkmeshOnce)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.push_back({"tall_a01_01"});

    nw::Area area;
    area.width = 2;
    area.height = 1;
    area.tileset = &tileset;
    nw::AreaTile first;
    first.id = 0;
    nw::AreaTile second;
    second.id = 0;
    area.tiles = {first, second};

    nw::nav::NavGeometry geometry;
    const auto stats = nw::nav::build_area_tile_nav_geometry(
        area, nw::kernel::resman(), geometry);

    EXPECT_EQ(stats.tile_count, 2u);
    EXPECT_EQ(stats.wok_tile_count, 2u);
    EXPECT_EQ(stats.unique_resource_count, 1u);
    EXPECT_GT(std::count(geometry.owner.begin(), geometry.owner.end(), 0u), 0);
    EXPECT_GT(std::count(geometry.owner.begin(), geometry.owner.end(), 1u), 0);
}
