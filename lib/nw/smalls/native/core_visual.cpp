#include "../runtime.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectHandle.hpp"
#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

namespace {

bool valid_object(nw::ObjectHandle obj) noexcept
{
    return nw::kernel::objects().valid(obj);
}

bool set_plt_color(nw::PltColors& colors, nw::PltLayer layer, int32_t value) noexcept
{
    if (value < 0 || value > 255) {
        return false;
    }
    colors.data[layer] = static_cast<uint8_t>(value);
    return true;
}

bool set_plt_colors(nw::PltColors& colors,
    int32_t skin,
    int32_t hair,
    int32_t metal1,
    int32_t metal2,
    int32_t cloth1,
    int32_t cloth2,
    int32_t leather1,
    int32_t leather2,
    int32_t tattoo1,
    int32_t tattoo2) noexcept
{
    return set_plt_color(colors, nw::plt_layer_skin, skin)
        && set_plt_color(colors, nw::plt_layer_hair, hair)
        && set_plt_color(colors, nw::plt_layer_metal1, metal1)
        && set_plt_color(colors, nw::plt_layer_metal2, metal2)
        && set_plt_color(colors, nw::plt_layer_cloth1, cloth1)
        && set_plt_color(colors, nw::plt_layer_cloth2, cloth2)
        && set_plt_color(colors, nw::plt_layer_leather1, leather1)
        && set_plt_color(colors, nw::plt_layer_leather2, leather2)
        && set_plt_color(colors, nw::plt_layer_tattoo1, tattoo1)
        && set_plt_color(colors, nw::plt_layer_tattoo2, tattoo2);
}

} // namespace

void register_core_visual(Runtime& rt)
{
    if (rt.get_native_module("core.visual")) {
        return;
    }

    rt.module("core.visual")
        .function("clear_visual", +[](nw::ObjectHandle obj, int32_t appearance) -> bool {
            if (!valid_object(obj)) { return false; }
            return nw::kernel::objects().components().clear_visual(obj, appearance); })
        .function("clear_visual_slot", +[](nw::ObjectHandle obj, int32_t slot) -> bool {
            if (!valid_object(obj)) { return false; }
            return nw::kernel::objects().components().clear_visual_slot(obj, slot); })
        .function("set_visual_body_variant", +[](nw::ObjectHandle obj, int32_t body_variant) -> bool {
            if (!valid_object(obj) || body_variant < 0) { return false; }
            return nw::kernel::objects().components().set_visual_body_variant(obj, body_variant); })
        .function("add_visual_model", +[](nw::ObjectHandle obj, nw::Resref model, nw::Resref attach_to, nw::Resref attach_from, int32_t kind, int32_t slot, int32_t part, int32_t flags) -> bool {
            if (!valid_object(obj) || flags < 0) { return false; }
            return nw::kernel::objects().components().add_visual_model(obj, nw::ObjectVisualModel{
                                                                                .model = model,
                                                                                .attach_to = attach_to,
                                                                                .attach_from = attach_from,
                                                                                .kind = kind,
                                                                                .slot = slot,
                                                                                .part = part,
                                                                                .flags = static_cast<uint32_t>(flags),
                                                                            }); })
        .function("add_visual_model_row", +[](nw::ObjectHandle obj, nw::Resref model, nw::Resref attach_to, nw::Resref attach_from, int32_t kind, int32_t slot, int32_t part, int32_t source_part, int32_t model_part, int32_t flags) -> bool {
            if (!valid_object(obj) || slot < -1 || source_part < 0 || model_part < 0 || flags < 0) { return false; }
            return nw::kernel::objects().components().add_visual_model_row(obj, nw::ObjectVisualModel{
                                                                                   .model = model,
                                                                                   .attach_to = attach_to,
                                                                                   .attach_from = attach_from,
                                                                                   .kind = kind,
                                                                                   .slot = slot,
                                                                                   .part = part,
                                                                                   .source_part = source_part,
                                                                                   .model_part = model_part,
                                                                                   .flags = static_cast<uint32_t>(flags),
                                                                               }); })
        .function("add_visual_part", +[](nw::ObjectHandle obj, nw::Resref attach_to, nw::Resref attach_from, int32_t kind, int32_t slot, int32_t part, int32_t source_part, int32_t model_part, int32_t flags) -> bool {
            if (!valid_object(obj) || flags < 0) { return false; }
            return nw::kernel::objects().components().add_visual_part(obj, nw::ObjectVisualModel{
                                                                               .attach_to = attach_to,
                                                                               .attach_from = attach_from,
                                                                               .kind = kind,
                                                                               .slot = slot,
                                                                               .part = part,
                                                                               .source_part = source_part,
                                                                               .model_part = model_part,
                                                                               .flags = static_cast<uint32_t>(flags),
                                                                           }); })
        .function("set_visual_plt_colors", +[](nw::ObjectHandle obj, int32_t slot, int32_t kind, int32_t part, int32_t mask, int32_t skin, int32_t hair, int32_t metal1, int32_t metal2, int32_t cloth1, int32_t cloth2, int32_t leather1, int32_t leather2, int32_t tattoo1, int32_t tattoo2) -> bool {
            if (!valid_object(obj) || slot < -1 || mask < 0) { return false; }
            nw::PltColors colors{};
            if (!set_plt_colors(colors, skin, hair, metal1, metal2, cloth1, cloth2, leather1, leather2, tattoo1, tattoo2)) {
                return false;
            }
            return nw::kernel::objects().components().set_visual_plt_colors(
                obj, slot, kind, part, static_cast<uint32_t>(mask), colors); })
        .function("set_visual_base_plt_colors", +[](nw::ObjectHandle obj, int32_t mask, int32_t skin, int32_t hair, int32_t metal1, int32_t metal2, int32_t cloth1, int32_t cloth2, int32_t leather1, int32_t leather2, int32_t tattoo1, int32_t tattoo2) -> bool {
            if (!valid_object(obj) || mask < 0) { return false; }
            nw::PltColors colors{};
            if (!set_plt_colors(colors, skin, hair, metal1, metal2, cloth1, cloth2, leather1, leather2, tattoo1, tattoo2)) {
                return false;
            }
            return nw::kernel::objects().components().set_visual_base_plt_colors(
                obj, static_cast<uint32_t>(mask), colors); })
        .function("add_visual_light", +[](nw::ObjectHandle obj, int32_t color, float offset_x, float offset_y, float offset_z) -> bool {
            if (!valid_object(obj)) { return false; }
            return nw::kernel::objects().components().add_visual_light(obj, color, glm::vec3{offset_x, offset_y, offset_z}); })
        .finalize();
}

} // namespace nw::smalls
