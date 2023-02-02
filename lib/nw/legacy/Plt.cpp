#include "Plt.hpp"

#include <cstring>

namespace nw {

Plt::Plt(std::filesystem::path file)
    : Plt(ByteArray::from_file(file))
{
}

Plt::Plt(ByteArray bytes)
    : bytes_{std::move(bytes)}
{
    if (bytes_.size() > 24 && memcmp(bytes_.data(), "PLT V1  ", 8) == 0) {
        bytes_.read_at(16, &width_, 4);
        bytes_.read_at(20, &height_, 4);
        valid_ = bytes_.size() == 24 + (2 * width_ * height_);
    } else {
        valid_ = false;
    }
}

uint32_t Plt::height() const { return height_; }
const PltPixel* Plt::pixels() const
{
    return reinterpret_cast<const PltPixel*>(bytes_.data() + 24);
}
bool Plt::valid() const { return valid_; }
uint32_t Plt::width() const { return width_; }

} // namespace nw
