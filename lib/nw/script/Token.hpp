#pragma once

#include "SourceLocation.hpp"

#include <iostream>

namespace nw::script {

enum class NssTokenType {
    INVALID = -1,
    END = 0,

    IDENTIFIER,
    COMMENT,

    // Punctuation
    LPAREN,    // (
    RPAREN,    // )
    LBRACE,    // {
    RBRACE,    // }
    LBRACKET,  // [
    RBRACKET,  // ]
    COMMA,     // ,
    COLON,     // :
    QUESTION,  // ?
    SEMICOLON, // ;
    POUND,     // #
    DOT,       // .

    // Operators
    AND,        // '&'
    ANDAND,     // &&
    ANDEQ,      // &=
    DIV,        // /
    DIVEQ,      // /=
    EQ,         // =
    EQEQ,       // ==
    GT,         // >
    GTEQ,       // >=
    LT,         // <
    LTEQ,       // <=
    MINUS,      // -
    MINUSEQ,    // -=
    MINUSMINUS, // --
    MOD,        // %
    MODEQ,      // %=
    TIMES,      // *
    TIMESEQ,    // *=
    NOT,        // !
    NOTEQ,      // !=
    OR,         // '|'
    OREQ,       // |=
    OROR,       // ||
    PLUS,       // +
    PLUSEQ,     // +=
    PLUSPLUS,   // ++
    SL,         // <<
    SLEQ,       // <<=
    SR,         // >>
    SREQ,       // >>=
    TILDE,      // ~
    USR,        // >>>
    USREQ,      // >>>=
    XOR,        // ^
    XOREQ,      // ^=

    // Constants
    FLOAT_CONST,
    INTEGER_CONST,
    OBJECT_INVALID_CONST,
    OBJECT_SELF_CONST,
    STRING_CONST,
    LOCATION_INVALID,
    JSON_CONST,

    // Keywords
    ACTION,       // action
    BREAK,        // break
    CASE,         // case
    CASSOWARY,    // cassowary
    CONST_,       // const
    CONTINUE,     // continue
    DEFAULT,      // default
    DO,           // do
    EFFECT,       // effect
    ELSE,         // else
    EVENT,        // event
    FLOAT,        // float
    FOR,          // for
    IF,           // if
    INT,          // int
    ITEMPROPERTY, // itemproperty
    JSON,         // json
    LOCATION,     // location
    OBJECT,       // object
    RETURN,       // return
    STRING,       // string
    STRUCT,       // struct
    SQLQUERY,     // sqlquery
    SWITCH,       // switch
    TALENT,       // talent
    VECTOR,       // vector
    VOID_,        // void
    WHILE,        // while
};

inline SourceLocation merge_source_location(SourceLocation lhs, SourceLocation rhs)
{
    return {lhs.start, rhs.end, {lhs.range.start, rhs.range.end}};
}

struct NssToken {
    NssToken() = default;
    NssToken(NssTokenType type_, std::string_view id_)
        : type{type_}
        , loc{id_.data(), id_.data() + id_.size()}
    {
    }

    NssToken(NssTokenType type_, std::string_view id_, SourcePosition start, SourcePosition end)
        : type{type_}
        , loc{id_.data(), id_.data() + id_.size(), {start, end}}
    {
    }

    NssTokenType type = NssTokenType::INVALID;
    SourceLocation loc;
};

} // nw::script

inline std::ostream& operator<<(std::ostream& out, const nw::script::NssToken& token)
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
