#include <gtest/gtest.h>

#include <nw/log.hpp>
#include <nw/script/AstPrinter.hpp>
#include <nw/script/Nss.hpp>
#include <nw/script/NssLexer.hpp>

#include <filesystem>
#include <string_view>

using namespace std::literals;
namespace fs = std::filesystem;

using namespace nw;

TEST(Nss, Parse)
{
    script::Context ctx{};
    script::Nss n2(fs::path("test_data/user/development/nw_s0_raisdead.nss"), &ctx);
    EXPECT_NO_THROW(n2.parse());
    EXPECT_TRUE(n2.errors() == 0);
}

TEST(Nss, Preprocessor)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss(fs::path("test_data/user/development/script_preprocessor.nss"), ctx.get());
    EXPECT_NO_THROW(nss.parse());
    EXPECT_TRUE(nss.errors() == 0);
    const script::Ast& script = nss.ast();
    EXPECT_TRUE(script.defines.size() == 6);
    EXPECT_TRUE(script.defines[0].first == "ENGINE_NUM_STRUCTURES");
    EXPECT_TRUE(script.defines[0].second == "5");

    EXPECT_TRUE(script.include_resrefs.size() == 1);
    EXPECT_TRUE(script.include_resrefs[0] == "constants");
}

TEST(Nss, ParseNwscript)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss(fs::path("test_data/user/scratch/nwscript.nss"), ctx.get());
    EXPECT_NO_THROW(nss.parse());
    EXPECT_NO_THROW(nss.resolve());
    EXPECT_TRUE(nss.errors() == 0);
    auto decl = nss.locate_export("IntToString", false);
    EXPECT_NE(decl, nullptr);
    auto d = dynamic_cast<nw::script::FunctionDecl*>(decl);
    if (d) {
        EXPECT_EQ(d->identifier.loc.view(), "IntToString");
        EXPECT_EQ(d->type_id_, nss.ctx()->type_id("string"));
    }

    // Completion
    auto completions = nss.complete("IntToString");
    std::sort(std::begin(completions), std::end(completions));
    EXPECT_EQ(completions.size(), 2);
    EXPECT_EQ(completions[0], "IntToHexString");
    EXPECT_EQ(completions[1], "IntToString");

    // Command Script Resolution..
    script::Nss nss2("string test_function(string s, int b) { return IntToString(b); }"sv, ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);

    // Env
    auto decl2 = nss.locate_export("TRUE", false);
    EXPECT_NE(decl2, nullptr);
    auto d2 = dynamic_cast<nw::script::VarDecl*>(decl2);
    if (d2) {
        EXPECT_EQ(d2->env.size(), 1);
    }

    auto decl3 = nss.locate_export("GetFirstObjectInShape", false);
    EXPECT_NE(decl3, nullptr);
    auto d3 = dynamic_cast<nw::script::FunctionDecl*>(decl3);
    EXPECT_NE(d3, nullptr);
    auto completions2 = d3->complete("OBJECT_type_");
    std::sort(std::begin(completions2), std::end(completions2));
    std::vector<std::string> expect2{
        "OBJECT_TYPE_ALL",
        "OBJECT_TYPE_AREA_OF_EFFECT",
        "OBJECT_TYPE_CREATURE",
        "OBJECT_TYPE_DOOR",
        "OBJECT_TYPE_ENCOUNTER",
        "OBJECT_TYPE_INVALID",
        "OBJECT_TYPE_ITEM",
        "OBJECT_TYPE_PLACEABLE",
        "OBJECT_TYPE_STORE",
        "OBJECT_TYPE_TRIGGER",
        "OBJECT_TYPE_WAYPOINT",
    };
    EXPECT_EQ(completions2, expect2);
}

TEST(Nss, Variables)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss1("int a = 3, b, c = 1;"sv, ctx.get());
    nss1.parse();
    const script::Ast& s1 = nss1.ast();
    auto decl1 = dynamic_cast<script::DeclStatement*>(s1.decls[0].get());
    EXPECT_TRUE(decl1);
    EXPECT_EQ(decl1->decls.size(), 3);

    // Empty statement..
    script::Nss nss2("void test_function(string s, int b) { int a;; }"sv, ctx.get());
    nss2.parse();
    EXPECT_EQ(nss2.errors(), 0);

    // Empty statement..
    script::Nss nss3("int a;;"sv, ctx.get());
    nss3.parse();
    EXPECT_EQ(nss3.warnings(), 1);

    // == Bad =================================================================

    // const without initializer
    script::Nss nss4("const int VARIABLE;"sv, ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_NO_THROW(nss4.resolve());
    EXPECT_EQ(nss4.errors(), 1);

    // using declared var in initializer
    script::Nss nss5("int VARIABLE = VARIABLE;"sv, ctx.get());
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_NO_THROW(nss5.resolve());
    EXPECT_EQ(nss5.errors(), 1);

    // init with wrong type
    script::Nss nss6("int VARIABLE = OBJECT_INVALID;"sv, ctx.get());
    EXPECT_NO_THROW(nss6.parse());
    EXPECT_NO_THROW(nss6.resolve());
    EXPECT_EQ(nss6.errors(), 1);

    // var with void type
    script::Nss nss7("void main() { void v; }"sv, ctx.get());
    EXPECT_NO_THROW(nss7.parse());
    EXPECT_NO_THROW(nss7.resolve());
    EXPECT_EQ(nss7.errors(), 1);

    // var with void type
    script::Nss nss8("void variable;"sv, ctx.get());
    EXPECT_NO_THROW(nss8.parse());
    EXPECT_NO_THROW(nss8.resolve());
    EXPECT_EQ(nss8.errors(), 1);
}

TEST(Nss, FunctionDecl)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss("void test_function(string s, int b);"sv, ctx.get());
    EXPECT_NO_THROW(nss.parse());
    EXPECT_EQ(nss.errors(), 0);
    const script::Ast& s = nss.ast();
    EXPECT_TRUE(s.decls.size() > 0);
    auto fd = dynamic_cast<script::FunctionDecl*>(s.decls[0].get());
    EXPECT_TRUE(fd);
    if (fd) {
        EXPECT_EQ(fd->type.type_specifier.loc.view(), "void");
        EXPECT_EQ(fd->params.size(), 2);
        EXPECT_EQ(fd->params[0]->type.type_specifier.loc.view(), "string");
    }

    script::Nss nss2("void test_function(string s, int b) { s + IntToString(b); }"sv, ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 0);
    const script::Ast& s2 = nss2.ast();
    EXPECT_TRUE(s2.decls.size() > 0);
    auto fd2 = dynamic_cast<script::FunctionDefinition*>(s2.decls[0].get());
    EXPECT_TRUE(fd2);
    if (fd2) {
        EXPECT_TRUE(fd2->decl_inline);
        EXPECT_TRUE(fd2->block);
        EXPECT_EQ(fd2->block->nodes.size(), 1);
    }

    script::Nss nss3("void test_function(string s, int b) { s; IntToString(b) +; int x = 2+2;}"sv, ctx.get());
    nss3.parse();
    EXPECT_EQ(nss3.errors(), 1);

    script::Nss nss4("void test_function(int b) { s + IntToString(b); }"sv, ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_EQ(nss4.errors(), 0);
    const script::Ast& s4 = nss4.ast();
    EXPECT_GT(s4.decls.size(), 0);
    auto fd4 = dynamic_cast<script::FunctionDefinition*>(s4.decls[0].get());
    if (fd4) {
        EXPECT_TRUE(fd4);
        EXPECT_TRUE(fd4->decl_inline);
        EXPECT_TRUE(fd4->block);
        EXPECT_EQ(fd4->block->nodes.size(), 1);
    }

    script::Nss nss5("int test_function(int a, int b) { return a % (b + x); }"sv, ctx.get());
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_EQ(nss5.errors(), 0);

    script::Nss nss6("int test_function(int a = -1, float x = 1.234) { return a; }"sv, ctx.get());
    EXPECT_NO_THROW(nss6.parse());
    EXPECT_EQ(nss6.errors(), 0);

    // == Bad =================================================================

    script::Nss nss7(R"(
        int test_function(int a);
        int test_function(int a, int b) { return a % b; }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss7.parse());
    EXPECT_NO_THROW(nss7.resolve());
    EXPECT_EQ(nss7.errors(), 1);

    script::Nss nss8(R"(
        int test_function(int a);
        int test_function(int a);
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss8.parse());
    EXPECT_NO_THROW(nss8.resolve());
    EXPECT_EQ(nss8.errors(), 1);

    script::Nss nss9(R"(
        int test_function(int a);
        int test_function(const int a = 1) { return a; }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss9.parse());
    EXPECT_NO_THROW(nss9.resolve());
    EXPECT_EQ(nss9.errors(), 1);

    script::Nss nss10(R"(
        int test_function(int a);
        int test_function(int b) { return b; }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss10.parse());
    EXPECT_NO_THROW(nss10.resolve());
    EXPECT_EQ(nss10.warnings(), 1);

    script::Nss nss11(R"(
        int test_function(float b);
        int test_function(int b) { return b; }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss11.parse());
    EXPECT_NO_THROW(nss11.resolve());
    EXPECT_EQ(nss11.errors(), 1);

    script::Nss nss12(R"(
        int test_function(int b = 1);
        int test_function(int b = -1) { return b; }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss12.parse());
    EXPECT_NO_THROW(nss12.resolve());
    EXPECT_EQ(nss12.errors(), 1);

    script::Nss nss13(R"(
        int test_function(int a);
        void test_function(int a) { a++; }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss13.parse());
    EXPECT_NO_THROW(nss13.resolve());
    EXPECT_EQ(nss13.errors(), 1);
}

TEST(Nss, FunctionDef)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss("void test_function(string s, int b) {}"sv, ctx.get());
    EXPECT_NO_THROW(nss.parse());
    EXPECT_EQ(nss.errors(), 0);

    script::Nss nss2(R"(
        void Something(object oID = OBJECT_INVALID);

        void Something(object oID) { /* ... */ }

        void main() {
            Something();
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);
}

TEST(Nss, FunctionCall)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss1("void main() { test_function(b); }"sv, ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_EQ(nss1.errors(), 0);

    script::Nss nss4(R"(
        int test_function(int b);
        void main() {
            int a = 1;
            int c = test_function(a);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_NO_THROW(nss4.resolve());
    EXPECT_EQ(nss4.errors(), 0);

    script::Nss nss5(R"(
        int test_function(float b);
        void main() {
            int a = 1;
            int c = test_function(a);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_NO_THROW(nss5.resolve());
    EXPECT_EQ(nss5.errors(), 0);

    script::Nss nss10(R"(
        int test_function(int b = 1);
        void main() {
            int c = test_function();
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss10.parse());
    EXPECT_NO_THROW(nss10.resolve());
    EXPECT_EQ(nss10.errors(), 0);

    // == Bad =================================================================

    script::Nss nss2("void main() { 423(b); }"sv, ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 1);

    script::Nss nss3("void main() { str.test(b); }"sv, ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.errors(), 1);

    script::Nss nss6(R"(
        int test_function(int b);
        void main() {
            string a = "";
            int c = test_function(a);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss6.parse());
    EXPECT_NO_THROW(nss6.resolve());
    EXPECT_EQ(nss6.errors(), 1);

    script::Nss nss7(R"(
        int test_function(int b);
        void main() {
            int a = 1;
            string c = test_function(a);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss7.parse());
    EXPECT_NO_THROW(nss7.resolve());
    EXPECT_EQ(nss7.errors(), 1);

    script::Nss nss8(R"(
        int test_function(int b);
        void main() {
            int c = test_function();
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss8.parse());
    EXPECT_NO_THROW(nss8.resolve());
    EXPECT_EQ(nss8.errors(), 1);

    script::Nss nss9(R"(
        int test_function(int b);
        void main() {
            int c = test_function(1, 3);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss9.parse());
    EXPECT_NO_THROW(nss9.resolve());
    EXPECT_EQ(nss9.errors(), 1);
}

TEST(Nss, DotOperator)
{
    auto ctx = std::make_unique<script::Context>();

    // == Good Parse ==========================================================
    script::Nss nss1("void main() { str.test; }"sv, ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_EQ(nss1.errors(), 0);

    script::Nss nss2("void main() { str.test.nested; }"sv, ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 0);

    script::Nss nss3("void main() { str().test.nested; }"sv, ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.errors(), 0);

    // == Bad Parse ============================================================

    // Only variable decl or chained dot operator
    script::Nss nss4("void main() { str.test.call_expr(); }"sv, ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_EQ(nss4.errors(), 1);
}

TEST(Nss, Struct)
{
    auto ctx = std::make_unique<script::Context>();

    // == Good =================================================================

    script::Nss nss1(R"(
        struct str { int test; };

        void main() {
            struct str s;
            s.test;
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 0);

    script::Nss nss2(R"(
        struct a { int test1; };
        struct b { struct a test2; };

        void main() {
            struct b s;
            s.test2.test1 = 1;
        }
    )"sv,
        ctx.get());

    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);

    script::Nss nss4(R"(
        struct a { int test1; };

        void main() {
            struct a s;
            s.test1++;
        }
    )"sv,
        ctx.get());

    EXPECT_NO_THROW(nss4.parse());
    EXPECT_NO_THROW(nss4.resolve());
    EXPECT_EQ(nss4.errors(), 0);

    // Struct is separate namespace
    script::Nss nss5(R"(
        struct MyStruct { int test1; };
        int MyStruct() { return 42; }

        void main() { }
    )"sv,
        ctx.get());

    EXPECT_NO_THROW(nss5.parse());
    EXPECT_NO_THROW(nss5.resolve());
    EXPECT_EQ(nss5.errors(), 0);

    // == Bad =================================================================

    script::Nss nss3(R"(
        struct a { int test1; };

        void main() {
            struct a s;
            s.test2;
        }
    )"sv,
        ctx.get());

    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 1);
}

TEST(Nss, Lexer)
{
    auto ctx = std::make_unique<script::Context>();

    script::NssLexer lexer{"x /= y", ctx.get()};
    auto t1 = lexer.next();
    EXPECT_EQ(t1.loc.view(), "x");
    EXPECT_EQ(lexer.next().loc.view(), "/=");
    auto t2 = lexer.next();
    EXPECT_EQ(t2.loc.view(), "y");
    EXPECT_EQ(script::merge_source_location(t1.loc, t2.loc).view(), "x /= y");

    script::NssLexer lexer2{"/*", ctx.get()};
    EXPECT_THROW(lexer2.next(), std::runtime_error);

    script::NssLexer lexer14{"/*/", ctx.get()};
    EXPECT_THROW(lexer14.next(), std::runtime_error);

    script::NssLexer lexer15{"/**/", ctx.get()};
    EXPECT_NO_THROW(lexer15.next());

    script::NssLexer lexer3{"/* this is comment */", ctx.get()};
    EXPECT_EQ(lexer3.next().type, script::NssTokenType::COMMENT);

    script::NssLexer lexer4{"\"this is unterminated", ctx.get()};
    EXPECT_THROW(lexer4.next(), std::runtime_error);

    script::NssLexer lexer5{"\"Hello World\"", ctx.get()};
    EXPECT_EQ(lexer5.next().loc.view(), "Hello World");

    script::NssLexer lexer6{"0x123456789abCdEf", ctx.get()};
    EXPECT_EQ(lexer6.next().type, script::NssTokenType::INTEGER_CONST);

    script::NssLexer lexer7{"~value", ctx.get()};
    EXPECT_EQ(lexer7.next().type, script::NssTokenType::TILDE);

    script::NssLexer lexer8{R"x(    \
    )x",
        ctx.get()};
    EXPECT_EQ(lexer8.next().type, script::NssTokenType::END);

    script::NssLexer lexer9{"\"", ctx.get()};
    EXPECT_THROW(lexer9.next(), std::runtime_error);

    script::NssLexer lexer10{"`", ctx.get()};
    EXPECT_NO_THROW(lexer10.next()); // Unrecognized character is only a warning

    script::NssLexer lexer12{R"(
        //::///////////////////////////////////////////////
        //:: Name
        //:: Copyright
        //:://////////////////////////////////////////////
        /*
            description
        */
        //:://////////////////////////////////////////////
        //:: Created By:
        //:: Created On:
        //:://////////////////////////////////////////////

        // comment
        // comment
        // comment

        int this;

        /* block-comment
            block-comment
            block-comment
            block-comment
            */

        float int_to_float(int val);
    )",
        ctx.get()};

    size_t line = 0;
    script::NssToken tok = lexer12.next();
    while (tok.type != script::NssTokenType::END) {
        line = tok.loc.line;
        tok = lexer12.next();
    }

    EXPECT_EQ(line, 26);

    script::NssLexer lexer13{">>> >>>=", ctx.get()};
    EXPECT_EQ(int(lexer13.next().type), int(script::NssTokenType::USR));
    EXPECT_EQ(int(lexer13.next().type), int(script::NssTokenType::USREQ));
}

TEST(Nss, Includes)
{
    auto ctx = std::make_unique<script::Context>();
    script::Nss nss(fs::path("test_data/user/development/circinc1.nss"), ctx.get());
    EXPECT_NO_THROW(nss.parse());
    EXPECT_EQ(nss.errors(), 0);
    EXPECT_EQ(nss.ast().include_resrefs.size(), 1);
    EXPECT_THROW(nss.process_includes(), std::runtime_error); // Recursive

    auto ctx2 = std::make_unique<script::Context>();
    script::Nss nss2(fs::path("test_data/user/development/script_preprocessor.nss"), ctx2.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_TRUE(nss2.errors() == 0);
    EXPECT_THROW(nss2.process_includes(), std::runtime_error); // Non-extant

    auto ctx3 = std::make_unique<script::Context>();
    script::Nss nss3(fs::path("test_data/user/development/test_inc.nss"), ctx3.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.ast().include_resrefs.size(), 1);
    EXPECT_TRUE(nss3.errors() == 0);
    EXPECT_NO_THROW(nss3.process_includes());
    EXPECT_EQ(nss.ast().includes.size(), 1);
    EXPECT_NO_THROW(nss3.resolve());

    auto deps3 = nss3.dependencies();
    EXPECT_EQ(deps3.size(), 2);
    EXPECT_NE(deps3.find("test_inc_i"), std::end(deps3));
}

TEST(Nss, Literals)
{
    auto ctx = std::make_unique<script::Context>();

    script::Nss nss1("void main() { float f = 0.314; }"sv, ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_EQ(nss1.errors(), 0);

    script::Nss nss2("void main() { float f = 0.314f; }"sv, ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 0);

    script::Nss nss3("void main() { float f = 2.; }"sv, ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.errors(), 0);

    script::Nss nss4("void main() { float f = 2.f; }"sv, ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_EQ(nss4.errors(), 0);

    script::Nss nss5("void main() { float f = 2; }"sv, ctx.get());
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_EQ(nss5.errors(), 0);

    script::Nss nss6("void main() { int i = 2; }"sv, ctx.get());
    EXPECT_NO_THROW(nss6.parse());
    EXPECT_EQ(nss6.errors(), 0);

    script::Nss nss7("void main() { int i = 0x123456789abcdef; }"sv, ctx.get());
    EXPECT_NO_THROW(nss7.parse());
    EXPECT_EQ(nss7.errors(), 0);

    script::Nss nss8("void main() { vector v = [1.0, 0.0f, 0.]; }"sv, ctx.get());
    EXPECT_NO_THROW(nss8.parse());
    EXPECT_EQ(nss8.errors(), 0);

    script::Nss nss9("int test(int x = -1, float y = -1.0f, int z = ~0x10, int x = !1);"sv, ctx.get());
    EXPECT_NO_THROW(nss9.parse());
    EXPECT_EQ(nss9.errors(), 0);
    auto fd = dynamic_cast<script::FunctionDecl*>(nss9.ast().decls[0].get());
    EXPECT_NE(fd, nullptr);
    if (fd) {
        for (const auto& p : fd->params) {
            EXPECT_TRUE(dynamic_cast<script::LiteralExpression*>(p->init.get()));
        }
    }
}

TEST(Nss, Switch)
{
    auto ctx = std::make_unique<script::Context>();

    // == Bad =================================================================

    // case outside of switch
    script::Nss nss1("void main() { case 1: ; }"sv, ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);

    // non-const case
    script::Nss nss2(R"(void main() {
        int x;
        switch(1) {
            case x: ++x;
        }; }
        )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 1);

    // wrong type in switch target expression
    script::Nss nss3(R"(void main() {
        int x;
        object y;
        switch(y) {
            case 1: ++x;
        }; }
        )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 1);

    // wrong type in case statement
    script::Nss nss4(R"(void main() {
        int x;
        object y;
        switch(x) {
            case 0.0f: ++x;
        }; }
        )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_NO_THROW(nss4.resolve());
    EXPECT_EQ(nss4.errors(), 1);

    // [TODO] duplicate case values

    // == Good ================================================================

    // const decls are fine in case statement
    script::Nss nss5(R"(void main() {
        const int x = 1;
        object y;
        switch(1) {
            case x: y = OBJECT_SELF;
        }; }
        )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_NO_THROW(nss5.resolve());
    EXPECT_EQ(nss5.errors(), 0);
}

TEST(Nss, BinaryExpressions)
{
    auto ctx = std::make_unique<script::Context>();

    script::Nss nss1(R"(
    void main() {
        int x = 1;
        float y = 1.0;

        x << x;
        x >> x;
        x >>> x;

        x << y;
        y << x;
        x >> y;
        y >> x;
        x >>> y;
        y >>> x;
    })"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 6);

    script::Nss nss2(R"(
    void main() {
        int x = 1;
        float y = 1.0;
        vector v;

        float z = 1; // OK
        y += x;      // OK
        x + y;       // OK
        y - x;       // OK
        x * y;       // OK
        y / x;       // OK
        v / y;       // OK
        v * y;       // OK
        y * v;       // OK
        x | x;       // OK
        x & x;       // OK

        y / v;       // Not OK
        x /= y;      // Not OK
        x | y;       // Not OK
        x & y;       // Not OK
    })"sv,
        ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 4);

    script::Nss nss3(R"(
    void main() {
        int x = 1;
        int y = 2;

        x == y;
        x != y;
        x > y;
        x >= y;
        x < y;
        x <= y;
    })"sv,
        ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 0);

    script::Nss nss4(R"(
    void main() {
        int x = 1;
        int y = 2;

        x = x || y;
        x = x && y;
        x = !y;
    })"sv,
        ctx.get());
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_NO_THROW(nss4.resolve());
    EXPECT_EQ(nss4.errors(), 0);
}

TEST(Nss, Group)
{
    auto ctx = std::make_unique<script::Context>();

    script::Nss nss1(R"(
        void main() {
            int x = 2 * (4 + 2);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 0);
}

TEST(Nss, Conditional)
{
    auto ctx = std::make_unique<script::Context>();

    // == Good ================================================================

    // non-integer bool test
    script::Nss nss3(R"(
        void main() {
            string s = 1 ? "Hi" : "Bye";
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 0);

    // == Bad =================================================================

    // non-integer bool test
    script::Nss nss1(R"(
        void main() {
            string s = "oops" ? "Hi" : "Bye";
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);

    // mismatched true/false branch types
    script::Nss nss2(R"(
        void main() {
            string s = "oops" ? "Hi" : 3;
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 2);
}

TEST(Nss, If)
{
    auto ctx = std::make_unique<script::Context>();

    // == Bad =================================================================

    // non-integer bool test
    script::Nss nss1(R"(
        void main() {
            string s = "Hello World";
            if(s) { s += "!"; }
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);

    // == Good ================================================================

    script::Nss nss2(R"(
        void main() {
            if(1)
                ;
            else
                ;
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);

    script::Nss nss3(R"(
        void main() {
            if(1){ }
            else if (2) { }
            else { }
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 0);
}

TEST(Nss, Do)
{
    auto ctx = std::make_unique<script::Context>();

    // == Bad =================================================================

    // non-integer bool test
    script::Nss nss1(R"(
        void main() {
            string s = "Hello World";
            do {

            } while(s);
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);
}

TEST(Nss, For)
{
    auto ctx = std::make_unique<script::Context>();

    // == Bad =================================================================

    // non-integer bool test
    script::Nss nss1(R"(
        void main() {
            int i;
            string s = "Hello World";
            for(i = 0; s; ++i) { }
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);

    // == Good ================================================================

    // empty
    script::Nss nss2(R"(
        void main() {
            for(;;) {
            }
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);

    // decl in init
    script::Nss nss3(R"(
        void main() {
            for(int i = 0; i < 10; ++i) {
            }
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 0);
}

TEST(Nss, While)
{
    auto ctx = std::make_unique<script::Context>();

    // == Bad =================================================================

    // non-integer bool test
    script::Nss nss1(R"(
        void main() {
            string s = "Hello World";
            while(s)
                ;
        }
    )"sv,
        ctx.get());
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);
}
