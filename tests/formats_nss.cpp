#include <catch2/catch.hpp>

#include <nw/formats/Nss.hpp>
#include <nw/formats/NssAstPrinter.hpp>
#include <nw/formats/NssLexer.hpp>
#include <nw/log.hpp>

#include <filesystem>
#include <iostream>
#include <string_view>

using namespace std::literals;
namespace fs = std::filesystem;

using namespace nw;

TEST_CASE("NWScript Parser", "[formats]")
{
    script::Nss nss(fs::path("test_data/user/development/test.nss"));
    REQUIRE_NOTHROW(nss.parse());
    script::NssAstPrinter p;
    nss.script().accept(&p);
    LOG_F(INFO, "{}", p.ss.str());
}

TEST_CASE("NWScript Parser - preprocessor", "[formats]")
{
    script::Nss nss(fs::path("test_data/user/development/script_preprocessor.nss"));
    REQUIRE_NOTHROW(nss.parse());
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
}

TEST_CASE("NWScript Parser - function decl", "[formats]")
{
    // This is kind of messed up.. and should be fixed.  Right now a function decl is parsed as a definition
    // with an empty block.
    script::Nss nss("void test_function(string s, int b);"sv);
    REQUIRE_NOTHROW(nss.parse());
    const script::Script& s = nss.script();
    REQUIRE(s.decls.size() > 0);
    auto fd = dynamic_cast<script::FunctionDefinition*>(s.decls[0].get());
    REQUIRE(fd);
    REQUIRE(!fd->block);
    REQUIRE(fd->decl->type.type_specifier.id == "void");
    REQUIRE(fd->decl->params.size() == 2);
    REQUIRE(fd->decl->params[0]->type.type_specifier.id == "string");
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
