#include <nw/render/nwn/render_asset_cache.hpp>

#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <cstdint>
#include <string>

class RenderAssetCache : public ::testing::Test {
protected:
    // Later render tests in this binary use the same ambient kernel services.
    void SetUp() override { nw::kernel::services().start(); }
};

TEST_F(RenderAssetCache, ClearDropsCachedMissingResourceEntries)
{
    nw::render::nwn::RenderAssetCache cache{nullptr};
    const nw::gfx::Handle<nw::gfx::Texture> fallback_texture{};

    EXPECT_TRUE(cache.stats().empty());

    const std::string missing_texture = "__rollnw_missing_render_asset_cache_texture__";
    const nw::Resref missing_resref{missing_texture};
    EXPECT_EQ(cache.get_or_load_texture(missing_resref, false, fallback_texture), fallback_texture);
    auto stats = cache.stats();
    EXPECT_EQ(stats.texture_count, 1u);
    EXPECT_EQ(stats.texture_upload_bytes, 0u);

    EXPECT_EQ(cache.get_or_load_source_image(missing_resref), nullptr);
    stats = cache.stats();
    EXPECT_EQ(stats.source_image_count, 1u);
    EXPECT_EQ(stats.source_image_pixel_bytes, 0u);

    const uint64_t initial_epoch = cache.epoch();
    cache.clear();
    const auto cleared_stats = cache.stats();
    EXPECT_TRUE(cleared_stats.empty());
    EXPECT_EQ(cleared_stats.epoch, initial_epoch + 1u);
    EXPECT_EQ(cleared_stats.invalidation_count, 1u);
    EXPECT_EQ(cleared_stats.observed_resource_generation, nw::kernel::resman().generation());
    EXPECT_EQ(cleared_stats.current_resource_generation, nw::kernel::resman().generation());
}

TEST_F(RenderAssetCache, ResourceRegistryGenerationInvalidatesEntriesLazily)
{
    nw::render::nwn::RenderAssetCache cache{nullptr};
    const std::string missing_texture = "__rollnw_missing_render_asset_cache_generation_texture__";
    const nw::Resref missing_resref{missing_texture};

    EXPECT_FALSE(cache.get_or_load_texture(missing_resref, false, {}).valid());
    EXPECT_EQ(cache.get_or_load_source_image(missing_resref), nullptr);
    const auto populated = cache.stats();
    ASSERT_EQ(populated.texture_count, 1u);
    ASSERT_EQ(populated.source_image_count, 1u);

    auto& resources = nw::kernel::resman();
    resources.unfreeze();
    resources.build_registry();
    ASSERT_GT(resources.generation(), populated.observed_resource_generation);

    EXPECT_FALSE(cache.get_or_load_texture(missing_resref, false, {}).valid());
    const auto refreshed = cache.stats();
    EXPECT_EQ(refreshed.epoch, populated.epoch + 1u);
    EXPECT_EQ(refreshed.invalidation_count, populated.invalidation_count + 1u);
    EXPECT_EQ(refreshed.texture_count, 1u);
    EXPECT_EQ(refreshed.source_image_count, 0u);
    EXPECT_EQ(refreshed.observed_resource_generation, resources.generation());
    EXPECT_EQ(refreshed.current_resource_generation, resources.generation());
}

TEST_F(RenderAssetCache, ResourceKeysCanonicalizeNamesAndSeparateTextureVariants)
{
    nw::render::nwn::RenderAssetCache cache{nullptr};

    EXPECT_FALSE(cache.get_or_load_texture("__ROLLNW_MISSING_CACHE_KEY__", false, {}).valid());
    EXPECT_EQ(cache.stats().texture_count, 1u);

    EXPECT_FALSE(cache.get_or_load_texture("__rollnw_missing_cache_key__", false, {}).valid());
    EXPECT_EQ(cache.stats().texture_count, 1u);

    EXPECT_FALSE(cache.get_or_load_texture("__rollnw_missing_cache_key__", true, {}).valid());
    EXPECT_EQ(cache.stats().texture_count, 2u);
}

TEST_F(RenderAssetCache, StatsCountDecodedSourceImagePixels)
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
