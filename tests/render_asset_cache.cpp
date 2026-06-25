#include <nw/render/nwn/render_asset_cache.hpp>

#include <gtest/gtest.h>

#include <string>

TEST(RenderAssetCache, ClearDropsCachedMissingResourceEntries)
{
    nw::render::nwn::RenderAssetCache cache{nullptr};
    const nw::gfx::Handle<nw::gfx::Texture> fallback_texture{};

    EXPECT_TRUE(cache.stats().empty());

    const std::string missing_texture = "__rollnw_missing_render_asset_cache_texture__";
    EXPECT_EQ(cache.get_or_load_texture(missing_texture, false, fallback_texture), fallback_texture);
    auto stats = cache.stats();
    EXPECT_EQ(stats.texture_count, 1u);
    EXPECT_EQ(stats.texture_upload_bytes, 0u);

    EXPECT_EQ(cache.get_or_load_source_image(missing_texture), nullptr);
    stats = cache.stats();
    EXPECT_EQ(stats.source_image_count, 1u);
    EXPECT_EQ(stats.source_image_pixel_bytes, 0u);

    cache.clear(fallback_texture);
    EXPECT_TRUE(cache.stats().empty());
}

TEST(RenderAssetCache, StatsCountDecodedSourceImagePixels)
{
    nw::render::nwn::RenderAssetCache cache{nullptr};

    const nw::Image* image = cache.get_or_load_source_image("bones");
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->valid());

    const size_t expected_bytes = static_cast<size_t>(image->width()) * image->height() * image->channels();
    const auto stats = cache.stats();
    EXPECT_EQ(stats.source_image_count, 1u);
    EXPECT_EQ(stats.source_image_pixel_bytes, expected_bytes);
    EXPECT_EQ(stats.texture_count, 0u);
    EXPECT_EQ(stats.texture_upload_bytes, 0u);
}
