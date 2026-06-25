#include <nw/gfx/gfx.hpp>

#include <gtest/gtest.h>

TEST(RenderPipelineCache, PipelineDescEqualityCoversRenderStateAndVertexLayout)
{
    nw::gfx::PipelineDesc base{};
    nw::gfx::PipelineDesc other = base;
    EXPECT_EQ(base, other);

    other.depth_compare = nw::gfx::CompareOp::LessEqual;
    EXPECT_NE(base, other);

    other = base;
    other.uses_bindless_sampled_textures = true;
    EXPECT_NE(base, other);

    other = base;
    other.storage_buffer_count = 3;
    EXPECT_NE(base, other);

    other = base;
    other.vertex_stride = 32;
    EXPECT_NE(base, other);

    other = base;
    other.vertex_attributes.push_back(nw::gfx::VertexAttribute{
        .location = 0,
        .offset = 12,
        .format = nw::gfx::VertexFormat::Float3,
    });
    EXPECT_NE(base, other);

    base.vertex_attributes = other.vertex_attributes;
    EXPECT_EQ(base, other);

    other.vertex_attributes[0].format = nw::gfx::VertexFormat::Float4;
    EXPECT_NE(base, other);
}
