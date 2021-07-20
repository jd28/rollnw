#pragma once

#define LOGURU_USE_FMTLIB 1
#define LOGURU_REDEFINE_ASSERT 1
#include "loguru/loguru.hpp"

namespace nw {

/// Initialize logger
void init_logger(int argc, char* argv[]);

} // namespace nw
