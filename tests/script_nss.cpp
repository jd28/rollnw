#include <catch2/catch_all.hpp>

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
}
