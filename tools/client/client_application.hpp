#pragma once

namespace nw::toolset {
// Desktop singleton. Returns the existing startup/frame exit status; all SDL,
// kernel, renderer and Rml ownership lives for this call in declaration order.
int run_client_application(const char* executable);
}
