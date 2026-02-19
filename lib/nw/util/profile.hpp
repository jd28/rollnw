#pragma once

#if defined(ROLLNW_ENABLE_TRACY)

#include <tracy/Tracy.hpp>

#include <cstring>

#define NW_PROFILE_SCOPE() ZoneScoped
#define NW_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define NW_PROFILE_MSG(msg) TracyMessageL(msg)
#define NW_PROFILE_VALUE(value) ZoneValue(value)
#define NW_PROFILE_TEXT(text, size) ZoneText(text, size)

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
