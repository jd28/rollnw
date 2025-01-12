#pragma once

#include "../log.hpp"
#include "templates.hpp"

#include <sstream>
#include <string_view>
#include <tuple>
#include <vector>

#define ERRARE_CONCAT_(x, y) x##y
#define ERRARE_CONCAT(x, y) ERRARE_CONCAT_(x, y)

#define ERRARE_UNIQUE_NAME ERRARE_CONCAT(errare, __COUNTER__)

namespace nw {

struct ErrorContextBase;

static thread_local ErrorContextBase* error_context_stack = nullptr;

struct ErrorContextBase {
    ErrorContextBase(const char* file_, int line_)
        : file{file_}
        , line{line_}
    {
        prev = error_context_stack;
        if (error_context_stack) {
            error_context_stack->next = this;
        }
        error_context_stack = this;
    }

    virtual ~ErrorContextBase()
    {
        error_context_stack = prev;
    }

    virtual std::string to_string() const = 0;

    const char* file;
    int line = 0;
    ErrorContextBase* prev = nullptr;
    ErrorContextBase* next = nullptr;
};

template <typename... Args>
struct ErrorContext : public ErrorContextBase {
    using tuple_type = std::tuple<std::string_view, Args...>;

    ErrorContext(const char* file_, int line_, std::string_view str, Args&&... args)
        : ErrorContextBase{file_, line_}
        , args_{str, std::forward<Args>(args)...}
    {
        static_assert((std::is_trivially_copyable_v<Args> && ...),
            "ErrorContext requires all Args to be trivially constructible.");
    }

    virtual std::string to_string() const override
    {
        return std::apply([](const auto& str, const auto&... args) -> std::string {
            return fmt::format(fmt::runtime(str), args...);
        },
            args_);
    }

    tuple_type args_;
};

inline std::string get_error_context()
{
    ErrorContextBase* current = error_context_stack;
    if (!current) { return {}; }

    std::vector<std::string> result;
    result.reserve(100);
    while (current) {
        std::string_view fullpath = current->file;
        size_t pos = fullpath.find_last_of("/\\");
        std::string_view basefile = (pos == std::string::npos) ? fullpath : fullpath.substr(pos + 1);
        result.push_back(fmt::format("[error context] {}:{:<6} {}", basefile, current->line, current->to_string()));
        current = current->prev;
    }

    std::ostringstream ss;
    for (const auto& it : reverse(result)) {
        ss << it << '\n';
    }
    return ss.str();
}

#define ERRARE(fmt, ...) \
    auto ERRARE_UNIQUE_NAME { nw::ErrorContext(__FILE__, __LINE__, fmt, __VA_ARGS__) }

} // namespace nw
