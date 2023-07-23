#include <gtest/gtest.h>

#include <nw/log.hpp>
#include <nw/script/AstPrinter.hpp>
#include <nw/script/Nss.hpp>
#include <nw/script/NssLexer.hpp>

#include <filesystem>
#include <iostream>
#include <string_view>

using namespace std::literals;
namespace fs = std::filesystem;

using namespace nw;

TEST(Nss, Parse)
{
    script::Nss nss(fs::path("test_data/user/development/test.nss"));
    EXPECT_NO_THROW(nss.parse());
    EXPECT_TRUE(nss.errors() == 0);
    script::AstPrinter p;
    nss.ast().accept(&p);
    LOG_F(INFO, "{}", p.ss.str());

    script::Nss n2(fs::path("test_data/user/development/nw_s0_raisdead.nss"));
    EXPECT_NO_THROW(n2.parse());
    EXPECT_TRUE(n2.errors() == 0);
}

TEST(Nss, Preprocessor)
{
    script::Nss nss(fs::path("test_data/user/development/script_preprocessor.nss"));
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
    script::Nss nss(fs::path("test_data/user/scratch/nwscript.nss"));
    EXPECT_NO_THROW(nss.parse());
    EXPECT_NO_THROW(nss.resolve());
    EXPECT_TRUE(nss.errors() == 0);
    auto decl = nss.locate_export("IntToString");
    EXPECT_NE(decl, nullptr);
    auto d = dynamic_cast<nw::script::FunctionDecl*>(decl);
    EXPECT_TRUE(d);
    EXPECT_EQ(d->identifier.loc.view, "IntToString");
    EXPECT_EQ(d->type_id_, nss.ctx()->type_id("string"));

    script::Nss nss2("string test_function(string s, int b) { return IntToString(b); }"sv);
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);
}

TEST(Nss, Variables)
{
    script::Nss nss1("int a = 3, b, c = 1;"sv);
    nss1.parse();
    const script::Ast& s1 = nss1.ast();
    auto decl1 = dynamic_cast<script::DeclStatement*>(s1.decls[0].get());
    EXPECT_TRUE(decl1);
    EXPECT_EQ(decl1->decls.size(), 3);

    // Empty statement..
    script::Nss nss2("void test_function(string s, int b) { int a;; }"sv);
    nss2.parse();
    EXPECT_EQ(nss2.errors(), 0);

    // Empty statement..
    script::Nss nss3("int a;;"sv);
    nss3.parse();
    EXPECT_EQ(nss3.warnings(), 1);

    // == Bad =================================================================

    // const without initializer
    script::Nss nss4("const int VARIABLE;"sv);
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_NO_THROW(nss4.resolve());
    EXPECT_EQ(nss4.errors(), 1);

    // using declared var in initializer
    script::Nss nss5("int VARIABLE = VARIABLE;"sv);
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_NO_THROW(nss5.resolve());
    EXPECT_EQ(nss5.errors(), 1);

    // init with wrong type
    script::Nss nss6("int VARIABLE = OBJECT_INVALID;"sv);
    EXPECT_NO_THROW(nss6.parse());
    EXPECT_NO_THROW(nss6.resolve());
    EXPECT_EQ(nss6.errors(), 1);
}

TEST(Nss, FunctionDecl)
{
    script::Nss nss("void test_function(string s, int b);"sv);
    EXPECT_NO_THROW(nss.parse());
    EXPECT_EQ(nss.errors(), 0);
    const script::Ast& s = nss.ast();
    EXPECT_TRUE(s.decls.size() > 0);
    auto fd = dynamic_cast<script::FunctionDecl*>(s.decls[0].get());
    EXPECT_TRUE(fd);
    EXPECT_EQ(fd->type.type_specifier.loc.view, "void");
    EXPECT_EQ(fd->params.size(), 2);
    EXPECT_EQ(fd->params[0]->type.type_specifier.loc.view, "string");

    script::Nss nss2("void test_function(string s, int b) { s + IntToString(b); }"sv);
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 0);
    const script::Ast& s2 = nss2.ast();
    EXPECT_TRUE(s2.decls.size() > 0);
    auto fd2 = dynamic_cast<script::FunctionDefinition*>(s2.decls[0].get());
    EXPECT_TRUE(fd2);
    EXPECT_TRUE(fd2->decl);
    EXPECT_TRUE(fd2->block);
    EXPECT_EQ(fd2->block->nodes.size(), 1);

    script::Nss nss3("void test_function(string s, int b) { s; IntToString(b) +; int x = 2+2;}"sv);
    nss3.parse();
    EXPECT_EQ(nss3.errors(), 1);

    script::Nss nss4("void test_function(int b) { s + IntToString(b); }"sv);
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_EQ(nss4.errors(), 0);
    const script::Ast& s4 = nss4.ast();
    EXPECT_GT(s4.decls.size(), 0);
    auto fd4 = dynamic_cast<script::FunctionDefinition*>(s4.decls[0].get());
    EXPECT_TRUE(fd4);
    EXPECT_TRUE(fd4->decl);
    EXPECT_TRUE(fd4->block);
    EXPECT_EQ(fd4->block->nodes.size(), 1);

    script::Nss nss5("int test_function(int a, int b) { return a % (b + x); }"sv);
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_EQ(nss5.errors(), 0);
}

TEST(Nss, FunctionCall)
{
    script::Nss nss1("void main() { test_function(b); }"sv);
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_EQ(nss1.errors(), 0);

    // == Bad =================================================================

    script::Nss nss2("void main() { 423(b); }"sv);
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 1);

    script::Nss nss3("void main() { str.test(b); }"sv);
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.errors(), 1);
}

TEST(Nss, DotOperator)
{
    // == Good Parse ==========================================================
    script::Nss nss1("void main() { str.test; }"sv);
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_EQ(nss1.errors(), 0);

    script::Nss nss2("void main() { str.test.nested; }"sv);
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 0);

    script::Nss nss3("void main() { str().test.nested; }"sv);
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.errors(), 0);

    // == Bad Parse ============================================================

    // Only variable decl or chained dot operator
    script::Nss nss4("void main() { str.test.call_expr(); }"sv);
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_EQ(nss4.errors(), 1);
}

TEST(Nss, Struct)
{
    // == Good =================================================================

    script::Nss nss1(R"(
        struct str { int test; };

        void main() {
            struct str s;
            s.test;
        }
    )"sv);
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
    )"sv);

    EXPECT_NO_THROW(nss2.parse());
    EXPECT_NO_THROW(nss2.resolve());
    EXPECT_EQ(nss2.errors(), 0);

    // == Bad =================================================================

    script::Nss nss3(R"(
        struct a { int test1; };

        void main() {
            struct a s;
            s.test2;
        }
    )"sv);

    EXPECT_NO_THROW(nss3.parse());
    EXPECT_NO_THROW(nss3.resolve());
    EXPECT_EQ(nss3.errors(), 1);
}

TEST(Nss, Lexer)
{
    script::NssLexer lexer{"x /= y"};
    EXPECT_EQ(lexer.next().loc.view, "x");
    EXPECT_EQ(lexer.next().loc.view, "/=");
    EXPECT_EQ(lexer.next().loc.view, "y");

    script::NssLexer lexer2{"/*"};
    EXPECT_THROW(lexer2.next(), std::runtime_error);

    script::NssLexer lexer3{"/* this is comment */"};
    EXPECT_EQ(lexer3.next().type, script::NssTokenType::COMMENT);

    script::NssLexer lexer4{"\"this is unterminated"};
    EXPECT_THROW(lexer4.next(), std::runtime_error);

    script::NssLexer lexer5{"\"Hello World\""};
    EXPECT_EQ(lexer5.next().loc.view, "Hello World");

    script::NssLexer lexer6{"0x123456789abCdEf"};
    EXPECT_EQ(lexer6.next().type, script::NssTokenType::INTEGER_CONST);

    script::NssLexer lexer7{"~value"};
    EXPECT_EQ(lexer7.next().type, script::NssTokenType::TILDE);

    script::NssLexer lexer8{R"x(    \
    )x"};
    EXPECT_EQ(lexer8.next().type, script::NssTokenType::END);
}

TEST(Nss, Includes)
{
    script::Nss nss(fs::path("test_data/user/development/circinc1.nss"));
    EXPECT_NO_THROW(nss.parse());
    EXPECT_EQ(nss.errors(), 0);
    EXPECT_EQ(nss.ast().include_resrefs.size(), 1);
    EXPECT_THROW(nss.process_includes(), std::runtime_error); // Recursive

    script::Nss nss2(fs::path("test_data/user/development/script_preprocessor.nss"));
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_TRUE(nss2.errors() == 0);
    EXPECT_THROW(nss2.process_includes(), std::runtime_error); // Non-extant

    script::Nss nss3(fs::path("test_data/user/development/test_inc.nss"));
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.ast().include_resrefs.size(), 1);
    EXPECT_TRUE(nss3.errors() == 0);
    EXPECT_NO_THROW(nss3.process_includes());
    EXPECT_EQ(nss.ast().includes.size(), 1);

    auto deps3 = nss3.dependencies();
    EXPECT_EQ(deps3.size(), 1);
    EXPECT_NE(deps3.find("test_inc_i"), std::end(deps3));
}

TEST(Nss, Literals)
{
    script::Nss nss1("void main() { float f = 0.314; }"sv);
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_EQ(nss1.errors(), 0);

    script::Nss nss2("void main() { float f = 0.314f; }"sv);
    EXPECT_NO_THROW(nss2.parse());
    EXPECT_EQ(nss2.errors(), 0);

    script::Nss nss3("void main() { float f = 2.; }"sv);
    EXPECT_NO_THROW(nss3.parse());
    EXPECT_EQ(nss3.errors(), 0);

    script::Nss nss4("void main() { float f = 2.f; }"sv);
    EXPECT_NO_THROW(nss4.parse());
    EXPECT_EQ(nss4.errors(), 0);

    script::Nss nss5("void main() { float f = 2; }"sv);
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_EQ(nss5.errors(), 0);

    script::Nss nss6("void main() { int i = 2; }"sv);
    EXPECT_NO_THROW(nss6.parse());
    EXPECT_EQ(nss6.errors(), 0);

    script::Nss nss7("void main() { int i = 0x123456789abcdef; }"sv);
    EXPECT_NO_THROW(nss7.parse());
    EXPECT_EQ(nss7.errors(), 0);

    script::Nss nss8("void main() { vector v = [1.0, 0.0f, 0.]; }"sv);
    EXPECT_NO_THROW(nss8.parse());
    EXPECT_EQ(nss8.errors(), 0);
}

TEST(Nss, Switch)
{
    // == Bad =================================================================

    // case outside of switch
    script::Nss nss1("void main() { case 1: ; }"sv);
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);

    // non-const case
    script::Nss nss2(R"(void main() {
        int x;
        switch(1) {
            case x: ++x;
        }; }
        )"sv);
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
        )"sv);
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
        )"sv);
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
        )"sv);
    EXPECT_NO_THROW(nss5.parse());
    EXPECT_NO_THROW(nss5.resolve());
    EXPECT_EQ(nss5.errors(), 0);
}
