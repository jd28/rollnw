#pragma once

#include "../config.hpp"

namespace nw {

struct TwoDATokenizer {
    explicit TwoDATokenizer(StringView tda)
        : buffer{tda}
    {
    }

    StringView next()
    {
        StringView result;
        size_t start, end;
        while (pos < buffer.size()) {
            switch (buffer[pos]) {
            default:
                start = pos++;
                while (pos < buffer.size()) {
                    if (buffer[pos] == ' '
                        || buffer[pos] == '\t'
                        || buffer[pos] == '\r'
                        || buffer[pos] == '\n') {
                        end = pos;
                        break;
                    }
                    ++pos;
                }
                if (pos == buffer.size()) end = buffer.size();
                result = StringView(&buffer[start], end - start);
                break;
            case '"':
                start = ++pos;
                while (pos < buffer.size()) {
                    if (buffer[pos] == '"' && buffer[pos - 1] != '\\') {
                        end = pos;
                        break;
                    }
                    ++pos;
                }
                if (pos == buffer.size() || buffer[pos] != '"')
                    throw std::runtime_error("Unterminated quote.");
                ++pos;

                result = StringView(&buffer[start], end - start);
                break;
            case '\r':
            case '\n':
                start = pos;
                if (buffer[pos] == '\r') ++pos;
                if (pos < buffer.size() && buffer[pos] == '\n') ++pos;
                result = StringView(&buffer[start], pos - start);
                ++line;
                break;
            case ' ':
            case '\t':
                while (buffer[pos] == ' ' || buffer[pos] == '\t') {
                    ++pos;
                }
                break;
            }
            if (result.size()) {
                break;
            }
        }
        return result;
    }

    size_t pos = 0, line = 0;
    StringView buffer;
};

inline bool is_newline(StringView token)
{
    return !token.empty() && (token[0] == '\r' || token[0] == '\n');
}

}
