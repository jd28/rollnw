#pragma once

#include "SourceLocation.hpp"

#include <iostream>

namespace nw::smalls {

enum class TokenType {
    INVALID = -1,
    END = 0,

    IDENTIFIER,
    COMMENT,

    // Punctuation
    PAREN_OPEN,    // (
    PAREN_CLOSE,   // )
    BRACE_OPEN,    // {
    BRACE_CLOSE,   // }
    BRACKET_OPEN,  // [
    BRACKET_CLOSE, // ]
    COMMA,         // ,
    COLON,         // :
    QUESTION,      // ?
    SEMICOLON,     // ;
    POUND,         // #
    DOT,           // .

    ANNOTATION_OPEN,  // [[
    ANNOTATION_CLOSE, // ]]

    // Operators
    ANDAND,      // &&
    DIV,         // /
    DIVEQ,       // /=
    EQ,          // =
    EQEQ,        // ==
    GT,          // >
    GTEQ,        // >=
    LT,          // <
    LTEQ,        // <=
    MINUS,       // -
    MINUSEQ,     // -=
    FUNC_RETURN, // ->
    MOD,         // %
    MODEQ,       // %=
    TIMES,       // *
    TIMESEQ,     // *=
    NOT,         // !
    NOTEQ,       // !=
    OR,          // '|'
    OROR,        // ||
    PLUS,        // +
    PLUSEQ,      // +=
    // Bitwise operators are handled via intrinsics

    // Constants
    BOOL_LITERAL_TRUE,
    BOOL_LITERAL_FALSE,
    FLOAT_LITERAL,
    INTEGER_LITERAL,
    STRING_LITERAL,
    STRING_RAW_LITERAL,
    FSTRING_LITERAL,

    // Keywords
    AS,       // as
    BREAK,    // break
    CASE,     // case
    CONST_,   // const
    CONTINUE, // continue
    DEFAULT,  // default
    ELSE,     // else
    EXTERN,   // extern
    FOR,      // for
    FROM,     // from
    FN,       // fn
    IF,       // if
    IMPORT,   // import
    IN,       // in (for-each)
    IS,       // is
    RETURN,   // return
    SWITCH,   // switch
    TYPE,     // type
    VAR,      // var

    // Macros
    _FUNCTION_,
    _FILE_,
    _DATE_,
    _TIME_,
    _LINE_,
};

inline SourceLocation merge_source_location(SourceLocation lhs, SourceLocation rhs)
{
    return {lhs.start, rhs.end, {lhs.range.start, rhs.range.end}};
}

struct Token {
    Token() = default;
    Token(TokenType type_, StringView id_)
        : type{type_}
    {
        loc.start = id_.data();
        loc.end = id_.data() + id_.size();
    }

    Token(TokenType type_, StringView id_, SourcePosition start, SourcePosition end)
        : type{type_}
        , loc{id_.data(), id_.data() + id_.size(), {start, end}}
    {
    }

    TokenType type = TokenType::INVALID;
    SourceLocation loc;
};

} // nw::smalls

inline std::ostream& operator<<(std::ostream& out, const nw::smalls::Token& token)
{
    out << "<'";
    out << token.loc.view();
    out << "', ";
    out << token.loc.range.start.line;
    out << ":";
    out << token.loc.range.start.column;
    out << ">";

    return out;
}
