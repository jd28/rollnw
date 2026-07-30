#pragma once

#define ROLLNW_STRINGIFY_INTERAL(a) #a
#define ROLLNW_STRINGIFY(a) ROLLNW_STRINGIFY_INTERAL(a)

/// Silences unused variable warnings
#define ROLLNW_UNUSED(thing) (void)thing

// The format string is part of __VA_ARGS__ rather than a named parameter, so
// there is never a trailing comma to suppress. `, ##__VA_ARGS__` is a GNU
// extension clang warns about, and `__VA_OPT__` needs /Zc:preprocessor on MSVC;
// this needs neither. A message is still required, as it was before.

#define ENSURE_OR_RETURN(cond, ...)      \
    do {                                 \
        if (!(cond)) {                   \
            LOG_F(ERROR, __VA_ARGS__);   \
            return;                      \
        }                                \
    } while (0)

#define ENSURE_OR_RETURN_ZERO(cond, ...) \
    do {                                 \
        if (!(cond)) {                   \
            LOG_F(ERROR, __VA_ARGS__);   \
            return 0;                    \
        }                                \
    } while (0)

#define ENSURE_OR_RETURN_NULLPTR(cond, ...) \
    do {                                    \
        if (!(cond)) {                      \
            LOG_F(ERROR, __VA_ARGS__);      \
            return 0;                       \
        }                                   \
    } while (0)

#define ENSURE_OR_RETURN_FALSE(cond, ...) \
    do {                                  \
        if (!(cond)) {                    \
            LOG_F(ERROR, __VA_ARGS__);    \
            return false;                 \
        }                                 \
    } while (0)

#define ENSURE_OR_RETURN_DEFAULT(cond, ...) \
    do {                                    \
        if (!(cond)) {                      \
            LOG_F(ERROR, __VA_ARGS__);      \
            return {};                      \
        }                                   \
    } while (0)

#define ENSURE_OR_RETURN_VALUE(value, cond, ...) \
    do {                                         \
        if (!(cond)) {                           \
            LOG_F(ERROR, __VA_ARGS__);           \
            return (value);                      \
        }                                        \
    } while (0)
