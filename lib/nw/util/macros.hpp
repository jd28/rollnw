#pragma once

#define ROLLNW_STRINGIFY_INTERAL(a) #a
#define ROLLNW_STRINGIFY(a) ROLLNW_STRINGIFY_INTERAL(a)

/// Silences unused variable warnings
#define ROLLNW_UNUSED(thing) (void)thing
