#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace nw::smalls {
struct Diagnostic;
struct ExecutionResult;
}

namespace nw::toolset {

std::string format_smalls_diagnostic(const nw::smalls::Diagnostic& diagnostic,
    std::string_view source_path = {}, size_t first_source_line = 1);
std::string format_smalls_execution_error(const nw::smalls::ExecutionResult& result,
    std::string_view source_path = {}, size_t first_source_line = 1);

} // namespace nw::toolset
