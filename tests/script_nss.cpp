#include <catch2/catch_test_macros.hpp>

#include <nw/log.hpp>
#include <nw/script/Nss.hpp>
#include <nw/script/NssAstPrinter.hpp>
#include <nw/script/NssLexer.hpp>

#include <filesystem>
#include <iostream>
#include <string_view>

using namespace std::literals;
namespace fs = std::filesystem;

using namespace nw;

TEST_CASE("NWScript Parser", "[script]")
{
    script::Nss nss(fs::path("test_data/user/development/test.nss"));
    REQUIRE_NOTHROW(nss.parse());
    REQUIRE(nss.errors() == 0);
    script::NssAstPrinter p;
    nss.script().accept(&p);
    LOG_F(INFO, "{}", p.ss.str());

    script::Nss n2(fs::path("test_data/user/development/nw_s0_raisdead.nss"));
    REQUIRE_NOTHROW(n2.parse());
    REQUIRE(n2.errors() == 0);
}

TEST_CASE("NWScript Parser - preprocessor", "[formats]")
{
    script::Nss nss(fs::path("test_data/user/development/script_preprocessor.nss"));
    REQUIRE_NOTHROW(nss.parse());
    REQUIRE(nss.errors() == 0);
    const script::Script& script = nss.script();
    REQUIRE(script.defines.size() == 6);
    REQUIRE(script.defines[0].first == "ENGINE_NUM_STRUCTURES");
    REQUIRE(script.defines[0].second == "5");

    REQUIRE(script.includes.size() == 1);
    REQUIRE(script.includes[0] == "constants");
}

TEST_CASE("NWScript Parser - nwscript", "[formats]")
{
    script::Nss nss(fs::path("test_data/user/scratch/nwscript.nss"));
    REQUIRE_NOTHROW(nss.parse());
    REQUIRE(nss.errors() == 0);
}

TEST_CASE("NWScript Parser - var decl", "[formats]")
{
    script::Nss nss1("int a = 3, b, c = 1;"sv);
    nss1.parse();
    const script::Script& s1 = nss1.script();
    auto decl1 = dynamic_cast<script::DeclStatement*>(s1.decls[0].get());
    REQUIRE(decl1);
    REQUIRE(decl1->decls.size() == 3);

    // Empty statement..
    script::Nss nss2("void test_function(string s, int b) { int a;; }"sv);
    nss2.parse();
    REQUIRE(nss2.errors() == 0);

    // Empty statement..
    script::Nss nss3("int a;;"sv);
    nss3.parse();
    REQUIRE(nss3.warnings() == 1);
}

TEST_CASE("NWScript Parser - function decl / definition", "[formats]")
{
    script::Nss nss("void test_function(string s, int b);"sv);
    REQUIRE_NOTHROW(nss.parse());
    REQUIRE(nss.errors() == 0);
    const script::Script& s = nss.script();
    REQUIRE(s.decls.size() > 0);
    auto fd = dynamic_cast<script::FunctionDecl*>(s.decls[0].get());
    REQUIRE(fd);
    REQUIRE(fd->type.type_specifier.id == "void");
    REQUIRE(fd->params.size() == 2);
    REQUIRE(fd->params[0]->type.type_specifier.id == "string");

    script::Nss nss2("void test_function(string s, int b) { s + IntToString(b); }"sv);
    REQUIRE_NOTHROW(nss2.parse());
    REQUIRE(nss2.errors() == 0);
    const script::Script& s2 = nss2.script();
    REQUIRE(s2.decls.size() > 0);
    auto fd2 = dynamic_cast<script::FunctionDefinition*>(s2.decls[0].get());
    REQUIRE(fd2);
    REQUIRE(fd2->decl);
    REQUIRE(fd2->block);
    REQUIRE(fd2->block->nodes.size() == 1);

    script::Nss nss3("void test_function(string s, int b) { s; IntToString(b) +; int x = 2+2;}"sv);
    nss3.parse();
    REQUIRE(nss3.errors() == 1);

    script::Nss nss4("void test_function(int b) { s + IntToString(b); }"sv);
    REQUIRE_NOTHROW(nss4.parse());
    REQUIRE(nss4.errors() == 0);
    const script::Script& s4 = nss4.script();
    REQUIRE(s4.decls.size() > 0);
    auto fd4 = dynamic_cast<script::FunctionDefinition*>(s4.decls[0].get());
    REQUIRE(fd4);
    REQUIRE(fd4->decl);
    REQUIRE(fd4->block);
    REQUIRE(fd4->block->nodes.size() == 1);

    script::Nss nss5("int test_function(int a, int b) { return a % b; }"sv);
    REQUIRE_NOTHROW(nss5.parse());
    REQUIRE(nss5.errors() == 0);
}

TEST_CASE("NWScript lexer", "[formats]")
{
    script::NssLexer lexer{"x /= y"};
    REQUIRE(lexer.next().id == "x");
    REQUIRE(lexer.next().id == "/=");
    REQUIRE(lexer.next().id == "y");

    script::NssLexer lexer2{"/*"};
    REQUIRE_THROWS(lexer2.next());

    script::NssLexer lexer3{"/* this is comment */"};
    REQUIRE(lexer2.next().type == script::NssTokenType::END); // comments are skipped currently

    script::NssLexer lexer4{"\"this is unterminated"};
    REQUIRE_THROWS(lexer4.next());

    script::NssLexer lexer5{"\"Hello World\""};
    REQUIRE(lexer5.next().id == "Hello World");

    script::NssLexer lexer6{"0x123456789abCdEf"};
    REQUIRE(lexer6.next().type == script::NssTokenType::INTEGER_CONST);

    script::NssLexer lexer7{"~value"};
    REQUIRE(lexer7.next().type == script::NssTokenType::TILDE);

    script::NssLexer lexer8{R"x(    \
    )x"};
    REQUIRE(lexer8.next().type == script::NssTokenType::END);
}

TEST_CASE("NWScript parser error/warning callback", "[formats]")
{
    script::Nss nss("int forgot_semicolon"sv);
    std::vector<std::string> errors;
    nss.parser().set_error_callback([&errors](std::string_view message, script::NssToken) {
        errors.emplace_back(message);
    });
    REQUIRE_THROWS(nss.parse());
    REQUIRE(nss.errors() == errors.size());

    script::Nss nss3("int a;;"sv);
    std::vector<std::string> warnings;
    nss3.parser().set_warning_callback([&warnings](std::string_view message, script::NssToken) {
        warnings.emplace_back(message);
    });
    nss3.parse();
    REQUIRE(nss3.warnings() == warnings.size());
}
