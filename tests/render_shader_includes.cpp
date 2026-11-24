#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TEST(ShaderIncludes, WaterShadersUseCommonRenderModelContracts)
{
    const std::filesystem::path shader_root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render/shaders";
    const auto read_shader = [&](std::string_view name) {
        std::ifstream input{shader_root / name};
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return std::move(buffer).str();
    };

    const auto vertex = read_shader("render_water.vs.hlsl");
    const auto pixel = read_shader("render_water.ps.hlsl");

    ASSERT_FALSE(vertex.empty());
    ASSERT_FALSE(pixel.empty());
    EXPECT_NE(vertex.find("scene_constants.inc.hlsl"), std::string::npos);
    EXPECT_NE(pixel.find("scene_constants.inc.hlsl"), std::string::npos);
    EXPECT_NE(pixel.find("scene_shadow.inc.hlsl"), std::string::npos);
    EXPECT_NE(pixel.find("model_draw_constants.inc.hlsl"), std::string::npos);
    EXPECT_NE(pixel.find("sc_albedo_index"), std::string::npos);
    EXPECT_NE(pixel.find("sc_albedo.a"), std::string::npos);
}
