#pragma once

namespace nw {

// Remove if ever added to standard.  This is basically from boost.

template <typename T>
class scope_exit {
public:
    explicit scope_exit(T&& exit)
        : exit_(std::forward<T>(exit))
    {
    }

    scope_exit(const scope_exit&) = delete;

    ~scope_exit()
    {
        try {
            exit_();
        } catch (...) {
        }
    }

    scope_exit& operator=(const scope_exit&) = delete;

private:
    T exit_;
};

template <typename T>
scope_exit<T> create_scope_exit(T&& exit)
{
    return scope_exit<T>(std::forward<T>(exit));
}

} // namespace nw

/// @private
#define _EXIT_SCOPE_LINENAME_CAT(name, line) name##line
/// @private
#define _EXIT_SCOPE_LINENAME(name, line) _EXIT_SCOPE_LINENAME_CAT(name, line)

// clang-format off

/// Creates scope exit.
#define SCOPE_EXIT(f)                                                        \
    const auto& _EXIT_SCOPE_LINENAME(EXIT, __LINE__) = create_scope_exit(f); (void)_EXIT_SCOPE_LINENAME(EXIT, __LINE__)

// clang-format on
