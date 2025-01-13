#pragma once

#include "../log.hpp"

#include <string>
#include <tuple>

#define ERRARE_CONCAT_(x, y) x##y
#define ERRARE_CONCAT(x, y) ERRARE_CONCAT_(x, y)

#define ERRARE_UNIQUE_NAME ERRARE_CONCAT(errare, __COUNTER__)

namespace nw {

struct ErrorContextBase;

extern thread_local ErrorContextBase* error_context_stack;

struct ErrorContextBase {
    ErrorContextBase(const char* file_, int line_);
    virtual ~ErrorContextBase();

    virtual std::string to_string() const = 0;

    const char* file;
    int line = 0;
    ErrorContextBase* prev = nullptr;
    ErrorContextBase* next = nullptr;
};

template <typename... Args>
struct ErrorContext : public ErrorContextBase {
    using tuple_type = std::tuple<std::string_view, Args...>;

    ErrorContext(const char* file_, int line_, std::string_view str, const Args&... args)
        : ErrorContextBase{file_, line_}
        , args_{str, args...}
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

std::string get_error_context();

#define ERRARE(fmt, ...) \
    auto ERRARE_UNIQUE_NAME { nw::ErrorContext(__FILE__, __LINE__, fmt, ##__VA_ARGS__) }

} // namespace nw
