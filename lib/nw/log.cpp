#define LOGURU_USE_FMTLIB 1
#include "loguru/loguru.cpp"

namespace nw {

void init_logger(int argc, char* argv[])
{
    loguru::init(argc, argv);
}

} // namespace nw
