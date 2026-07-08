#include <nw/gfx/gfx.hpp>
#include <nw/render/model.hpp>

#include <gtest/gtest.h>

#include <vector>

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

TEST(RenderGfxPool, RejectsSlotCapacityOverflow)
{
    using Pool = nw::gfx::Pool<nw::gfx::Buffer, uint32_t>;

    Pool pool;
    std::vector<nw::gfx::Handle<nw::gfx::Buffer>> handles;
    handles.reserve(Pool::max_capacity());
    for (size_t i = 0; i < Pool::max_capacity(); ++i) {
        auto handle = pool.insert(static_cast<uint32_t>(i));
        ASSERT_TRUE(handle.valid()) << i;
        EXPECT_EQ(handle.index(), i);
        handles.push_back(handle);
    }

    EXPECT_FALSE(pool.insert(0xFFFFu).valid());
}

TEST(RenderGfxPool, ReusedSlotGenerationNeverReturnsInvalidZero)
{
    using Pool = nw::gfx::Pool<nw::gfx::Buffer, uint32_t>;

    Pool pool;
    auto handle = pool.insert(1u);
    ASSERT_TRUE(handle.valid());

    for (size_t i = 0; i < Pool::max_capacity(); ++i) {
        pool.destroy(handle);
        handle = pool.insert(static_cast<uint32_t>(i));
        ASSERT_TRUE(handle.valid()) << i;
        ASSERT_NE(pool.get(handle), nullptr);
    }
}

TEST(RenderGfxFrameSlots, PublicFrameCountMatchesCurrentBackendContract)
{
    EXPECT_EQ(nw::gfx::kFramesInFlight, 2u);
}
