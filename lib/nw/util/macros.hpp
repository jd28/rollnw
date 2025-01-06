#pragma once

#define ROLLNW_STRINGIFY_INTERAL(a) #a
#define ROLLNW_STRINGIFY(a) ROLLNW_STRINGIFY_INTERAL(a)

/// Silences unused variable warnings
#define ROLLNW_UNUSED(thing) (void)thing

#define ENSURE_OR_RETURN(cond, fmt, ...)      \
    do {                                      \
        if (!(cond)) {                        \
            LOG_F(ERROR, fmt, ##__VA_ARGS__); \
            return;                           \
        }                                     \
    } while (0)

#define ENSURE_OR_RETURN_ZERO(cond, fmt, ...) \
    do {                                      \
        if (!(cond)) {                        \
            LOG_F(ERROR, fmt, ##__VA_ARGS__); \
            return 0;                         \
        }                                     \
    } while (0)

#define ENSURE_OR_RETURN_FALSE(cond, fmt, ...) \
    do {                                       \
        if (!(cond)) {                         \
            LOG_F(ERROR, fmt, ##__VA_ARGS__);  \
            return false;                      \
        }                                      \
    } while (0)
