#include <gtest/gtest.h>

#include <nw/model/Mdl.hpp>

#include <string_view>

using namespace std::literals;

namespace {

nw::model::Mdl parse_walkmesh(nw::ResourceType::type type, std::string_view text)
{
    nw::ResourceData data;
    data.name = {"sample"sv, type};
    data.bytes.append(text.data(), text.size());
    return nw::model::Mdl{std::move(data)};
}

constexpr auto mesh_body = R"mdl(
  verts 4
    0 0 0
    1 0 0
    0 1 0
    1 1 0
  faces 3
    0 1 2  1  0 0 0  5
    0 1 7  1  0 0 0  9
    1 3 2  1  0 0 0  7
)mdl"sv;

} // namespace

TEST(ModelWalkmesh, ParsesWokGeometryAndKeepsAcceptedFaceMaterials)
{
    const std::string text = std::string{R"mdl(#NWmax WALKMESH ASCII
beginwalkmeshgeom sample
node aabb walk
  parent sample
)mdl"}
        + std::string{mesh_body}
        + R"mdl(endnode
endwalkmeshgeom exporter_label_does_not_match
)mdl";

    auto mdl = parse_walkmesh(nw::ResourceType::wok, text);

    ASSERT_TRUE(mdl.valid());
    ASSERT_EQ(mdl.model.nodes.size(), 1u);
    const auto* mesh = dynamic_cast<const nw::model::AABBNode*>(mdl.model.nodes[0].get());
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->indices.size(), 6u);
    ASSERT_EQ(mdl.model.face_material_ranges.size(), 1u);
    EXPECT_EQ(mdl.model.face_material_ranges[0].node_index, 0u);
    EXPECT_EQ(mdl.model.face_material_ranges[0].material_offset, 0u);
    EXPECT_EQ(mdl.model.face_material_ranges[0].face_count, 2u);
    EXPECT_EQ(mdl.model.face_materials, (nw::Vector<uint32_t>{5, 7}));
}

TEST(ModelWalkmesh, ParsesBarePwkWithExporterRootAliases)
{
    const std::string text = std::string{R"mdl(#NWmax PWKMESH ASCII
node trimesh NoWalk
  parent first_external_root
)mdl"}
        + std::string{mesh_body}
        + R"mdl(endnode
node dummy use01
  parent differently_spelled_external_root
endnode
)mdl";

    auto mdl = parse_walkmesh(nw::ResourceType::pwk, text);

    ASSERT_TRUE(mdl.valid());
    ASSERT_EQ(mdl.model.nodes.size(), 2u);
    EXPECT_EQ(mdl.model.nodes[0]->parent, nullptr);
    EXPECT_EQ(mdl.model.nodes[1]->parent, nullptr);
    EXPECT_EQ(mdl.model.face_materials, (nw::Vector<uint32_t>{5, 7}));
}

TEST(ModelWalkmesh, ParsesBareDwkOpenAndClosedMeshes)
{
    const std::string text = std::string{R"mdl(#NWmax DWKMESH ASCII
node trimesh door_wg_closed
  parent door_dwk
)mdl"}
        + std::string{mesh_body}
        + R"mdl(endnode
node trimesh door_wg_open1
  parent door_dwk
)mdl"
        + std::string{mesh_body}
        + R"mdl(endnode
)mdl";

    auto mdl = parse_walkmesh(nw::ResourceType::dwk, text);

    ASSERT_TRUE(mdl.valid());
    ASSERT_EQ(mdl.model.nodes.size(), 2u);
    ASSERT_EQ(mdl.model.face_material_ranges.size(), 2u);
    EXPECT_EQ(mdl.model.face_material_ranges[0].node_index, 0u);
    EXPECT_EQ(mdl.model.face_material_ranges[1].node_index, 1u);
    EXPECT_EQ(mdl.model.face_materials, (nw::Vector<uint32_t>{5, 7, 5, 7}));
}

TEST(ModelWalkmesh, DoesNotTreatBareNodesAsOrdinaryMdlGeometry)
{
    constexpr auto text = R"mdl(node trimesh walk
  parent external_root
endnode
)mdl"sv;

    auto mdl = parse_walkmesh(nw::ResourceType::mdl, text);

    EXPECT_FALSE(mdl.valid());
}

TEST(ModelWalkmesh, PreservesOnlyAabbMaterialsInOrdinaryMdl)
{
    const std::string text = std::string{R"mdl(#MAXMODEL ASCII
newmodel sample
#MAXGEOM ASCII
beginmodelgeom sample
node dummy sample
  parent NULL
endnode
node trimesh visible_mesh
  parent sample
)mdl"}
        + std::string{mesh_body}
        + R"mdl(endnode
node aabb walkmesh
  parent sample
)mdl"
        + std::string{mesh_body}
        + R"mdl(endnode
endmodelgeom sample
donemodel sample
)mdl";

    auto mdl = parse_walkmesh(nw::ResourceType::mdl, text);

    ASSERT_TRUE(mdl.valid());
    ASSERT_EQ(mdl.model.nodes.size(), 3u);
    ASSERT_EQ(mdl.model.face_material_ranges.size(), 1u);
    EXPECT_EQ(mdl.model.face_material_ranges[0].node_index, 2u);
    EXPECT_EQ(mdl.model.face_materials, (nw::Vector<uint32_t>{5, 7}));
}
