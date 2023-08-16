#include "Plt.hpp"

#include "../kernel/Resources.hpp"
#include "../log.hpp"

#include <cstring>

namespace nw {

Plt::Plt(std::filesystem::path file)
    : Plt(ResourceData::from_file(file))
{
}

Plt::Plt(ResourceData data)
    : data_{std::move(data)}
{
    if (data_.bytes.size() > 24 && memcmp(data_.bytes.data(), "PLT V1  ", 8) == 0) {
        data_.bytes.read_at(16, &width_, 4);
        data_.bytes.read_at(20, &height_, 4);
        valid_ = data_.bytes.size() == 24 + (2 * width_ * height_);
    } else {
        valid_ = false;
    }
}

uint32_t Plt::height() const { return height_; }
const PltPixel* Plt::pixels() const
{
    return reinterpret_cast<const PltPixel*>(data_.bytes.data() + 24);
}
bool Plt::valid() const { return valid_; }
uint32_t Plt::width() const { return width_; }

std::array<uint8_t, 4> decode_plt_color(const Plt& plt, const PltColors& colors, uint32_t x, uint32_t y)
{
    std::array<uint8_t, 4> result{};
    if (x >= plt.width() || y >= plt.height()) {
        LOG_F(ERROR, "[plt] invalid coordinates ({}, {})", x, y);
        return result;
    }

    auto pixel = plt.pixels()[y * plt.width() + x];
    auto selected_color = colors.data[pixel.layer];
    auto img = nw::kernel::resman().palette_texture(pixel.layer);
    if (!img->valid()) {
        LOG_F(ERROR, "[plt] invalid palette texture for layer {}", uint8_t(pixel.layer));
        return result;
    }

    auto pal_data = img->data() + (selected_color * img->width() + pixel.color) * img->channels();

    return {pal_data[0], pal_data[1], pal_data[2], img->channels() == 4 ? pal_data[3] : uint8_t(0xff)};
}

} // namespace nw
