#include "NssLexer.hpp"

#include "../kernel/Kernel.hpp"
#include "../log.hpp"
#include "Nss.hpp"

#include <xsimd/xsimd.hpp>

#include <array>
#include <cctype>
#include <cstring>
#include <string_view>

namespace nw::script {

enum struct CharType {
    none,
    alpha,
    digit,
};

constexpr std::array<CharType, 256> create_character_map()
{
    std::array<CharType, 256> result = {};
    for (size_t i = 0; i < 256; ++i) {
        if (i >= '0' && i <= '9') {
            result[i] = CharType::digit;
        } else if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z')) {
            result[i] = CharType::alpha;
        } else {
            result[i] = CharType::none;
        }
    }
    return result;
}
constexpr auto char_map = create_character_map();

NssLexer::NssLexer(StringView buffer, Context* ctx, Nss* parent)
    : ctx_{ctx}
    , parent_{parent}
    , buffer_{buffer}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
    line_map.push_back(0);
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
    switch (tk.loc.view()[0]) {
    default:
        return NssTokenType::IDENTIFIER;
    // Macros
    case '_':
        if (tk.loc.view() == "__FUNCTION__") {
            return NssTokenType::_FUNCTION_;
        } else if (tk.loc.view() == "__FILE__") {
            return NssTokenType::_FILE_;
        } else if (tk.loc.view() == "__DATE__") {
            return NssTokenType::_DATE_;
        } else if (tk.loc.view() == "__TIME__") {
            return NssTokenType::_TIME_;
        } else if (tk.loc.view() == "__LINE__") {
            return NssTokenType::_LINE_;
        }
        break;

        // Keywords
    case 'a':
        if (tk.loc.view() == "action") {
            return NssTokenType::ACTION;
        }
        break;
    case 'b':
        if (tk.loc.view() == "break") {
            return NssTokenType::BREAK;
        }
        break;
    case 'c':
        if (tk.loc.view() == "case") {
            return NssTokenType::CASE;
            // CASE
        } else if (kernel::config().version() == nw::GameVersion::vEE
            && tk.loc.view() == "cassowary") {
            return NssTokenType::CASSOWARY;
        } else if (tk.loc.view() == "const") {
            return NssTokenType::CONST_;
        } else if (tk.loc.view() == "continue") {
            return NssTokenType::CONTINUE;
        }
        break;
    case 'd':
        if (tk.loc.view() == "default") {
            return NssTokenType::DEFAULT;
        } else if (tk.loc.view() == "do") {
            return NssTokenType::DO;
        }
        break;
    case 'e':
        if (tk.loc.view() == "effect") {
            return NssTokenType::EFFECT;
        } else if (tk.loc.view() == "else") {
            return NssTokenType::ELSE;
        } else if (tk.loc.view() == "event") {
            return NssTokenType::EVENT;
        }
        break;
    case 'f':
        if (tk.loc.view() == "float") {
            return NssTokenType::FLOAT;
        } else if (tk.loc.view() == "for") {
            return NssTokenType::FOR;
        }
        break;
    case 'i':
        if (tk.loc.view() == "if") {
            return NssTokenType::IF;
        } else if (tk.loc.view() == "int") {
            return NssTokenType::INT;
        } else if (tk.loc.view() == "itemproperty") {
            return NssTokenType::ITEMPROPERTY;
        }
        break;
    case 'j':
        if (kernel::config().version() == nw::GameVersion::vEE) {
            if (tk.loc.view() == "json") {
                return NssTokenType::JSON;
            }
        }
        break;
    case 'J':
        if (kernel::config().version() == nw::GameVersion::vEE) {
            for (auto json_const : {"JSON_FALSE", "JSON_TRUE", "JSON_OBJECT", "JSON_ARRAY", "JSON_STRING"}) {
                if (tk.loc.view() == json_const) { return NssTokenType::JSON_CONST; }
            }
        }
        break;
    case 'l':
        if (tk.loc.view() == "location") {
            return NssTokenType::LOCATION;
        }
        break;
    case 'L':
        if (kernel::config().version() == nw::GameVersion::vEE) {
            if (tk.loc.view() == "LOCATION_INVALID") {
                return NssTokenType::LOCATION_INVALID;
            }
        }
        break;
    case 'o':
        if (tk.loc.view() == "object") {
            return NssTokenType::OBJECT;
        }
        break;
    case 'O':
        if (tk.loc.view() == "OBJECT_INVALID") {
            return NssTokenType::OBJECT_INVALID_CONST;
        } else if (tk.loc.view() == "OBJECT_SELF") {
            return NssTokenType::OBJECT_SELF_CONST;
        }
        break;
    case 'r':
        if (tk.loc.view() == "return") {
            return NssTokenType::RETURN;
        }
        break;
    case 's':
        if (tk.loc.view() == "string") {
            return NssTokenType::STRING;
        } else if (tk.loc.view() == "struct") {
            return NssTokenType::STRUCT;
        } else if (tk.loc.view() == "switch") {
            return NssTokenType::SWITCH;
        } else if (kernel::config().version() == nw::GameVersion::vEE
            && tk.loc.view() == "sqlquery") {
            return NssTokenType::SQLQUERY;
        }
        break;
    case 't':
        if (tk.loc.view() == "talent") {
            return NssTokenType::TALENT;
        }
        break;
    case 'v':
        if (tk.loc.view() == "vector") {
            return NssTokenType::VECTOR;
        } else if (tk.loc.view() == "void") {
            return NssTokenType::VOID_;
        }
        break;
    case 'w':
        if (tk.loc.view() == "while") {
            return NssTokenType::WHILE;
        }
        break;
    }
    return NssTokenType::IDENTIFIER;
}

NssToken NssLexer::handle_identifier()
{
    size_t start = pos_;
    SourcePosition start_pos{line_, start - last_line_pos_};

    while (pos_ < buffer_.size()) {
        auto cur = get(pos_);
        if (char_map[cur] != CharType::alpha
            && char_map[cur] != CharType::digit
            && cur != '_') {
            break;
        }
        ++pos_;
    }
    auto t = NssToken{
        NssTokenType::IDENTIFIER,
        {buffer_.data() + start, pos_ - start},
        start_pos,
        SourcePosition{line_, pos_ - last_line_pos_},
    };
    t.type = check_keyword(t);
    return t;
}

NssToken NssLexer::handle_number()
{
    size_t start = pos_;
    SourcePosition start_pos{line_, start - last_line_pos_};

    bool is_float = false;
    int base = 10;
    if (get(pos_) == '0') {
        switch (get(pos_ + 1)) {
        case 'x':
        case 'X':
            base = 16;
            break;
        case 'b':
        case 'B':
            base = 2;
            break;
        case 'o':
        case 'O':
            base = 8;
            break;
        }
    }

    if (base != 10) { pos_ += 2; }

    while (pos_ < buffer_.size()) {
        auto cur = get(pos_);
        if (base == 16) {
            if (char_map[cur] == CharType::digit
                || (cur >= 'a' && cur <= 'f')
                || (cur >= 'A' && cur <= 'F')) {
                pos_++;
            } else {
                break;
            }
        } else if (base == 2) {
            if (cur == '0' || cur == '1') {
                pos_++;
            } else {
                break;
            }
        } else if (base == 8) {
            if (cur >= '0' || cur <= '7') {
                pos_++;
            } else {
                break;
            }
        } else {
            if (cur == '.') {
                ++pos_;
                is_float = true;
            } else if (char_map[cur] != CharType::digit) {
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
    return NssToken{
        type,
        {buffer_.data() + start, pos_ - start},
        start_pos,
        SourcePosition{line_, pos_ - last_line_pos_},
    };
}

NssToken NssLexer::next()
{
#define NSS_TOKEN(type, size)                                           \
    do {                                                                \
        t = NssToken{NssTokenType::type, {data(), size}};               \
        t.loc.range.start = start_pos;                                  \
        pos_ += size;                                                   \
        t.loc.range.end = SourcePosition{line_, pos_ - last_line_pos_}; \
    } while (0)

    while (pos_ < buffer_.size()) {
        size_t start = pos_;
        SourcePosition start_pos{line_, start - last_line_pos_};
        NssToken t;

        switch (get(pos_)) {
        default:
            if (char_map[get(pos_)] == CharType::alpha || get(pos_) == '_') {
                t = handle_identifier();
            } else if (char_map[get(pos_)] == CharType::digit) {
                t = handle_number();
            } else {
                SourceLocation loc;
                ctx_->lexical_diagnostic(parent_, fmt::format("Unrecognized character '{}'", get(pos_)),
                    true,
                    {start_pos, SourcePosition{line_, pos_ - last_line_pos_}});
                ++pos_;
                continue;
            }
            break;

        // Whitespace
        case ' ': {
            // Most? Use 4 spaces while coding nwscript?
            while (pos_ + 4 < buffer_.size()) {
                uint32_t temp;
                memcpy(&temp, buffer_.data() + pos_, 4);
                if (temp != 0x20202020) { break; }
                pos_ += 4;
            }
            while (get(pos_) == ' ') {
                ++pos_;
            }
            continue;
        }
        case '\t':
            while (get(pos_) == '\t') {
                ++pos_;
            }
            continue;
        case '\r':
            ++line_;
            ++pos_;
            if (get(pos_) == '\n') ++pos_;
            last_line_pos_ = pos_;
            line_map.push_back(last_line_pos_);
            continue;
        case '\n':
            ++line_;
            ++pos_;
            last_line_pos_ = pos_;
            line_map.push_back(last_line_pos_);
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
        case 'h':
        case 'H':
            if (kernel::config().version() == GameVersion::vEE
                && get(pos_ + 1) == '"') {

                bool matched = false;
                size_t original_last_line = last_line_pos_;
                size_t start_line = line_;
                start = pos_ + 2;
                pos_ += 2;
                while (pos_ < buffer_.size()) {
                    auto scan = scan_to_target(buffer_.data() + pos_, '"', buffer_.size() - pos_, true);
                    if (!scan) { break; }
                    pos_ = scan - buffer_.data();
                    if (get(pos_ - 1) == '\\' && get(pos_) == '"') {
                        ++pos_; // Skip escape, then skip " below.
                    } else if (pos_ != start && get(pos_) == '"') {
                        t = NssToken{
                            NssTokenType::STRING_HASH_LITERAL,
                            {buffer_.data() + start, pos_ - 1 - start},
                            start_pos,
                            SourcePosition{line_, pos_ - last_line_pos_},
                        };
                        ++pos_;
                        matched = true;
                        break;
                    }
                    ++pos_;
                }
                if (!matched) {
                    SourceLocation loc;
                    loc.start = &buffer_[start - 2];
                    loc.end = buffer_.data() + start;
                    loc.range.start = start_pos;
                    loc.range.end = SourcePosition{start_pos.line, start_pos.column + 2};
                    throw lexical_error("Unterminated raw string", loc);
                }
            } else {
                t = handle_identifier();
            }
            break;
        case 'r':
        case 'R': {
            if (kernel::config().version() == GameVersion::vEE
                && get(pos_ + 1) == '"') {

                bool matched = false;
                size_t start_line = line_;
                start = pos_ + 2;
                pos_ += 2;
                while (pos_ < buffer_.size()) {
                    auto scan = scan_to_target(buffer_.data() + pos_, '"', buffer_.size() - pos_, false);
                    if (!scan) { break; }
                    pos_ = scan - buffer_.data();
                    if (get(pos_) == '"') {
                        if (get(pos_ - 1) == '\\') {
                            ++pos_;
                            continue;
                        } else if (get(pos_ + 1) == '"') {
                            pos_ += 2;
                            continue;
                        }
                    }
                    if (pos_ != start && get(pos_) == '"') {
                        t = NssToken{
                            NssTokenType::STRING_RAW_CONST,
                            {buffer_.data() + start, pos_ - 1 - start},
                            start_pos,
                            SourcePosition{line_, pos_ - last_line_pos_},
                        };
                        ++pos_;
                        matched = true;
                        break;
                    }
                    ++pos_;
                }
                if (!matched) {
                    SourceLocation loc;
                    loc.start = &buffer_[start - 2];
                    loc.end = buffer_.data() + start;
                    loc.range.start = start_pos;
                    loc.range.end = SourcePosition{start_pos.line, start_pos.column + 2};
                    throw lexical_error("Unterminated raw string", loc);
                }
            } else {
                t = handle_identifier();
            }
        } break;
        case '"': {
            start = ++pos_;
            auto scan = scan_to_target(buffer_.data() + pos_, '"', buffer_.size() - pos_, true);
            pos_ = scan ? scan - buffer_.data() : buffer_.size();

            if (pos_ == buffer_.size() || get(pos_) != '"') {
                SourceLocation loc;
                loc.start = &buffer_[start - 1];
                loc.end = buffer_.data() + start;
                loc.range.start = start_pos;
                loc.range.end = SourcePosition{start_pos.line, start_pos.column + 1};
                throw lexical_error("Unterminated string", loc);
            }

            t = NssToken{NssTokenType::STRING_CONST,
                {&buffer_[start], pos_ - start},
                start_pos,
                SourcePosition{line_, pos_ + 1 - last_line_pos_}};

            ++pos_;
        } break;

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
                case '>':
                    switch (get(pos_ + 3)) {
                    case '=':
                        NSS_TOKEN(USREQ, 4);
                        break;
                    default:
                        NSS_TOKEN(USR, 3);
                        break;
                    }
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
            case '/': { // Uh oh
                SourceLocation loc{
                    buffer_.data() + start - 1,
                    buffer_.data() + pos_ + 1,
                    {start_pos, SourcePosition{line_, pos_ + 1 - last_line_pos_}},
                };
                throw lexical_error("Mismatched block quote", loc);
            } break;
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
                ctx_->lexical_diagnostic(parent_, fmt::format("Unrecognized character '{}'", get(pos_)),
                    true,
                    {SourcePosition{line_, start - last_line_pos_},
                        SourcePosition{line_, start - last_line_pos_ + 1}});
            }
            ++pos_;
            continue;
        case '/': // DIV
            switch (get(pos_ + 1)) {
            case '/': { // Comment
                pos_ += 2;
                start = pos_;
                auto test = scan_to_endl(buffer_.data() + pos_, buffer_.size() - pos_);
                if (!test) {
                    pos_ = buffer_.size();
                } else {
                    pos_ = test - buffer_.data();
                }
                t = NssToken{
                    NssTokenType::COMMENT,
                    {buffer_.data() + start, pos_ - start},
                    start_pos,
                    SourcePosition{line_, pos_ - last_line_pos_},
                };
            } break;
            case '*': { // Block Comment
                bool matched = false;
                size_t original_last_line = last_line_pos_;
                size_t start_line = line_;
                start = pos_ + 2;
                pos_ += 2;
                while (pos_ < buffer_.size()) {
                    if (get(pos_) == '\n') {
                        ++line_;
                        last_line_pos_ = pos_;
                        line_map.push_back(last_line_pos_);
                    } else if (pos_ != start && get(pos_ - 1) == '*' && get(pos_) == '/') {
                        t = NssToken{
                            NssTokenType::COMMENT,
                            {buffer_.data() + start, pos_ - 1 - start},
                            start_pos,
                            SourcePosition{line_, pos_ - last_line_pos_},
                        };
                        ++pos_;
                        matched = true;
                        break;
                    }
                    ++pos_;
                }
                if (!matched) {
                    SourceLocation loc;
                    loc.start = &buffer_[start - 2];
                    loc.end = buffer_.data() + start;
                    loc.range.start = start_pos;
                    loc.range.end = SourcePosition{start_pos.line, start_pos.column + 2};
                    throw lexical_error("Unterminated block quote", loc);
                }
            } break;
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
            return current_ = NssToken{NssTokenType::END, {}, {}, {}};
        }
    }
    return current_ = NssToken{
               NssTokenType::END,
               {},
               SourcePosition{line_, pos_ - last_line_pos_},
               SourcePosition{line_, pos_ - last_line_pos_},
           };
}

const char* NssLexer::scan_to_target(const char* data, char target, size_t size, bool no_eol)
{
    using batch_type = xsimd::batch<int8_t>;
    const size_t batch_size = batch_type::size;

    batch_type target_batch = batch_type::broadcast(static_cast<int8_t>(target));
    batch_type eol_batch = batch_type::broadcast(static_cast<int8_t>(target));

    size_t i = 0;
    for (; i + batch_size <= size; i += batch_size) {
        batch_type chunk = xsimd::load_unaligned(reinterpret_cast<const int8_t*>(data + i));
        auto matches = (chunk == target_batch);
        auto newlines = (chunk == eol_batch);

        if (xsimd::any(matches)) {
            for (size_t j = 0; j < batch_size; ++j) {
                if (newlines.get(j)) {
                    if (no_eol) {
                        return data + i + j;
                    } else {
                        ++line_;
                        last_line_pos_ = pos_;
                        line_map.push_back(last_line_pos_);
                    }
                } else if (matches.get(j)) {
                    return data + i + j;
                }
            }
        }
    }

    for (; i < size; ++i) {
        if (data[i] == '\n') {
            if (no_eol) {
                return data + i;
            } else {
                ++line_;
                last_line_pos_ = pos_;
                line_map.push_back(last_line_pos_);
            }
        } else if (data[i] == target) {
            return data + i;
        }
    }

    return nullptr;
}

const char* NssLexer::scan_to_endl(const char* data, size_t size)
{
    using batch_type = xsimd::batch<int8_t>;
    const size_t batch_size = batch_type::size;

    batch_type target_batch = batch_type::broadcast(static_cast<int8_t>('\n'));

    size_t i = 0;
    for (; i + batch_size <= size; i += batch_size) {
        batch_type chunk = xsimd::load_unaligned(reinterpret_cast<const int8_t*>(data + i));
        auto matches = (chunk == target_batch);

        if (xsimd::any(matches)) {
            for (size_t j = 0; j < batch_size; ++j) {
                if (matches.get(j)) { return data + i + j; }
            }
        }
    }

    for (; i < size; ++i) {
        if (data[i] == '\n') { return data + i; }
    }

    return nullptr;
}

} // namespace nw::script
