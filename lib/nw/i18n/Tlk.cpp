#include "Tlk.hpp"

#include "../log.hpp"
#include "../util/macros.hpp"

namespace nw {

Tlk::Tlk(const std::filesystem::path& filename)
    : bytes_{ByteArray::from_file(filename)}
{
#define CHECK_OFF(value)                                                                          \
    do {                                                                                          \
        if (!(value)) {                                                                           \
            LOG_F(ERROR, "corrupt tlk: {}, error: {}", filename.c_str(), LIBNW_STRINGIFY(value)); \
            return;                                                                               \
        }                                                                                         \
    } while (0)

    CHECK_OFF(bytes_.size() > sizeof(detail::TlkHeader));
    header_ = reinterpret_cast<detail::TlkHeader*>(bytes_.data());
    CHECK_OFF(strncmp(header_->type, "TLK ", 4) == 0);
    CHECK_OFF(strncmp(header_->version, "V3.0", 4) == 0);
    elements_ = reinterpret_cast<detail::TlkElement*>(bytes_.data() + sizeof(detail::TlkHeader));
    CHECK_OFF(bytes_.size() >= sizeof(detail::TlkElement) * header_->str_count + sizeof(detail::TlkHeader));
    loaded_ = true;

#undef CHECK_OFF
}

std::string_view Tlk::get(uint32_t strref)
{
    std::string_view result;
    if (!loaded_) {
        LOG_F(ERROR, "attempting to get string from invalid tlk");
        return result;
    }

    if (strref >= header_->str_count) {
        LOG_F(ERROR, "attempting to get string from invalid strref");
        return result;
    }

    const auto& ele = elements_[strref];
    if (header_->str_offset + ele.offset + ele.size <= bytes_.size()) {
        result = {(const char*)bytes_.data() + header_->str_offset + ele.offset, ele.size};
    } else {
        LOG_F(ERROR, "failed to read strref: {}", strref);
    }

    return result;
}

size_t Tlk::size() const noexcept
{
    return header_->str_count;
}

bool Tlk::is_valid() const noexcept
{
    return loaded_;
}

} // namespace nw
