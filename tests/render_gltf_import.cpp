#include <gtest/gtest.h>

#include <nw/gfx/gfx.hpp>
#include <nw/render/gltf/import_gltf.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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
        context_desc.width = 64;
        context_desc.height = 64;
        context = nw::gfx::create_context(core, context_desc);
        return context != nullptr;
    }
};

} // namespace

TEST(RenderGltfImport, ImportsModelAssetWithoutGraphicsContext)
{
    const auto dir = std::filesystem::temp_directory_path() / "rollnw_model_asset_gltf_import";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto bin_path = dir / "triangle.bin";
    std::vector<uint8_t> bin(36, 0);
    const auto write_float = [&](size_t offset, float value) {
        static_assert(sizeof(float) == 4);
        std::memcpy(bin.data() + offset, &value, sizeof(float));
    };

    const float positions[] = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    for (size_t i = 0; i < std::size(positions); ++i) {
        write_float(i * sizeof(float), positions[i]);
    }

    {
        std::ofstream out(bin_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    const auto gltf_path = dir / "triangle.gltf";
    {
        std::ofstream out(gltf_path);
        out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.25, 0.5, 0.75, 1.0],
      "metallicFactor": 0.2,
      "roughnessFactor": 0.75
    }
  }],
  "buffers": [{"uri": "triangle.bin", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}
  ],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0},
    "material": 0
  }]}]
})";
    }

    auto asset = nw::render::gltf::import_gltf_model_asset(gltf_path);
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->source_kind, nw::render::ModelAssetSourceKind::gltf);
    EXPECT_EQ(asset->name, "triangle.gltf");
    ASSERT_EQ(asset->materials.size(), 1u);
    EXPECT_FLOAT_EQ(asset->materials[0].albedo.x, 0.25f);
    EXPECT_FLOAT_EQ(asset->materials[0].albedo.y, 0.5f);
    EXPECT_FLOAT_EQ(asset->materials[0].albedo.z, 0.75f);
    EXPECT_FLOAT_EQ(asset->materials[0].metallic, 0.2f);
    EXPECT_FLOAT_EQ(asset->materials[0].roughness, 0.75f);
    EXPECT_FLOAT_EQ(asset->materials[0].specular_strength, 1.0f);

    ASSERT_EQ(asset->nodes.size(), 1u);
    ASSERT_EQ(asset->primitives.size(), 1u);
    const auto& primitive = asset->primitives[0];
    EXPECT_FALSE(primitive.skinned);
    EXPECT_TRUE(primitive.casts_shadow);
    EXPECT_EQ(primitive.material, 0u);
    EXPECT_EQ(primitive.node, 0);
    EXPECT_EQ(primitive.vertices.size(), 3u);
    EXPECT_EQ(primitive.indices, (std::vector<uint32_t>{0u, 1u, 2u}));
    EXPECT_TRUE(asset->shadow.valid);
    EXPECT_TRUE(asset->shadow.casts_shadow);
    EXPECT_EQ(asset->shadow.caster_count, 1u);

    const auto validation = nw::render::validate_model_asset(*asset);
    EXPECT_TRUE(validation.passed());
    EXPECT_EQ(validation.valid_primitive_count, 1u);

    nw::render::gltf::ImportGltfDesc desc{};
    const auto uploaded = nw::render::gltf::import_gltf_render_model_from_asset(gltf_path, desc);
    EXPECT_TRUE(uploaded.asset_imported);
    EXPECT_FALSE(uploaded.model);
    EXPECT_EQ(uploaded.geometry_upload_stats.primitive_count, 1u);
    EXPECT_EQ(uploaded.geometry_upload_stats.uploaded_primitive_count, 0u);
    EXPECT_EQ(uploaded.geometry_upload_stats.missing_context_count, 1u);
    EXPECT_EQ(uploaded.texture_upload_stats.material_count, 0u);

    std::filesystem::remove_all(dir);
}

TEST(RenderGltfImport, ImportsModelAssetMaterialTextureSourcesWithoutGraphicsContext)
{
    const auto dir = std::filesystem::temp_directory_path() / "rollnw_model_asset_texture_source_gltf_import";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto bin_path = dir / "triangle.bin";
    std::vector<uint8_t> bin(36, 0);
    const auto write_float = [&](size_t offset, float value) {
        static_assert(sizeof(float) == 4);
        std::memcpy(bin.data() + offset, &value, sizeof(float));
    };

    const float positions[] = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    for (size_t i = 0; i < std::size(positions); ++i) {
        write_float(i * sizeof(float), positions[i]);
    }

    {
        std::ofstream out(bin_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    const auto gltf_path = dir / "textured_triangle.gltf";
    {
        std::ofstream out(gltf_path);
        out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "images": [
    {"uri": "albedo.png"},
    {"uri": "normal.png"},
    {"uri": "metal_rough.png"},
    {"uri": "occlusion.png"},
    {"uri": "emissive.png"}
  ],
  "textures": [
    {"source": 0},
    {"source": 1},
    {"source": 2},
    {"source": 3},
    {"source": 4}
  ],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorTexture": {"index": 0},
      "metallicRoughnessTexture": {"index": 2}
    },
    "normalTexture": {"index": 1},
    "occlusionTexture": {"index": 3},
    "emissiveTexture": {"index": 4}
  }],
  "buffers": [{"uri": "triangle.bin", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}
  ],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0},
    "material": 0
  }]}]
})";
    }

    auto asset = nw::render::gltf::import_gltf_model_asset(gltf_path);
    ASSERT_TRUE(asset);
    ASSERT_EQ(asset->texture_sources.size(), 5u);
    for (const auto& source : asset->texture_sources) {
        EXPECT_EQ(source.kind, nw::render::ModelAssetTextureSourceKind::external_file);
        EXPECT_TRUE(source.encoded_bytes.empty());
    }
    EXPECT_EQ(std::filesystem::path(asset->texture_sources[0].path).filename(), "albedo.png");
    EXPECT_EQ(std::filesystem::path(asset->texture_sources[1].path).filename(), "normal.png");
    EXPECT_EQ(std::filesystem::path(asset->texture_sources[2].path).filename(), "metal_rough.png");
    EXPECT_EQ(std::filesystem::path(asset->texture_sources[3].path).filename(), "occlusion.png");
    EXPECT_EQ(std::filesystem::path(asset->texture_sources[4].path).filename(), "emissive.png");

    ASSERT_EQ(asset->material_texture_sources.size(), 1u);
    const auto& material_sources = asset->material_texture_sources[0];
    EXPECT_EQ(material_sources.albedo, 0u);
    EXPECT_EQ(material_sources.normal, 1u);
    EXPECT_EQ(material_sources.metallic_roughness, 2u);
    EXPECT_EQ(material_sources.occlusion, 3u);
    EXPECT_EQ(material_sources.emissive, 4u);

    const auto validation = nw::render::validate_model_asset(*asset);
    EXPECT_TRUE(validation.passed());
    EXPECT_EQ(validation.texture_source_count, 5u);
    EXPECT_EQ(validation.material_texture_binding_count, 1u);

    std::filesystem::remove_all(dir);
}

TEST(RenderGltfImport, ImportsSkinnedModelAssetWithoutGraphicsContext)
{
    const auto dir = std::filesystem::temp_directory_path() / "rollnw_skinned_model_asset_gltf_import";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto bin_path = dir / "skin.bin";
    std::vector<uint8_t> bin(96, 0);
    const auto write_float = [&](size_t offset, float value) {
        static_assert(sizeof(float) == 4);
        std::memcpy(bin.data() + offset, &value, sizeof(float));
    };

    const float positions[] = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    for (size_t i = 0; i < std::size(positions); ++i) {
        write_float(i * sizeof(float), positions[i]);
    }

    for (size_t vertex = 0; vertex < 3; ++vertex) {
        bin[36 + vertex * 4] = 0;
    }

    for (size_t vertex = 0; vertex < 3; ++vertex) {
        write_float(48 + vertex * 16, 1.0f);
    }

    {
        std::ofstream out(bin_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    const auto gltf_path = dir / "skin.gltf";
    {
        std::ofstream out(gltf_path);
        out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"mesh": 0, "skin": 0},
    {}
  ],
  "skins": [{"joints": [1]}],
  "buffers": [{"uri": "skin.bin", "byteLength": 96}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 12},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"}
  ],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}
  }]}]
})";
    }

    auto asset = nw::render::gltf::import_gltf_model_asset(gltf_path);
    ASSERT_TRUE(asset);
    ASSERT_EQ(asset->skins.size(), 1u);
    EXPECT_EQ(asset->skins[0].joints.size(), 1u);
    ASSERT_EQ(asset->skeletons.size(), 1u);
    EXPECT_EQ(asset->skeletons[0].joints.size(), 1u);

    ASSERT_EQ(asset->primitives.size(), 1u);
    const auto& primitive = asset->primitives[0];
    EXPECT_TRUE(primitive.skinned);
    EXPECT_TRUE(primitive.vertices.empty());
    EXPECT_EQ(primitive.skinned_vertices.size(), 3u);
    EXPECT_EQ(primitive.skin, 0u);
    EXPECT_EQ(primitive.indices, (std::vector<uint32_t>{0u, 1u, 2u}));
    ASSERT_FALSE(primitive.skinned_vertices.empty());
    EXPECT_EQ(primitive.skinned_vertices[0].joint_indices & 0xffu, 0u);
    EXPECT_EQ(primitive.skinned_vertices[0].joint_weights & 0xffu, 255u);

    const auto validation = nw::render::validate_model_asset(*asset);
    EXPECT_TRUE(validation.passed());
    EXPECT_EQ(validation.valid_primitive_count, 1u);

    std::filesystem::remove_all(dir);
}

TEST(RenderGltfImport, SparsePositionAccessorDropsPrimitive)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto dir = std::filesystem::temp_directory_path() / "rollnw_sparse_gltf_import";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto bin_path = dir / "sparse.bin";
    const std::vector<uint8_t> bin(64, 0);
    {
        std::ofstream out(bin_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    const auto gltf_path = dir / "sparse_position.gltf";
    {
        std::ofstream out(gltf_path);
        out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "buffers": [{"uri": "sparse.bin", "byteLength": 64}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 1},
    {"buffer": 0, "byteOffset": 40, "byteLength": 12}
  ],
  "accessors": [{
    "bufferView": 0,
    "componentType": 5126,
    "count": 3,
    "type": "VEC3",
    "sparse": {
      "count": 1,
      "indices": {"bufferView": 1, "componentType": 5121},
      "values": {"bufferView": 2}
    }
  }],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}]
})";
    }

    nw::render::gltf::ImportGltfDesc desc{};
    desc.ctx = gfx.context;
    auto model = nw::render::gltf::import_gltf(gltf_path, desc);
    EXPECT_FALSE(model);

    std::filesystem::remove_all(dir);
}

TEST(RenderGltfImport, SkinJointOutsideSkinMatrixRangeFallsBackToStaticPrimitive)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    const auto dir = std::filesystem::temp_directory_path() / "rollnw_skin_joint_range_gltf_import";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto bin_path = dir / "skin.bin";
    std::vector<uint8_t> bin(96, 0);
    const auto write_float = [&](size_t offset, float value) {
        static_assert(sizeof(float) == 4);
        std::memcpy(bin.data() + offset, &value, sizeof(float));
    };

    const float positions[] = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    for (size_t i = 0; i < std::size(positions); ++i) {
        write_float(i * sizeof(float), positions[i]);
    }

    for (size_t vertex = 0; vertex < 3; ++vertex) {
        bin[36 + vertex * 4] = 1;
    }

    for (size_t vertex = 0; vertex < 3; ++vertex) {
        write_float(48 + vertex * 16, 1.0f);
    }

    {
        std::ofstream out(bin_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    const auto gltf_path = dir / "skin_joint_range.gltf";
    {
        std::ofstream out(gltf_path);
        out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"mesh": 0, "skin": 0},
    {}
  ],
  "skins": [{"joints": [1]}],
  "buffers": [{"uri": "skin.bin", "byteLength": 96}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 12},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"}
  ],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}
  }]}]
})";
    }

    nw::render::gltf::ImportGltfDesc desc{};
    desc.ctx = gfx.context;
    auto model = nw::render::gltf::import_gltf(gltf_path, desc);
    ASSERT_TRUE(model);
    ASSERT_EQ(model->primitives.size(), 1u);
    EXPECT_FALSE(model->primitives[0].skinned);
    EXPECT_EQ(model->primitives[0].skin, std::numeric_limits<uint32_t>::max());

    std::filesystem::remove_all(dir);
}
