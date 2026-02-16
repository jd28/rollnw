#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstConstEvaluator.hpp>
#include <nw/smalls/AstLocator.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Lexer.hpp>
#include <nw/smalls/Smalls.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>

using namespace std::literals;
namespace fs = std::filesystem;

namespace {
struct CountingSmallsContext : nw::smalls::Context {
    void lexical_diagnostic(nw::smalls::Script*, nw::StringView, bool, nw::smalls::SourceRange) override
    {
        ++lexical_calls;
    }

    int lexical_calls = 0;
};
} // namespace

TEST_F(SmallsLexer, BasicLexing)
{
    nw::smalls::Lexer lexer{"x /= y", nw::kernel::runtime().diagnostic_context()};
    auto t1 = lexer.next();
    EXPECT_EQ(t1.loc.view(), "x");
    EXPECT_EQ(lexer.next().loc.view(), "/=");
    auto t2 = lexer.next();
    EXPECT_EQ(t2.loc.view(), "y");
    EXPECT_EQ(nw::smalls::merge_source_location(t1.loc, t2.loc).view(), "x /= y");

    nw::smalls::Lexer lexer2{"/*", nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer2.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer14{"/*/", nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer14.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer15{"/**/", nw::kernel::runtime().diagnostic_context()};
    EXPECT_NO_THROW(lexer15.next());

    nw::smalls::Lexer lexer3{"/* this is comment */", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(lexer3.next().type, nw::smalls::TokenType::COMMENT);

    nw::smalls::Lexer lexer4{"\"this is unterminated", nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer4.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer5{"\"Hello World\"", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(lexer5.next().loc.view(), "Hello World");

    nw::smalls::Lexer lexer6{"0x123456789abCdEf", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(lexer6.next().type, nw::smalls::TokenType::INTEGER_LITERAL);

    nw::smalls::Lexer lexer8{R"x(    \
    )x",
        nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(lexer8.next().type, nw::smalls::TokenType::END);

    nw::smalls::Lexer lexer9{"\"", nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer9.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer10{"`", nw::kernel::runtime().diagnostic_context()};
    EXPECT_NO_THROW(lexer10.next()); // Unrecognized character is only a warning

    nw::smalls::Lexer lexer12{R"(
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
        nw::kernel::runtime().diagnostic_context()};

    size_t line = 0;
    nw::smalls::Token tok = lexer12.next();
    while (tok.type != nw::smalls::TokenType::END) {
        line = tok.loc.range.end.line;
        tok = lexer12.next();
    }

    EXPECT_EQ(line, 26);
    EXPECT_EQ(lexer12.line_map[2], 61);
    EXPECT_EQ(lexer12.line_map[line - 1], 572);

    nw::smalls::Lexer lexer16{R"(
            R"
                This is a test of raw string lexer;
            "
        )",
        nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer16.next().type), int(nw::smalls::TokenType::STRING_RAW_LITERAL));

    nw::smalls::Lexer lexer17{R"(
            r"
                This is a test of raw string lexer;
            "
        )",
        nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer17.next().type), int(nw::smalls::TokenType::STRING_RAW_LITERAL));

    nw::smalls::Lexer lexer17b{R"(r"abc")", nw::kernel::runtime().diagnostic_context()};
    auto raw17b = lexer17b.next();
    EXPECT_EQ(raw17b.type, nw::smalls::TokenType::STRING_RAW_LITERAL);
    EXPECT_EQ(raw17b.loc.view(), "abc");

    nw::smalls::Lexer lexer18{R"(
            r"
                This is a test of raw string lexer;
        )",
        nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer18.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer19{R"(
            r"
                This is a test of raw string lexer; \"
        )",
        nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer19.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer20{R"(
            r"
                This is a test of raw string with escaped ""inside"""
        )",
        nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer20.next().type), int(nw::smalls::TokenType::STRING_RAW_LITERAL));
    EXPECT_EQ(int(lexer20.next().type), int(nw::smalls::TokenType::END));

    auto nss1 = make_script("void main() { string test = \"; }"sv);
    EXPECT_NO_THROW(nss1.parse());
    EXPECT_NO_THROW(nss1.resolve());
    EXPECT_EQ(nss1.errors(), 1);

    nw::smalls::Lexer lexer21{"0b1111", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer21.next().type), int(nw::smalls::TokenType::INTEGER_LITERAL));

    nw::smalls::Lexer lexer22{"0xDEADBEEF", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer22.next().type), int(nw::smalls::TokenType::INTEGER_LITERAL));

    nw::smalls::Lexer lexer23{"0O747", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer23.next().type), int(nw::smalls::TokenType::INTEGER_LITERAL));

    nw::smalls::Lexer lexer24{"__FUNCTION__", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer24.next().type), int(nw::smalls::TokenType::_FUNCTION_));

    nw::smalls::Lexer lexer25{"__FILE__", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer25.next().type), int(nw::smalls::TokenType::_FILE_));

    nw::smalls::Lexer lexer26{"__TIME__", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer26.next().type), int(nw::smalls::TokenType::_TIME_));

    nw::smalls::Lexer lexer27{"__DATE__", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer27.next().type), int(nw::smalls::TokenType::_DATE_));

    nw::smalls::Lexer lexer28{"__LINE__", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer28.next().type), int(nw::smalls::TokenType::_LINE_));

    nw::smalls::Lexer lexer31{R"([ ] [[ ]])", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer31.next().type), int(nw::smalls::TokenType::BRACKET_OPEN));
    EXPECT_EQ(int(lexer31.next().type), int(nw::smalls::TokenType::BRACKET_CLOSE));
    EXPECT_EQ(int(lexer31.next().type), int(nw::smalls::TokenType::ANNOTATION_OPEN));
    EXPECT_EQ(int(lexer31.next().type), int(nw::smalls::TokenType::ANNOTATION_CLOSE));

    nw::smalls::Lexer lexer32{R"(- > ->)", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer32.next().type), int(nw::smalls::TokenType::MINUS));
    EXPECT_EQ(int(lexer32.next().type), int(nw::smalls::TokenType::GT));
    EXPECT_EQ(int(lexer32.next().type), int(nw::smalls::TokenType::FUNC_RETURN));

    nw::smalls::Lexer lexer33{R"(fn var type extern from import as is elif else)", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::FN));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::VAR));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::TYPE));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::EXTERN));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::FROM));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::IMPORT));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::AS));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::IS));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::ELIF));
    EXPECT_EQ(int(lexer33.next().type), int(nw::smalls::TokenType::ELSE));

    nw::smalls::Lexer lexer34{"*/", nw::kernel::runtime().diagnostic_context()};
    EXPECT_THROW(lexer34.next(), nw::smalls::lexical_error);

    nw::smalls::Lexer lexer35{"/*\n*/x", nw::kernel::runtime().diagnostic_context()};
    EXPECT_EQ(lexer35.next().type, nw::smalls::TokenType::COMMENT);
    auto tok35 = lexer35.next();
    EXPECT_EQ(tok35.type, nw::smalls::TokenType::IDENTIFIER);
    EXPECT_EQ(tok35.loc.view(), "x");
    EXPECT_EQ(tok35.loc.range.start.line, 2);
    EXPECT_EQ(tok35.loc.range.start.column, 2);

    CountingSmallsContext counting_ctx;
    nw::smalls::Lexer lexer36{"&", &counting_ctx};
    EXPECT_EQ(lexer36.next().type, nw::smalls::TokenType::END);
    EXPECT_EQ(counting_ctx.lexical_calls, 1);
}
