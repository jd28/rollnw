#pragma once

#if defined(ROLLNW_ENABLE_TRACY)

#include <tracy/Tracy.hpp>

#include <concepts>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace nw::profile {

template <std::integral T>
inline void plot(const char* name, T value)
{
    static_assert(std::is_signed_v<T> || sizeof(T) < sizeof(int64_t),
        "Tracy plots cannot represent uint64_t values");
    TracyPlot(name, static_cast<int64_t>(value));
}

template <std::floating_point T>
inline void plot(const char* name, T value)
{
    TracyPlot(name, static_cast<double>(value));
}

}

#define NW_PROFILE_SCOPE() ZoneScoped
#define NW_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define NW_PROFILE_MSG(msg) TracyMessageL(msg)
#define NW_PROFILE_VALUE(value) ZoneValue(value)
#define NW_PROFILE_TEXT(text, size) ZoneText(text, size)
#define NW_PROFILE_PLOT(name, value) ::nw::profile::plot(name, value)

#define NW_PROFILE_TEXT_CSTR(cstr)                     \
    do {                                               \
        const char* _nw_text = (cstr);                 \
        if (_nw_text) {                                \
            ZoneText(_nw_text, std::strlen(_nw_text)); \
        }                                              \
    } while (0)

#else

#define NW_PROFILE_SCOPE() \
    do {                   \
    } while (0)

#define NW_PROFILE_SCOPE_N(name) \
    do {                         \
        (void)(name);            \
    } while (0)

#define NW_PROFILE_MSG(msg) \
    do {                    \
        (void)(msg);        \
    } while (0)

#define NW_PROFILE_PLOT(name, value) \
    do {                             \
        (void)(name);                \
        (void)(value);               \
    } while (0)

#define NW_PROFILE_VALUE(value) \
    do {                        \
        (void)(value);          \
    } while (0)

#define NW_PROFILE_TEXT(text, size) \
    do {                            \
        (void)(text);               \
        (void)(size);               \
    } while (0)

#define NW_PROFILE_TEXT_CSTR(cstr) \
    do {                           \
        (void)(cstr);              \
    } while (0)

#endif
