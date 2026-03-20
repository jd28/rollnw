#include <gtest/gtest.h>

#include "../tools/smalls-lsp/lsp_text.hpp"

// ============================================================================
// identifier_at — returns the identifier under a given (line, col) position
// ============================================================================

TEST(LspText, IdentifierAt_MiddleOfWord)
{
    std::string text = "var hello = 42;";
    // col 5 is 'l' inside "hello" (0-indexed)
    EXPECT_EQ(identifier_at(text, 0, 5), "hello");
}

TEST(LspText, IdentifierAt_StartOfWord)
{
    std::string text = "var hello = 42;";
    // col 4 is the 'h'
    EXPECT_EQ(identifier_at(text, 0, 4), "hello");
}

TEST(LspText, IdentifierAt_EndOfWord)
{
    std::string text = "var hello = 42;";
    // col 8 is the last 'o'; col 9 is space — col 8 should return "hello"
    EXPECT_EQ(identifier_at(text, 0, 8), "hello");
}

TEST(LspText, IdentifierAt_OnSpace)
{
    std::string text = "var hello = 42;";
    // col 3 is the space between "var" and "hello"
    EXPECT_EQ(identifier_at(text, 0, 3), "");
}

TEST(LspText, IdentifierAt_WithUnderscore)
{
    std::string text = "my_variable_name";
    EXPECT_EQ(identifier_at(text, 0, 8), "my_variable_name");
}

TEST(LspText, IdentifierAt_SecondLine)
{
    std::string text = "first_line\nsecond_line";
    EXPECT_EQ(identifier_at(text, 1, 3), "second_line");
}

TEST(LspText, IdentifierAt_ThirdLine)
{
    std::string text = "line_one\nline_two\nfoo_bar";
    EXPECT_EQ(identifier_at(text, 2, 0), "foo_bar");
}

TEST(LspText, IdentifierAt_PastEndOfLine)
{
    std::string text = "ab\ncd";
    // Line 0 has only 2 chars; col 100 is out of range
    EXPECT_EQ(identifier_at(text, 0, 100), "");
}

TEST(LspText, IdentifierAt_OnOperator)
{
    std::string text = "a + b";
    EXPECT_EQ(identifier_at(text, 0, 2), ""); // '+' is not an identifier char
}

TEST(LspText, IdentifierAt_SingleChar)
{
    std::string text = "x";
    EXPECT_EQ(identifier_at(text, 0, 0), "x");
}

TEST(LspText, IdentifierAt_ColAfterLastChar)
{
    // col == length of word: cursor is just past the identifier
    std::string text = "foo bar";
    // "foo" is cols 0-2; col 3 is the space
    EXPECT_EQ(identifier_at(text, 0, 3), "");
}

// ============================================================================
// identifier_before — returns identifier immediately before the dot at dot_col-1
// ============================================================================

TEST(LspText, IdentifierBefore_Simple)
{
    // "foo.bar", dot is at col 3, so dot_col = 4 (column right after dot)
    std::string text = "foo.bar";
    EXPECT_EQ(identifier_before(text, 0, 4), "foo");
}

TEST(LspText, IdentifierBefore_WithLeadingSpaces)
{
    // "  foo.bar", dot is at col 5, dot_col = 6
    std::string text = "  foo.bar";
    EXPECT_EQ(identifier_before(text, 0, 6), "foo");
}

TEST(LspText, IdentifierBefore_DotAtStartOfLine)
{
    // dot_col = 1 means dot is at col 0, nothing before it
    std::string text = ".bar";
    EXPECT_EQ(identifier_before(text, 0, 1), "");
}

TEST(LspText, IdentifierBefore_WithUnderscore)
{
    std::string text = "my_obj.field";
    // dot at col 6, dot_col = 7
    EXPECT_EQ(identifier_before(text, 0, 7), "my_obj");
}

TEST(LspText, IdentifierBefore_SecondLine)
{
    std::string text = "line_one\nobj.member";
    // On line 1: "obj" ends at col 2, dot at col 3, dot_col = 4
    EXPECT_EQ(identifier_before(text, 1, 4), "obj");
}

TEST(LspText, IdentifierBefore_SpaceBeforeDot)
{
    // "foo .bar" — space directly before dot, so no identifier
    std::string text = "foo .bar";
    // dot at col 4, dot_col = 5
    EXPECT_EQ(identifier_before(text, 0, 5), "");
}

TEST(LspText, IdentifierBefore_ChainedDots)
{
    // "a.b.c" — cursor at dot_col=4 (the second dot col+1)
    // Should return "b" (identifier between first and second dot)
    std::string text = "a.b.c";
    EXPECT_EQ(identifier_before(text, 0, 4), "b");
}

// ============================================================================
// detect_dot_trigger — detects dot-completion context from buffer position
// ============================================================================

TEST(LspText, DetectDotTrigger_CursorAtEndOfWordAfterDot)
{
    // "foo.bar|" — cursor at character=7 (end of "bar")
    std::string text = "foo.bar";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 0, 7, dot_col));
    EXPECT_EQ(dot_col, 4); // column right after the dot
}

TEST(LspText, DetectDotTrigger_CursorMidWordAfterDot)
{
    // "foo.ba|" — cursor at character=6 (mid-word after dot)
    std::string text = "foo.ba";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 0, 6, dot_col));
    EXPECT_EQ(dot_col, 4);
}

TEST(LspText, DetectDotTrigger_CursorRightAfterDot)
{
    // "foo.|" — cursor right after the dot, no partial word typed yet
    std::string text = "foo.";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 0, 4, dot_col));
    EXPECT_EQ(dot_col, 4);
}

TEST(LspText, DetectDotTrigger_NoDot)
{
    // "foobar|" — no dot anywhere
    std::string text = "foobar";
    int dot_col = 0;
    EXPECT_FALSE(detect_dot_trigger(text, 0, 6, dot_col));
}

TEST(LspText, DetectDotTrigger_SpaceBeforeCursor)
{
    // "foo bar|" — space before partial word, no dot
    std::string text = "foo bar";
    int dot_col = 0;
    EXPECT_FALSE(detect_dot_trigger(text, 0, 7, dot_col));
}

TEST(LspText, DetectDotTrigger_ChainedDots)
{
    // "a.b.ba|" — cursor after "ba", should detect the second dot
    std::string text = "a.b.ba";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 0, 6, dot_col));
    EXPECT_EQ(dot_col, 4); // column right after the second dot
}

TEST(LspText, DetectDotTrigger_SecondLine)
{
    // Two-line buffer; dot trigger on line 1
    std::string text = "import foo;\nfoo.bar";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 1, 7, dot_col));
    EXPECT_EQ(dot_col, 4);
}

TEST(LspText, DetectDotTrigger_CursorAtStartOfLine)
{
    // No characters before cursor → no dot possible
    std::string text = "foo.bar";
    int dot_col = 0;
    EXPECT_FALSE(detect_dot_trigger(text, 0, 0, dot_col));
}

TEST(LspText, IdentifierBefore_ModuleAlias)
{
    // "import foo.bar as utils;" and then usage: "utils.Vec2"
    // When user types "utils." and cursor is right after dot, dot_col = 7
    std::string text = "utils.Vec2";
    // dot at col 5, dot_col = 6 (right after dot)
    EXPECT_EQ(identifier_before(text, 0, 6), "utils");
}

TEST(LspText, DetectDotTrigger_ModuleAlias)
{
    // "utils.Vec2|" — cursor at end, should detect dot trigger
    std::string text = "utils.Vec2";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 0, 10, dot_col));
    EXPECT_EQ(dot_col, 6); // column right after the dot
    // And identifier_before gives us "utils"
    EXPECT_EQ(identifier_before(text, 0, dot_col), "utils");
}

TEST(LspText, DetectDotTrigger_DotAtStartOfLine)
{
    // ".bar|" — the dot IS at col 0, and scanning left past "bar" finds it.
    // dot_col=1 (right after the dot). identifier_before with dot_col=1 would
    // return "" (nothing before the dot), so complete_dot gets an empty needle.
    std::string text = ".bar";
    int dot_col = 0;
    EXPECT_TRUE(detect_dot_trigger(text, 0, 4, dot_col));
    EXPECT_EQ(dot_col, 1);
}
