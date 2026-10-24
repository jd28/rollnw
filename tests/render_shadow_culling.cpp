#include <nw/render/shadow_culling.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace {

// Original viewer predicate retained as an independent reference, including
// perspective W rejection and the 0.05 NDC padding.
bool reference_visible(const nw::render::Bounds& bounds, const glm::mat4& matrix)
{
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (const float z : {bounds.min.z, bounds.max.z}) {
        for (const float y : {bounds.min.y, bounds.max.y}) {
            for (const float x : {bounds.min.x, bounds.max.x}) {
                const auto clip = matrix * glm::vec4{x, y, z, 1.0f};
                if (std::abs(clip.w) <= 1.0e-6f) continue;
                const auto ndc = glm::vec3{clip} / clip.w;
                minimum = glm::min(minimum, ndc);
                maximum = glm::max(maximum, ndc);
            }
        }
    }
    return maximum.x >= -1.05f && minimum.x <= 1.05f
        && maximum.y >= -1.05f && minimum.y <= 1.05f
        && maximum.z >= -0.05f && minimum.z <= 1.05f;
}

void check_masks(std::span<const nw::render::Bounds> bounds, std::span<const glm::mat4> views)
{
    std::vector<uint8_t> actual(bounds.size(), 255u);
    ASSERT_TRUE(nw::render::collect_shadow_visibility_masks(bounds, views, actual));
    for (size_t row = 0; row < bounds.size(); ++row) {
        uint8_t expected = 0;
        for (size_t view = 0; view < views.size(); ++view) {
            if (reference_visible(bounds[row], views[view])) expected |= static_cast<uint8_t>(1u << view);
        }
        ASSERT_EQ(actual[row], expected) << "row " << row;
    }
}

} // namespace

TEST(RenderShadowCulling, RejectsInvalidShapesBeforeWriting)
{
    const std::array bounds{nw::render::Bounds{}};
    const std::array<glm::mat4, 9> views{};
    std::array<uint8_t, 1> masks{42u};
    EXPECT_FALSE(nw::render::collect_shadow_visibility_masks(bounds, views, masks));
    EXPECT_EQ(masks[0], 42u);
    EXPECT_FALSE(nw::render::collect_shadow_visibility_masks({}, {}, masks));
    EXPECT_EQ(masks[0], 42u);
    EXPECT_TRUE(nw::render::collect_shadow_visibility_masks({}, {}, {}));
    EXPECT_TRUE(nw::render::collect_shadow_visibility_masks(bounds, {}, masks));
    EXPECT_EQ(masks[0], 0u);
}

TEST(RenderShadowCulling, MatchesCornerProjectionAtClipBoundaries)
{
    std::vector<nw::render::Bounds> bounds;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const float low = axis == 2 ? -0.05f : -1.05f;
        for (const float edge : {low, 1.05f}) {
            for (const float value : {edge, std::nextafter(edge, -INFINITY), std::nextafter(edge, INFINITY)}) {
                glm::vec3 point{0.0f, 0.0f, 0.5f};
                point[axis] = value;
                bounds.push_back({point, point});
                bounds.push_back({glm::min(point, glm::vec3{0.0f}), glm::max(point, glm::vec3{0.0f})});
            }
        }
    }
    const std::array views{glm::mat4{1.0f}};
    check_masks(bounds, views);
    // Count-one batches also exercise the enclosing-bound rejection.
    for (const auto& row : bounds)
        check_masks(std::span{&row, 1u}, views);
}

TEST(RenderShadowCulling, MatchesRotatedOrthographicAndPerspectiveViews)
{
    std::mt19937 random{0x51ad0u};
    std::uniform_real_distribution<float> position{-2000.0f, 2000.0f};
    std::uniform_real_distribution<float> extent{0.0f, 150.0f};
    std::vector<nw::render::Bounds> bounds(4097);
    for (uint32_t batch = 0; batch < 32; ++batch) {
        for (auto& row : bounds) {
            row.min = {position(random), position(random), position(random)};
            row.max = row.min + glm::vec3{extent(random), extent(random), extent(random)};
        }
        std::array<glm::mat4, 8> views;
        for (size_t index = 0; index < views.size(); ++index) {
            const auto view = glm::lookAt(glm::vec3{position(random), position(random), 2500.0f},
                glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, 1.0f});
            views[index] = (index < 6 ? glm::ortho(-500.0f, 500.0f, -500.0f, 500.0f, 0.1f, 4000.0f)
                                      : glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 4000.0f))
                * view;
        }
        check_masks(bounds, views);
    }
}

TEST(RenderShadowCulling, PreservesDegenerateNonfiniteAndOverflowPolicy)
{
    const float huge = std::numeric_limits<float>::max();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array bounds{
        nw::render::Bounds{{-0.5f, -0.5f, 0.1f}, {0.5f, 0.5f, 0.9f}},
        nw::render::Bounds{{huge, huge, huge}, {huge, huge, huge}},
        nw::render::Bounds{{-huge, -huge, -huge}, {-huge, -huge, -huge}},
        nw::render::Bounds{{nan, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        nw::render::Bounds{{-INFINITY, 0.0f, 0.0f}, {INFINITY, 1.0f, 1.0f}},
        nw::render::Bounds{{1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}},
    };
    std::array views{glm::mat4{1.0f}, glm::mat4{0.0f}, glm::mat4{1.0f}, glm::mat4{1.0f}, glm::mat4{1.0f}, glm::mat4{1.0f}, glm::mat4{1.0f}};
    views[2][0][0] = huge;
    views[2][1][0] = -huge;
    views[3][3][3] = -1.0f;
    views[4][0][3] = 1.0f; // Mixed-sign and zero W within a bound.
    views[5][0][0] = nan;
    views[6][3][3] = 1.0e-7f;
    check_masks(bounds, views);
    check_masks(std::span{bounds}.first(3), views); // Finite batch with overflowing enclosing projections.
    for (const auto& row : bounds)
        check_masks(std::span{&row, 1u}, views);
}

TEST(RenderShadowCulling, MatchesRoundedBoundariesAfterTranslationAndRotation)
{
    std::mt19937 random{0xc011u};
    std::uniform_real_distribution<float> coordinate{-1200.0f, 1200.0f};
    for (uint32_t batch = 0; batch < 128; ++batch) {
        const auto matrix = glm::ortho(-90.0f, 90.0f, -90.0f, 90.0f, 0.1f, 2000.0f)
            * glm::lookAt(glm::vec3{coordinate(random), coordinate(random), 1000.0f},
                glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, 1.0f});
        const auto inverse = glm::inverse(glm::dmat4{matrix});
        std::vector<nw::render::Bounds> bounds;
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            for (float edge : {axis == 2 ? -0.05f : -1.05f, 1.05f}) {
                glm::dvec4 clip{0.0, 0.0, 0.5, 1.0};
                clip[axis] = edge;
                const glm::vec3 point{inverse * clip};
                for (glm::length_t component = 0; component < 3; ++component) {
                    for (float direction : {-INFINITY, INFINITY}) {
                        auto shifted = point;
                        for (uint32_t step = 0; step < 16; ++step) {
                            bounds.push_back({shifted, shifted});
                            shifted[component] = std::nextafter(shifted[component], direction);
                        }
                    }
                }
            }
        }
        const std::array views{matrix};
        check_masks(bounds, views);
        for (const auto& row : bounds)
            check_masks(std::span{&row, 1u}, views);
    }
}
