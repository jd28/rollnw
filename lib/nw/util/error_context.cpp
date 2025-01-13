#include "error_context.hpp"

#include "string.hpp"

#include <algorithm>

namespace nw {

thread_local ErrorContextBase* error_context_stack = nullptr;

ErrorContextBase::ErrorContextBase(const char* file_, int line_)
    : file{file_}
    , line{line_}
{
    prev = error_context_stack;
    if (error_context_stack) {
        error_context_stack->next = this;
    }
    error_context_stack = this;
}

ErrorContextBase::~ErrorContextBase()
{
    error_context_stack = prev;
}

std::string get_error_context()
{
    ErrorContextBase* current = error_context_stack;
    if (!current) {
        LOG_F(ERROR, "Error context is empty.");
        return {};
    }

    std::vector<std::string> result;
    result.reserve(100);
    while (current) {
        std::string_view fullpath = current->file;
        size_t pos = fullpath.find_last_of("/\\");
        std::string_view basefile = (pos == std::string::npos) ? fullpath : fullpath.substr(pos + 1);
        result.push_back(fmt::format("[ec] {:>23}:{:<6} {}", basefile, current->line, current->to_string()));
        current = current->prev;
    }

    std::reverse(result.begin(), result.end());
    return string::join(result, "\n");
}

} // namespace nw
