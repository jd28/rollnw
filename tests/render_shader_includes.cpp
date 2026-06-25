#include <gtest/gtest.h>

#include <nw/render/shader_provider.hpp>

#include <map>
#include <string>

using namespace std::literals;

namespace {

nw::render::ShaderIncludeResolver make_resolver(std::map<std::string, std::string> files)
{
    return [files = std::move(files)](std::string_view name) -> std::optional<std::string> {
        auto it = files.find(std::string{name});
        if (it == files.end()) { return {}; }
        return it->second;
    };
}

} // namespace

TEST(ShaderIncludes, NoIncludesPassthrough)
{
    const std::string source = "float4 main() : SV_Target\n{\n    return 0;\n}\n";
    auto result = nw::render::expand_shader_includes(source, "test.ps.hlsl", make_resolver({}));
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, source);
}

TEST(ShaderIncludes, BasicExpansion)
{
    auto resolver = make_resolver({{"common.inc.hlsl", "float helper() { return 1.0; }\n"}});
    auto result = nw::render::expand_shader_includes(
        "#include \"common.inc.hlsl\"\nfloat4 main() { return helper(); }\n",
        "test.ps.hlsl", resolver);
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("float helper()"), std::string::npos);
    EXPECT_NE(result->find("#line 1 \"common.inc.hlsl\""), std::string::npos);
    EXPECT_NE(result->find("#line 2 \"test.ps.hlsl\""), std::string::npos);
    EXPECT_EQ(result->find("#include"), std::string::npos);
}

TEST(ShaderIncludes, LeadingWhitespaceAndIndentedDirective)
{
    auto resolver = make_resolver({{"a.inc.hlsl", "int a;\n"}});
    auto result = nw::render::expand_shader_includes(
        "  \t#include \"a.inc.hlsl\"\n", "test.vs.hlsl", resolver);
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("int a;"), std::string::npos);
}

TEST(ShaderIncludes, DuplicateIncludeSplicedOnce)
{
    auto resolver = make_resolver({{"a.inc.hlsl", "int unique_symbol;\n"}});
    auto result = nw::render::expand_shader_includes(
        "#include \"a.inc.hlsl\"\n#include \"a.inc.hlsl\"\nvoid main() {}\n",
        "test.vs.hlsl", resolver);
    ASSERT_TRUE(result);
    const auto first = result->find("unique_symbol");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(result->find("unique_symbol", first + 1), std::string::npos);
}

TEST(ShaderIncludes, NestedIncludes)
{
    auto resolver = make_resolver({
        {"outer.inc.hlsl", "#include \"inner.inc.hlsl\"\nint outer;\n"},
        {"inner.inc.hlsl", "int inner;\n"},
    });
    auto result = nw::render::expand_shader_includes(
        "#include \"outer.inc.hlsl\"\nvoid main() {}\n", "test.vs.hlsl", resolver);
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("int inner;"), std::string::npos);
    EXPECT_NE(result->find("int outer;"), std::string::npos);
    EXPECT_LT(result->find("int inner;"), result->find("int outer;"));
}

TEST(ShaderIncludes, DiamondIncludeSplicedOnce)
{
    auto resolver = make_resolver({
        {"a.inc.hlsl", "#include \"shared.inc.hlsl\"\nint a;\n"},
        {"b.inc.hlsl", "#include \"shared.inc.hlsl\"\nint b;\n"},
        {"shared.inc.hlsl", "cbuffer Shared : register(b1) { float x; };\n"},
    });
    auto result = nw::render::expand_shader_includes(
        "#include \"a.inc.hlsl\"\n#include \"b.inc.hlsl\"\n", "test.vs.hlsl", resolver);
    ASSERT_TRUE(result);
    const auto first = result->find("cbuffer Shared");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(result->find("cbuffer Shared", first + 1), std::string::npos);
}

TEST(ShaderIncludes, UnresolvedIncludeFails)
{
    auto result = nw::render::expand_shader_includes(
        "#include \"missing.inc.hlsl\"\n", "test.vs.hlsl", make_resolver({}));
    EXPECT_FALSE(result);
}

TEST(ShaderIncludes, SelfIncludeTerminates)
{
    auto resolver = make_resolver({{"loop.inc.hlsl", "#include \"loop.inc.hlsl\"\nint x;\n"}});
    auto result = nw::render::expand_shader_includes(
        "#include \"loop.inc.hlsl\"\n", "test.vs.hlsl", resolver);
    // The seen-set splices each file once, so self-inclusion terminates.
    ASSERT_TRUE(result);
    const auto first = result->find("int x;");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(result->find("int x;", first + 1), std::string::npos);
}

TEST(ShaderIncludes, NonIncludePreprocessorLinesUntouched)
{
    const std::string source = "#define FOO 1\n#if FOO\nint x;\n#endif\n";
    auto result = nw::render::expand_shader_includes(source, "test.ps.hlsl", make_resolver({}));
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, source);
}

TEST(ShaderIncludes, NoTrailingNewline)
{
    auto resolver = make_resolver({{"a.inc.hlsl", "int a;"}});
    auto result = nw::render::expand_shader_includes(
        "#include \"a.inc.hlsl\"\nvoid main() {}", "test.vs.hlsl", resolver);
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("int a;"), std::string::npos);
    EXPECT_NE(result->find("void main() {}"), std::string::npos);
}
