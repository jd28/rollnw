#include "server.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace {

/// True when the client has already sent more input.
///
/// `std::cin`'s own buffer answers only for data the stream has already
/// consumed from the OS, which for a pipe is usually nothing, so the descriptor
/// is polled as well. Both are needed: the buffer catches a partially consumed
/// frame, the poll catches bytes still in the pipe.
bool stdin_has_pending_input()
{
    if (std::cin.rdbuf() && std::cin.rdbuf()->in_avail() > 0) {
        return true;
    }

#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD available = 0;
    if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
        return available > 0;
    }
    return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
#else
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    return ::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0;
#endif
}

} // namespace

namespace lsp = nw::smalls;

int main(int argc, char* argv[])
{
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
    nw::init_logger(argc, argv);

    nw::kernel::services().create(nw::kernel::ServiceMode::language);

    auto& rt = nw::kernel::runtime();
    rt.set_diagnostic_config({lsp::DebugLevel::full});
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-I" || arg == "--module-path") && i + 1 < argc) {
            rt.add_module_path(argv[++i]);
        }
    }
    nw::kernel::services().start(nw::kernel::ServiceMode::language);

    run_smalls_lsp(std::cin, std::cout, stdin_has_pending_input);

    nw::kernel::services().shutdown();
    return 0;
}
