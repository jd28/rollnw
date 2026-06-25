#include <nw/gfx/gfx.hpp>
#include <nw/render/model.hpp>

#include <gtest/gtest.h>

TEST(RenderGfxBindless, SlotZeroIsInvalidSentinel)
{
    EXPECT_EQ(nw::gfx::kInvalidBindlessTextureIndex, 0u);
    EXPECT_FALSE(nw::gfx::bindless_texture_index_valid(nw::gfx::kInvalidBindlessTextureIndex));
    EXPECT_TRUE(nw::gfx::bindless_texture_index_valid(1u));
}

TEST(RenderGfxBindless, DefaultModelMaterialUsesInvalidTextureIndices)
{
    nw::render::Material material{};
    EXPECT_EQ(material.albedo_index, nw::gfx::kInvalidBindlessTextureIndex);
    EXPECT_EQ(material.normal_index, nw::gfx::kInvalidBindlessTextureIndex);
    EXPECT_EQ(material.surface_index, nw::gfx::kInvalidBindlessTextureIndex);
    EXPECT_EQ(material.emissive_index, nw::gfx::kInvalidBindlessTextureIndex);
}
