#pragma once

#include "Bytecode.hpp"

namespace nw::smalls {

// Returns false and fills error_message on invalid bytecode.
bool verify_bytecode_module(const BytecodeModule* module, String* error_message);

} // namespace nw::smalls
