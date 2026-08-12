#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Rml {
class Context;
}

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

struct RmlSmallsGlobalBinding {
    std::string variable;
    std::string module;
    std::string global;
};

class RmlSmallsDataModel {
public:
    RmlSmallsDataModel();
    ~RmlSmallsDataModel();

    RmlSmallsDataModel(const RmlSmallsDataModel&) = delete;
    RmlSmallsDataModel& operator=(const RmlSmallsDataModel&) = delete;

    bool initialize(Rml::Context& context,
        nw::smalls::Runtime& runtime,
        std::string_view model_name,
        std::span<const RmlSmallsGlobalBinding> bindings);
    bool synchronize(nw::smalls::Runtime& runtime);
    void dirty_all();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nw::toolset
