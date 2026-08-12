#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nw/objects/ObjectHandle.hpp>

namespace nw::toolset {
struct UiListEvent;
}

namespace nw::toolset {

struct SmallsInvocationResult {
    bool ok = false;
    std::string message;
};

class RmlSmallsBridge {
public:
    bool initialize();
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    void publish_active_object(nw::ObjectHandle object);
    void clear_active_object();
    void clear_active_object(nw::ObjectHandle object);
    [[nodiscard]] nw::ObjectHandle active_object() const noexcept;

    void publish_active_area(nw::ObjectHandle area);
    void clear_active_area() noexcept;
    [[nodiscard]] nw::ObjectHandle active_area() const noexcept;

    void register_event_handler(std::string event_key, std::string function_name);
    std::string dispatch_event(std::string_view event_key, const std::vector<std::string_view>& args);
    std::string call(std::string_view module_path, std::string_view function_name, const std::vector<std::string_view>& args);
    SmallsInvocationResult invoke(
        std::string_view module_path, std::string_view function_name, const std::vector<std::string_view>& args);
    SmallsInvocationResult call_ui_list_callback(
        std::string_view qualified_function, const UiListEvent& event);
    void refresh_ui_lists();

private:
    bool ensure_current_runtime();
    SmallsInvocationResult execute_handler(
        std::string_view module_path, std::string_view function_name, const std::vector<std::string_view>& args);

    bool initialized_ = false;
    uint64_t runtime_generation_ = 0;
    nw::ObjectHandle active_area_{};
    std::string module_path_ = "toolset.ui";
    std::unordered_map<std::string, std::string> event_handlers_;
};

} // namespace nw::toolset
