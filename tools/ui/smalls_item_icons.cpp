#include "smalls_item_icons.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_cat.h>

#include <algorithm>
#include <limits>

namespace nw::toolset {
namespace {

bool copy_image_rgba(const Image& image, bool flip_rows, RmlGeneratedTexture& output)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0) {
        return false;
    }
    const size_t pixels = static_cast<size_t>(image.width()) * image.height();
    if (pixels > std::numeric_limits<size_t>::max() / 4) {
        return false;
    }
    const uint32_t channels = image.channels();
    if (channels == 0 || channels > 4) {
        return false;
    }

    output.width = image.width();
    output.height = image.height();
    output.rgba.resize(pixels * 4);
    const auto* source = image.data();
    for (uint32_t y = 0; y < image.height(); ++y) {
        const uint32_t source_y = flip_rows ? image.height() - y - 1 : y;
        for (uint32_t x = 0; x < image.width(); ++x) {
            const size_t source_index = (static_cast<size_t>(source_y) * image.width() + x) * channels;
            const size_t output_index = (static_cast<size_t>(y) * image.width() + x) * 4;
            if (channels == 1 || channels == 2) {
                output.rgba[output_index + 0] = source[source_index];
                output.rgba[output_index + 1] = source[source_index];
                output.rgba[output_index + 2] = source[source_index];
                output.rgba[output_index + 3] = channels == 2
                    ? source[source_index + 1]
                    : 255;
            } else {
                output.rgba[output_index + 0] = source[source_index + 0];
                output.rgba[output_index + 1] = source[source_index + 1];
                output.rgba[output_index + 2] = source[source_index + 2];
                output.rgba[output_index + 3] = channels == 4
                    ? source[source_index + 3]
                    : 255;
            }
        }
    }
    return true;
}

bool load_icon_layer(const ObjectItemIconLayer& layer, RmlGeneratedTexture& output)
{
    if (layer.resource.empty()) {
        return false;
    }
    if ((layer.flags & ObjectItemIconFlags::plt) != 0) {
        auto data = kernel::resman().demand(Resource{layer.resource, ResourceType::plt});
        if (data.bytes.size() == 0) {
            return false;
        }
        const Plt plt{std::move(data)};
        const Image image{plt, layer.plt_colors};
        return copy_image_rgba(image, true, output);
    }

    auto data = kernel::resman().demand_in_order(
        layer.resource, {ResourceType::dds, ResourceType::tga});
    if (data.bytes.size() == 0) {
        return false;
    }
    const Image image{std::move(data), false};
    return copy_image_rgba(image, image.is_bio_dds(), output);
}

bool load_fallback_icon(
    std::span<const ObjectItemIconLayer> layers, RmlGeneratedTexture& output)
{
    const auto fallback = std::find_if(layers.begin(), layers.end(), [](const auto& row) {
        return (row.flags & ObjectItemIconFlags::allow_default) != 0
            && !row.fallback.empty();
    });
    if (fallback == layers.end()) {
        return false;
    }
    auto data = kernel::resman().demand_in_order(
        fallback->fallback, {ResourceType::dds, ResourceType::tga});
    if (data.bytes.size() == 0) {
        return false;
    }
    const Image image{std::move(data), false};
    return copy_image_rgba(image, image.is_bio_dds(), output);
}

bool composite_over(RmlGeneratedTexture& destination, const RmlGeneratedTexture& source)
{
    if (destination.width != source.width || destination.height != source.height
        || destination.rgba.size() != source.rgba.size()) {
        return false;
    }
    for (size_t index = 0; index < destination.rgba.size(); index += 4) {
        const uint32_t source_alpha = source.rgba[index + 3];
        const uint32_t destination_alpha = destination.rgba[index + 3];
        const uint32_t inverse_source_alpha = 255 - source_alpha;
        const uint32_t output_alpha = source_alpha
            + (destination_alpha * inverse_source_alpha + 127) / 255;
        for (size_t channel = 0; channel < 3; ++channel) {
            const uint32_t numerator = source.rgba[index + channel] * source_alpha
                + (destination.rgba[index + channel] * destination_alpha
                          * inverse_source_alpha
                      + 127)
                    / 255;
            destination.rgba[index + channel] = output_alpha == 0
                ? 0
                : static_cast<uint8_t>((numerator + output_alpha / 2) / output_alpha);
        }
        destination.rgba[index + 3] = static_cast<uint8_t>(output_alpha);
    }
    return true;
}

std::string icon_source(std::span<const ObjectItemIconLayer> layers)
{
    std::string result = "rollnw-item-icon://v1/";
    absl::StrAppend(&result, kernel::resman().generation());
    for (const auto& layer : layers) {
        absl::StrAppend(&result, "/", layer.resource.view(), ",", layer.fallback.view(),
            ",", layer.part, ",", static_cast<uint32_t>(layer.flags));
        for (const uint8_t color : layer.plt_colors.data) {
            absl::StrAppend(&result, ",", static_cast<uint32_t>(color));
        }
    }
    return result;
}

bool build_icon(
    std::span<const ObjectItemIconLayer> layers, RmlGeneratedTexture& output)
{
    if (layers.empty()) {
        return false;
    }
    bool have_layer = false;
    for (const auto& layer : layers) {
        RmlGeneratedTexture decoded;
        if (!load_icon_layer(layer, decoded)) {
            return load_fallback_icon(layers, output);
        }
        if (!have_layer) {
            output = std::move(decoded);
            have_layer = true;
        } else if (!composite_over(output, decoded)) {
            return load_fallback_icon(layers, output);
        }
    }
    return have_layer || load_fallback_icon(layers, output);
}

void measure_visible_bounds(RmlGeneratedTexture& texture)
{
    uint32_t min_x = texture.width;
    uint32_t min_y = texture.height;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    bool found = false;
    for (uint32_t y = 0; y < texture.height; ++y) {
        for (uint32_t x = 0; x < texture.width; ++x) {
            const size_t alpha = (static_cast<size_t>(y) * texture.width + x) * 4 + 3;
            if (texture.rgba[alpha] == 0) {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
            found = true;
        }
    }

    if (!found) {
        texture.visible_width = texture.width;
        texture.visible_height = texture.height;
        return;
    }
    texture.visible_x = min_x;
    texture.visible_y = min_y;
    texture.visible_width = max_x - min_x + 1;
    texture.visible_height = max_y - min_y + 1;
}

} // namespace

void build_item_icon_images(uint8_t variant,
    std::span<const ObjectHandle> items,
    ItemIconTextureCache& cache,
    ItemIconBatch& output)
{
    output = {};
    output.sources.resize(items.size());
    const uint64_t generation = kernel::resman().generation();
    if (cache.resource_generation != generation) {
        cache.resource_generation = generation;
        cache.textures.clear();
    }
    if (variant >= ObjectItemIconState::variant_count) {
        output.protocol_valid = false;
        output.diagnostic = "Item icon variant is outside the materialized range";
        output.missing_count = static_cast<uint32_t>(items.size());
        return;
    }

    for (size_t index = 0; index < items.size(); ++index) {
        const auto* state = items[index].type == ObjectType::item
            ? kernel::objects().components().find_item_icons(items[index])
            : nullptr;
        const auto layers = state ? state->variant(variant) : std::span<const ObjectItemIconLayer>{};
        if (layers.empty()) {
            output.protocol_valid = false;
            if (output.diagnostic.empty()) {
                output.diagnostic = "Item has no materialized Smalls icon rows";
            }
            ++output.missing_count;
            continue;
        }

        std::string source = icon_source(layers);
        const auto existing = std::lower_bound(cache.textures.begin(), cache.textures.end(), source,
            [](const RmlGeneratedTexture& texture, std::string_view key) {
                return texture.source < key;
            });
        if (existing != cache.textures.end() && existing->source == source) {
            output.sources[index] = std::move(source);
            continue;
        }

        RmlGeneratedTexture texture;
        if (!build_icon(layers, texture)) {
            ++output.missing_count;
            continue;
        }
        measure_visible_bounds(texture);
        texture.source = source;
        cache.textures.insert(existing, std::move(texture));
        output.sources[index] = std::move(source);
    }

    absl::flat_hash_set<std::string_view> retained_sources;
    retained_sources.reserve(output.sources.size());
    for (const auto& source : output.sources) {
        if (!source.empty()) {
            retained_sources.insert(source);
        }
    }
    const auto removed = std::remove_if(cache.textures.begin(), cache.textures.end(),
        [&retained_sources](const RmlGeneratedTexture& texture) {
            return !retained_sources.contains(texture.source);
        });
    cache.textures.erase(removed, cache.textures.end());
}

} // namespace nw::toolset
