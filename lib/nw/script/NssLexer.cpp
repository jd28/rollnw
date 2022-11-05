#include "NssLexer.hpp"

#include "../log.hpp"

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace nw::script {

NssToken::NssToken(NssTokenType type_, std::string_view id_, size_t line_, size_t start_, size_t end_)
    : type{type_}
    , id{id_}
    , line{line_}
    , start{start_}
    , end{end_}
{
}

NssLexer::NssLexer(std::string_view buffer)
    : buffer_{buffer}
{
}

const NssToken& NssLexer::current() const
{
    return current_;
}

const char* NssLexer::data() const
{
    return buffer_.data() + pos_;
}

char NssLexer::get(size_t pos) const
{
    return pos >= buffer_.size() ? 0 : buffer_[pos];
}

inline NssTokenType check_keyword(const NssToken& tk)
{
    switch (tk.id[0]) {
    default:
        return NssTokenType::IDENTIFIER;
        // Keywords
    case 'a':
        if (tk.id == "action") {
            return NssTokenType::ACTION;
        }
        break;
    case 'b':
        if (tk.id == "break") {
            return NssTokenType::BREAK;
        }
        break;
    case 'c':
        if (tk.id == "case") {
            return NssTokenType::CASE;
            // CASE
        } else if (tk.id == "cassowary") {
            return NssTokenType::CASSOWARY;
        } else if (tk.id == "const") {
            return NssTokenType::CONST_;
        } else if (tk.id == "continue") {
            return NssTokenType::CONTINUE;
        }
        break;
    case 'd':
        if (tk.id == "default") {
            return NssTokenType::DEFAULT;
        } else if (tk.id == "do") {
            return NssTokenType::DO;
        }
        break;
    case 'e':
        if (tk.id == "effect") {
            return NssTokenType::EFFECT;
        } else if (tk.id == "else") {
            return NssTokenType::ELSE;
        } else if (tk.id == "event") {
            return NssTokenType::EVENT;
        }
        break;
    case 'f':
        if (tk.id == "float") {
            return NssTokenType::FLOAT;
        } else if (tk.id == "for") {
            return NssTokenType::FOR;
        }
        break;
    case 'i':
        if (tk.id == "if") {
            return NssTokenType::IF;
        } else if (tk.id == "int") {
            return NssTokenType::INT;
        } else if (tk.id == "itemproperty") {
            return NssTokenType::ITEMPROPERTY;
        }
        break;
    case 'j':
        if (tk.id == "json") {
            return NssTokenType::JSON;
        }
        break;
    case 'l':
        if (tk.id == "location") {
            return NssTokenType::LOCATION;
        }
        break;
    case 'o':
        if (tk.id == "object") {
            return NssTokenType::OBJECT;
        }
        break;
    case 'r':
        if (tk.id == "return") {
            return NssTokenType::RETURN;
        }
        break;
    case 's':
        if (tk.id == "string") {
            return NssTokenType::STRING;
        } else if (tk.id == "struct") {
            return NssTokenType::STRUCT;
        } else if (tk.id == "switch") {
            return NssTokenType::SWITCH;
        } else if (tk.id == "sqlquery") {
            return NssTokenType::SQLQUERY;
        }
        break;
    case 't':
        if (tk.id == "talent") {
            return NssTokenType::TALENT;
        }
        break;
    case 'v':
        if (tk.id == "vector") {
            return NssTokenType::VECTOR;
        } else if (tk.id == "void") {
            return NssTokenType::VOID_;
        }
        break;
    case 'w':
        if (tk.id == "while") {
            return NssTokenType::WHILE;
        }
        break;
    }
    return NssTokenType::IDENTIFIER;
}

NssToken NssLexer::handle_identifier()
{
    size_t start = pos_;
    while (pos_ < buffer_.size()) {
        if (!std::isalnum(get(pos_)) && get(pos_) != '_') {
            break;
        }
        ++pos_;
    }
    auto t = NssToken{NssTokenType::IDENTIFIER,
        {buffer_.data() + start, pos_ - start},
        line_,
        start - last_line_pos_,
        pos_ - last_line_pos_};
    t.type = check_keyword(t);
    return t;
}

NssToken NssLexer::handle_number()
{
    size_t start = pos_;
    bool is_float = false;
    bool is_hex = get(pos_) == '0' && (get(pos_ + 1) == 'x' || get(pos_ + 1) == 'X');
    if (is_hex) { pos_ += 2; }

    while (pos_ < buffer_.size()) {
        if (is_hex) {
            if (std::isdigit(get(pos_))
                || (get(pos_) >= 'a' && get(pos_) <= 'f')
                || (get(pos_) >= 'A' && get(pos_) <= 'F')) {
                pos_++;
            } else {
                break;
            }
        } else {
            if (get(pos_) == '.') {
                ++pos_;
                is_float = true;
            } else if (!std::isdigit(get(pos_))) {
                break;
            } else {
                ++pos_;
            }
        }
    }
    if (is_float && get(pos_) == 'f') {
        ++pos_;
    }

    auto type = is_float ? NssTokenType::FLOAT_CONST : NssTokenType::INTEGER_CONST;
    return NssToken{type, {buffer_.data() + start, pos_ - start}, line_, start - last_line_pos_, pos_ - last_line_pos_};
}

NssToken NssLexer::next()
{
#define NSS_TOKEN(type, size)                                                                                          \
    do {                                                                                                               \
        t = NssToken{NssTokenType::type, {data(), size}, line_, start - last_line_pos_, pos_ + size - last_line_pos_}; \
        pos_ += size;                                                                                                  \
    } while (0)

    while (pos_ < buffer_.size()) {
        size_t start = pos_;
        NssToken t;

        switch (get(pos_)) {
        default:
            if (std::isalpha(get(pos_)) || get(pos_) == '_') {
                t = handle_identifier();
            } else if (std::isdigit(get(pos_))) {
                t = handle_number();
            } else {
                LOG_F(WARNING, "Unrecognized character '{}', line: {}", get(pos_), line_);
                ++pos_;
                continue;
            }
            break;

        // Whitespace
        case ' ':
        case '\t':
            ++pos_;
            continue;
        case '\r':
            ++line_;
            ++pos_;
            if (get(pos_) == '\n') ++pos_;
            last_line_pos_ = pos_;
            continue;
        case '\n':
            ++line_;
            ++pos_;
            last_line_pos_ = pos_;
            continue;

        // Punctuation
        case '#':
            NSS_TOKEN(POUND, 1);
            break;
        case '(':
            NSS_TOKEN(LPAREN, 1);
            break;
        case ')':
            NSS_TOKEN(RPAREN, 1);
            break;
        case '{':
            NSS_TOKEN(LBRACE, 1);
            break;
        case '}':
            NSS_TOKEN(RBRACE, 1);
            break;
        case '[':
            NSS_TOKEN(LBRACKET, 1);
            break;
        case ']':
            NSS_TOKEN(RBRACKET, 1);
            break;
        case ',':
            NSS_TOKEN(COMMA, 1);
            break;
        case ':':
            NSS_TOKEN(COLON, 1);
            break;
        case '?':
            NSS_TOKEN(QUESTION, 1);
            break;
        case ';':
            NSS_TOKEN(SEMICOLON, 1);
            break;
        case '.':
            NSS_TOKEN(DOT, 1);
            break;

        // String constants
        case '"': // String constant
            start = ++pos_;
            while (pos_ < buffer_.size()) {
                if (get(pos_) == '"' && buffer_[pos_ - 1] != '\\') {
                    break;
                }
                ++pos_;
            }
            if (pos_ == buffer_.size() || get(pos_) != '"')
                throw std::runtime_error("Unterminated quote.");

            t = NssToken{NssTokenType::STRING_CONST,
                {&buffer_[start], pos_ - start},
                line_,
                start - last_line_pos_,
                pos_ - last_line_pos_};
            ++pos_;

            break;

        // Operators
        case '=': // PLUS
            switch (get(pos_ + 1)) {
            case '=':
                NSS_TOKEN(EQEQ, 2);
                break;
            default:
                NSS_TOKEN(EQ, 1);
                break;
            }
            break;
        case '!': // PLUS
            switch (get(pos_ + 1)) {
            case '=':
                NSS_TOKEN(NOTEQ, 2);
                break;
            default:
                NSS_TOKEN(NOT, 1);
                break;
            }
            break;
        case '+': // PLUS
            switch (get(pos_ + 1)) {
            case '=':
                NSS_TOKEN(PLUSEQ, 2);
                break;
            case '+':
                NSS_TOKEN(PLUSPLUS, 2);
                break;
            default:
                NSS_TOKEN(PLUS, 1);
                break;
            }
            break;
        case '-': // MINUS
            switch (get(pos_ + 1)) {
            case '=':
                NSS_TOKEN(MINUSEQ, 2);
                break;
            case '-':
                NSS_TOKEN(MINUSMINUS, 2);
                break;
            default:
                NSS_TOKEN(MINUS, 1);
                break;
            }
            break;
        case '>': // GT
            switch (get(pos_ + 1)) {
            case '>':
                switch (get(pos_ + 2)) {
                case '=':
                    NSS_TOKEN(SREQ, 3);
                    break;
                default:
                    NSS_TOKEN(SR, 2);
                    break;
                }
                break;
            case '=':
                NSS_TOKEN(GTEQ, 2);
                break;
            default:
                NSS_TOKEN(GT, 1);
                break;
            }
            break;
        case '<': // LT
            switch (get(pos_ + 1)) {
            case '<':
                switch (get(pos_ + 2)) {
                case '=':
                    NSS_TOKEN(SLEQ, 3);
                    break;
                default:
                    NSS_TOKEN(SL, 2);
                    break;
                }
                break;
            case '=':
                NSS_TOKEN(LTEQ, 2);
                break;
            default:
                NSS_TOKEN(LT, 1);
                break;
            }
            break;
        case '%': // MOD
            switch (get(pos_ + 1)) {
            case '=':
                NSS_TOKEN(MODEQ, 2);
                break;
            default:
                NSS_TOKEN(MOD, 1);
                break;
            }
            break;
        case '~': // TILDE
            NSS_TOKEN(TILDE, 1);
            break;
        case '&': // AND
            switch (get(pos_ + 1)) {
            case '&':
                NSS_TOKEN(ANDAND, 2);
                break;
            case '=':
                NSS_TOKEN(ANDEQ, 2);
                break;
            default:
                NSS_TOKEN(AND, 1);
                break;
            }
            break;
        case '^': // XOR
            switch (get(pos_ + 1)) {
            case '=':
                NSS_TOKEN(XOREQ, 2);
                break;
            default:
                NSS_TOKEN(XOR, 1);
                break;
            }
            break;
        case '|': // OR
            switch (get(pos_ + 1)) {
            case '|':
                NSS_TOKEN(OROR, 2);
                break;
            case '=':
                NSS_TOKEN(OREQ, 2);
                break;
            default:
                NSS_TOKEN(OR, 1);
                break;
            }
            break;
        case '*': // TIMES
            switch (get(pos_ + 1)) {
            case '/': // Uh oh
                throw std::runtime_error("Mismatched block quote");
                break;
            case '=':
                NSS_TOKEN(TIMESEQ, 2);
                break;
            default:
                NSS_TOKEN(TIMES, 1);
                break;
            }
            break;
        case '\\': // Escape character
            // A couple scripts have escaped new lines (for no reason)
            if (get(pos_ + 1) != '\r' && get(pos_ + 1) != '\n') {
                LOG_F(WARNING, "Unrecognized character '{}', line: {}", get(pos_), line_);
            }
            ++pos_;
            continue;
        case '/': // DIV
            switch (get(pos_ + 1)) {
            case '/': // Comment
                pos_ += 2;
                while (pos_ < buffer_.size()) {
                    if (get(pos_) == '\n') {
                        ++line_;
                        break;
                    }
                    ++pos_;
                }
                continue;
            case '*': // Block Comment
                pos_ += 4;
                while (pos_ < buffer_.size()) {
                    if (get(pos_) == '\n') { ++line_; }
                    if (get(pos_ - 1) == '*' && get(pos_) == '/')
                        break;
                    ++pos_;
                }
                if (pos_ >= buffer_.size()) {
                    throw std::runtime_error("Unterminated block quote");
                } else {
                    ++pos_;
                }
                continue;
            case '=':
                NSS_TOKEN(DIVEQ, 2);
                break;
            default:
                NSS_TOKEN(DIV, 1);
                break;
            }
            break;
        }

        if (t.type != NssTokenType::INVALID) {
            return current_ = t;
        } else {
            return current_ = NssToken{NssTokenType::END, {}, line_, pos_, pos_};
        }
    }
    return current_ = NssToken{NssTokenType::END, {}, line_, pos_, pos_};
}

} // namespace nw::script
