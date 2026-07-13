#pragma once

#include "Bytecode.hpp"

namespace nw::smalls {

struct Runtime;

// Returns false and fills error_message on invalid bytecode.
bool verify_bytecode_module(const BytecodeModule* module, String* error_message);
bool verify_bytecode_module(const BytecodeModule* module, const Runtime* runtime, String* error_message);

} // namespace nw::smalls
