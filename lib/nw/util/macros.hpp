#pragma once

#define LIBNW_STRINGIFY_INTERAL(a) #a
#define LIBNW_STRINGIFY(a) LIBNW_STRINGIFY_INTERAL(a)

/// Silences unused variable warnings
#define LIBNW_UNUSED(thing) (void)thing
