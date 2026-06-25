#pragma once

#include "preview_render_resources.hpp"

#include <nw/formats/Plt.hpp>
#include <nw/objects/Appearance.hpp>
#include <nw/render/nwn/model_loader.hpp>

#include <memory>
#include <string_view>

namespace nw::render::viewer {

using nw::render::nwn::MaterialMode;
using nw::render::nwn::Mesh;
using nw::render::nwn::ModelInstance;
using nw::render::nwn::ModelLoader;

nw::PltColors creature_plt_colors(const nw::CreatureAppearance& appearance);
std::string preferred_plt_bitmap(std::string_view model_resref, std::string_view bitmap_name = {});
void apply_plt_to_model(PreviewRenderResources& resources, ModelInstance& model, const nw::PltColors& colors,
    std::string_view model_resref = {});
std::unique_ptr<ModelInstance> load_model_with_plt(PreviewRenderResources& resources, std::string_view resref,
    const nw::PltColors* colors = nullptr, std::string_view root_name = {});

} // namespace nw::render::viewer
