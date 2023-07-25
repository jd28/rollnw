#pragma once

#include <iostream>
#include <string_view>

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

struct SourceLocation {
    const char* start = nullptr;
    const char* end = nullptr;
    size_t line = 0;
    size_t column = 0;

    std::string_view view() const
    {
        if (!start || !end) { return {}; }
        return std::string_view{start, size_t(end - start)};
    }
};

inline SourceLocation merge_source_location(SourceLocation lhs, SourceLocation rhs)
{
    return {lhs.start, rhs.end, lhs.line, lhs.column};
}

struct NssToken {
    NssToken() = default;
    NssToken(NssTokenType type_, std::string_view id_, size_t line_, size_t start_)
        : type{type_}
        , loc{id_.data(), id_.data() + id_.size(), line_, start_}
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
    out << token.loc.line;
    out << ":";
    out << token.loc.column;
    out << ">";

    return out;
}
