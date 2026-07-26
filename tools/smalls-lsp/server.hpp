#pragma once

#include <iosfwd>

/// Runs the Smalls language server until the input stream reaches EOF.
///
/// The caller owns both streams and the initialized kernel/runtime services.
void run_smalls_lsp(std::istream& input, std::ostream& output);
