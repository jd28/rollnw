#include "preview_plt.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/error_context.hpp>
#include <nw/util/string.hpp>

#include <filesystem>

namespace nw::render::viewer {
namespace {

std::unique_ptr<ModelInstance> load_local_model(PreviewRenderResources& resources, const std::filesystem::path& path,
    std::string_view root_name)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading local MDL '{}'", std::string_view{path_text});

    auto owned_mdl = std::make_unique<nw::model::Mdl>(path);
    if (!owned_mdl || !owned_mdl->valid()) {
        return nullptr;
    }

    ModelLoader loader(resources.context());
    auto model = loader.load(owned_mdl.get(), root_name);
    if (!model) {
        return nullptr;
    }
    model->owned_mdl_ = std::move(owned_mdl);
    return model;
}

} // namespace

std::string preferred_plt_bitmap(std::string_view model_resref, std::string_view bitmap_name)
{
    auto& resman = nw::kernel::resman();
    const auto has_plt = [&](std::string_view name) {
        return !name.empty() && resman.contains({nw::Resref{name}, nw::ResourceType::plt});
    };

    if (has_plt(model_resref)) {
        return std::string(model_resref);
    }

    // Humanoid part models often use g/o/d variants while the PLT stays on the h variant.
    if (model_resref.size() >= 4 && model_resref[0] == 'p'
        && (model_resref[1] == 'm' || model_resref[1] == 'f')
        && model_resref[2] != 'h') {
        auto candidate = std::string(model_resref);
        candidate[2] = 'h';
        if (has_plt(candidate)) {
            return candidate;
        }
    }

    return std::string(bitmap_name);
}

void apply_plt_to_model(PreviewRenderResources& resources, ModelInstance& model, const nw::PltColors& colors,
    std::string_view model_resref)
{
    for (auto& node : model.nodes_) {
        auto* mesh = dynamic_cast<Mesh*>(node.get());
        if (!mesh || mesh->bitmap_name.empty()) {
            continue;
        }
        const auto bitmap_name = preferred_plt_bitmap(model_resref, mesh->bitmap_name);
        if (!nw::kernel::resman().contains({nw::Resref{bitmap_name}, nw::ResourceType::plt})) {
            continue;
        }
        mesh->bitmap_name = bitmap_name;
        mesh->plt_colors = colors;
        mesh->uses_plt = true;
        mesh->texture = resources.get_or_load_raw_plt_texture(mesh->bitmap_name);
        if (mesh->texture.valid()) {
            mesh->texture_index = nw::gfx::get_bindless_texture_index(resources.context(), mesh->texture);
        } else {
            mesh->uses_plt = false;
            mesh->texture = resources.get_or_load_texture(mesh->bitmap_name, colors, mesh->material_mode == MaterialMode::transparent);
            mesh->texture_index = nw::gfx::get_bindless_texture_index(resources.context(), mesh->texture);
        }
    }
}

std::unique_ptr<ModelInstance> load_model_with_plt(PreviewRenderResources& resources, std::string_view resref,
    const nw::PltColors* colors, std::string_view root_name)
{
    ERRARE("[viewer] loading model '{}'", resref);

    auto source_path = std::filesystem::path{resref};
    nw::String ext = source_path.extension().string();
    nw::string::tolower(&ext);

    std::string model_resref{resref};
    std::unique_ptr<ModelInstance> model;
    if (ext == ".mdl" && std::filesystem::exists(source_path)) {
        model_resref = source_path.stem().string();
        model = load_local_model(resources, source_path, root_name);
    } else {
        ModelLoader loader(resources.context());
        model = loader.load(resref, root_name);
    }
    if (model && colors) {
        apply_plt_to_model(resources, *model, *colors, model_resref);
    }
    return model;
}

} // namespace nw::render::viewer
