#pragma once

#include <iostream>
#include <string_view>

namespace nw {

enum class NssTokenType {
    INVALID = -1,
    END = 0,

    // Identifier
    IDENTIFIER,

    // Punctuation
    LPAREN,    // (
    RPAREN,    // )
    LBRACE,    // {
    RBRACE,    // }
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
    CONST,        // const
    CONTINUE,     // continue
    DEFAULT,      // default
    DO,           // do
    EFFECT,       // effect
    ELSE,         // else
    FLOAT,        // float
    FOR,          // for
    IF,           // if
    INT,          // int
    ITEMPROPERTY, // itemproperty
    OBJECT,       // object
    RETURN,       // return
    STRING,       // string
    STRUCT,       // struct
    SWITCH,       // switch
    TALENT,       // talent
    VECTOR,       // vector
    VOID,         // void
    WHILE,        // while
};

struct NssToken {
    NssToken() = default;
    NssToken(NssTokenType type, std::string_view id, size_t line, size_t start, size_t end);
    NssTokenType type = NssTokenType::INVALID;
    std::string_view id;
    size_t line, start, end;
};

struct NssLexer {
    explicit NssLexer(std::string_view buffer);

    char get(size_t pos) const;
    bool match(std::string_view str) const;
    NssToken next();
    NssToken handle_identifier();
    NssToken handle_number();
    const NssToken& current() const;
    const char* data() const;

private:
    std::string_view buffer_;
    size_t pos_ = 0, line_ = 0, last_line_pos_ = 0;
    NssToken current_;
};

} // namespace nw

inline std::ostream& operator<<(std::ostream& out, const nw::NssToken& token)
{
    out << "<'";
    out << token.id;
    out << "', ";
    out << token.line;
    out << ":";
    out << token.start;
    out << ":";
    out << token.end;
    out << ">";

    return out;
}
