#pragma once

#include <functional>
#include <iosfwd>

/// Reports whether more input is already available to read.
///
/// The server uses this to coalesce a burst of edits: when a `didChange` is
/// followed by more pending input, analysis is deferred until the burst drains,
/// so a fast typist gets one analysis pass rather than one per keystroke.
/// Returning false always is correct and simply analyzes eagerly.
using InputPendingFn = std::function<bool()>;

/// Runs the Smalls language server until the input stream reaches EOF.
///
/// The caller owns both streams and the initialized kernel/runtime services.
/// When `input_pending` is empty the stream's own buffer is consulted, which is
/// accurate for an in-memory stream but not for a pipe; a caller reading from a
/// pipe should supply a predicate that polls the descriptor.
void run_smalls_lsp(std::istream& input, std::ostream& output,
    InputPendingFn input_pending = {});
