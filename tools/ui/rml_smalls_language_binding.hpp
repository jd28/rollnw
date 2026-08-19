#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace Rml {
class ElementDocument;
}

namespace nw::toolset {

struct RmlSmallsDiagnostic {
    std::string message;
};

struct RmlSmallsBindingStats {
    uint64_t bound_listener_count = 0;
    uint64_t bound_argument_count = 0;
    uint64_t interned_target_count = 0;
    uint64_t suppressed_diagnostic_count = 0;
};

class RmlSmallsLanguageBinding {
public:
    RmlSmallsLanguageBinding();
    ~RmlSmallsLanguageBinding();

    RmlSmallsLanguageBinding(const RmlSmallsLanguageBinding&) = delete;
    RmlSmallsLanguageBinding& operator=(const RmlSmallsLanguageBinding&) = delete;

    bool initialize(nw::smalls::Runtime& runtime);
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] const std::vector<RmlSmallsDiagnostic>& diagnostics() const noexcept;
    [[nodiscard]] RmlSmallsBindingStats stats() const noexcept;
    void clear_diagnostics();

    // Dispatches one synthetic refresh event to each identified .smalls_refresh
    // element. Targets are re-resolved by ID because earlier handlers may replace
    // subtrees. Elements and commands resolve synchronously on the UI thread.
    void refresh_elements(Rml::ElementDocument* document);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nw::toolset
