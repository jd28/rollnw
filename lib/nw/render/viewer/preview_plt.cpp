#include "preview_plt.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/resources/ResourceManager.hpp>

namespace nw::render::viewer {
namespace {

bool has_plt(std::string_view name)
{
    return !name.empty()
        && nw::kernel::resman().contains({nw::Resref{name}, nw::ResourceType::plt});
}

} // namespace

std::string preferred_plt_bitmap(
    std::string_view model_resref, std::string_view bitmap_name, std::string_view explicit_plt)
{
    if (has_plt(explicit_plt)) {
        return std::string(explicit_plt);
    }

    if (has_plt(model_resref)) {
        return std::string(model_resref);
    }

    auto resolved = nw::render::nwn::resolve_nwn_model_albedo_resref(model_resref, bitmap_name);
    if (has_plt(resolved)) {
        return resolved;
    }

    return std::string(bitmap_name);
}

} // namespace nw::render::viewer
