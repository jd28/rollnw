#include "Image.hpp"

#include "../log.hpp"
#include "../util/string.hpp"

#include "d3dtypes.h"

#include <nowide/convert.hpp>
#include <stb/image_DXT.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace nw {

Image::Image(const std::filesystem::path& filename)
    : bytes_{ByteArray::from_file(filename)}
    , data_{nullptr, nullptr}
{
    is_dds_ = string::icmp(filename.extension().string(), ".dds");
    is_loaded_ = parse();
}

Image::Image(ByteArray bytes, bool is_dds)
    : bytes_{std::move(bytes)}
    , is_dds_(is_dds)
    , data_{nullptr, nullptr}
{
    is_loaded_ = parse();
}

int Image::channels() { return channels_; }

uint8_t* Image::data() { return data_.get(); }

int Image::height() const { return height_; }

int Image::width() const { return width_; }

bool Image::valid() const { return is_loaded_; }

bool Image::write_to(const std::filesystem::path& filename) const
{
    if (!data_) return false;

    fs::path temp = fs::temp_directory_path() / filename.filename();
    std::string ext = filename.extension().string();
    const char* temp_path;
#ifdef _MSC_VER
    temp_path = temp.string().c_str();
#else
    temp_path = temp.c_str();
#endif
    if (string::icmp(ext, ".dds")) {
        if (!save_image_as_DDS(temp_path, width_, height_, channels_, data_.get())) {
            LOG_F(INFO, "Failed to write DDS");
            return false;
        }
    } else if (string::icmp(ext, ".png")) {
        if (!stbi_write_png(temp_path, width_, height_, channels_, data_.get(), 0)) {
            LOG_F(INFO, "Failed to write PNG");
            return false;
        }
    } else if (string::icmp(ext, ".tga")) {
        if (!stbi_write_tga(temp_path, width_, height_, channels_, data_.get())) {
            LOG_F(INFO, "Failed to write TGA");
            return false;
        }
    } else {
        LOG_F(ERROR, "Unknown file conversion type: {}", ext);
        return false;
    }

    std::error_code ec;
    fs::copy_file(temp, filename, fs::copy_options::overwrite_existing, ec);
    fs::remove(temp);
    if (ec) {
        LOG_F(ERROR, "Failed to write {}, error: {}", filename.string(), ec.value());
        return false;
    }

    return true;
}

// ---- Private ---------------------------------------------------------------

bool Image::parse()
{
    if (bytes_.size() == 0) { return false; }
    bool result = false;
    if (is_dds_) {
        result = parse_dds();
    } else { // Defer to stb_image
        data_ = std::unique_ptr<uint8_t[], void (*)(void*)>(stbi_load_from_memory((stbi_uc*)bytes_.data(),
                                                                static_cast<int>(bytes_.size()), &width_, &height_, &channels_, 0),
            free);
        if (!data_) {
            LOG_F(ERROR, "Failed to load image: {}", stbi_failure_reason());
            result = false;
        } else {
            result = true;
        }
    }
    bytes_.clear(); // If we have bytes we're done with them.
    return result;
}

namespace detail {
// The stuff in detail is from Torlack's NWNExplorer

struct BiowareDdsHeader {
    uint32_t width;
    uint32_t height;
    uint32_t colors;
    uint32_t reserved[2];
};

static int DdsRound(double d);
static void DecompressDdsDXT3AlphaBlock(const uint8_t* data, D3DCOLOR* dest, int x, int y, int width);
static void DecompressDdsDXT5AlphaBlock(const uint8_t* data, D3DCOLOR* dest, int x, int y, int width);
static void DecompressDdsColorBlock(const uint8_t* data, D3DCOLOR* dest, int x, int y, int width, int colors);

} // namespace detail

bool Image::parse_dds()
{
    uint32_t magic;
    bytes_.read_at(0, &magic, 4);

    if (magic != 0x20534444) {
        return parse_bioware();
    } else {
        return parse_dxt();
    }
}

bool Image::parse_bioware()
{
    detail::BiowareDdsHeader bioware_header;

    size_t off = 0;
    bytes_.read_at(off, &bioware_header, sizeof(detail::BiowareDdsHeader));
    channels_ = bioware_header.colors;
    width_ = bioware_header.width;
    height_ = bioware_header.height;
    off += sizeof(detail::BiowareDdsHeader);

    if (channels_ != 3 && channels_ != 4)
        return false;

    D3DCOLOR* pixels = new D3DCOLOR[height_ * width_];
    data_ = std::unique_ptr<uint8_t[], void (*)(void*)>((uint8_t*)pixels, free);

    int x = 0, y = 0;

    while (y < height_) {
        if (0) {
            detail::DecompressDdsDXT3AlphaBlock((uint8_t*)bytes_.data() + off, pixels, x, y, width_);
            off += 8;
        } else if (channels_ == 4) {
            detail::DecompressDdsDXT5AlphaBlock((uint8_t*)bytes_.data() + off, pixels, x, y, width_);
            off += 8;
        }

        detail::DecompressDdsColorBlock((uint8_t*)bytes_.data() + off, pixels, x, y, width_, channels_);
        off += 8;

        x += 4;
        if (x >= width_) {
            x = 0;
            y += 4;
        }
    }

    return true;
}

bool Image::parse_dxt()
{
    data_ = std::unique_ptr<uint8_t[], void (*)(void*)>(stbi_load_from_memory((stbi_uc*)bytes_.data(),
                                                            static_cast<int>(bytes_.size()), &height_, &width_, &channels_, 0),
        free);
    if (data_ == nullptr) {
        LOG_F(INFO, "Failed to load DDS: {}", stbi_failure_reason());
        return false;
    }

    return true;
}

namespace detail {

static int DdsRound(double d)
{
    int ret = (int)(d + 0.500001);

    if (ret < 0)
        return 0;
    else if (ret > 255)
        return 255;

    return ret;
}

static void DecompressDdsDXT3AlphaBlock(const uint8_t* data, D3DCOLOR* dest, int x, int y, int width)
{
    int i, j;
    uint8_t c = 0;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            if (j % 2)
                c >>= 4;
            else
                c = *(data++);

            dest[((y + i) * width) + (x + j)] = RGBA_SETALPHA(dest[((y + i) * width) + (x + j)],
                DdsRound((double)(c & 0x0F) * 0xFF / 0x0F));
        }
    }
}

static void DecompressDdsDXT5AlphaBlock(const uint8_t* data, D3DCOLOR* dest, int x, int y, int width)
{
    uint8_t a[8];
    uint8_t alphaIndex;
    int bitPos;
    int i, j;

    a[0] = *data;
    a[1] = *(data + 1);

    if (*data > *(data + 1)) {
        a[2] = DdsRound(6.0 / 7 * a[0] + 1.0 / 7 * a[1]);
        a[3] = DdsRound(5.0 / 7 * a[0] + 2.0 / 7 * a[1]);
        a[4] = DdsRound(4.0 / 7 * a[0] + 3.0 / 7 * a[1]);
        a[5] = DdsRound(3.0 / 7 * a[0] + 4.0 / 7 * a[1]);
        a[6] = DdsRound(2.0 / 7 * a[0] + 5.0 / 7 * a[1]);
        a[7] = DdsRound(1.0 / 7 * a[0] + 6.0 / 7 * a[1]);
    } else {
        a[2] = DdsRound(4.0 / 5 * a[1] + 1.0 / 5 * a[0]);
        a[3] = DdsRound(3.0 / 5 * a[1] + 2.0 / 5 * a[0]);
        a[4] = DdsRound(2.0 / 5 * a[1] + 3.0 / 5 * a[0]);
        a[5] = DdsRound(1.0 / 5 * a[1] + 4.0 / 5 * a[0]);
        a[6] = 0x00;
        a[7] = 0xFF;
    }

    data += 2;
    bitPos = 0;

#define DDS_GET_BIT(X) (((*(data + (X) / 8)) >> ((X) % 8)) & 0x01)
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            alphaIndex = (DDS_GET_BIT(bitPos + 2) << 2) | (DDS_GET_BIT(bitPos + 1) << 1) | DDS_GET_BIT(bitPos);

            dest[((y + i) * width) + (x + j)] = RGBA_SETALPHA(dest[((y + i) * width) + (x + j)], a[alphaIndex]);

            bitPos += 3;
        }
    }
#undef DDS_GET_BIT
}

static void DecompressDdsColorBlock(const uint8_t* data, D3DCOLOR* dest, int x, int y, int width, int colors)
{
    int i, j, oneBitTrans;
    uint8_t colorIndex;
    uint16_t w, word1, word2, *pData = (uint16_t*)data;

    struct DDS_COLOR {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } c[4];
    const uint8_t* alphas;

    word1 = (*pData);
    word2 = (*(pData + 1));
    pData += 2;

    if (word1 > word2) {
        oneBitTrans = 0;
    } else {
        if (colors == 3)
            oneBitTrans = 1;
        else
            oneBitTrans = 0;
    }

    if (oneBitTrans)
        alphas = (const uint8_t*)"\xff\xff\xff\x00";
    else
        alphas = (const uint8_t*)"\xff\xff\xff\xff";

    c[0].r = (word1 >> 11) & 0x1F;
    c[0].r = DdsRound((double)c[0].r * 0xFF / 0x1F);
    c[0].g = (word1 >> 5) & 0x3F;
    c[0].g = DdsRound((double)c[0].g * 0xFF / 0x3F);
    c[0].b = (word1)&0x1F;
    c[0].b = DdsRound((double)c[0].b * 0xFF / 0x1F);

    c[1].r = (word2 >> 11) & 0x1F;
    c[1].r = DdsRound((double)c[1].r * 0xFF / 0x1F);
    c[1].g = (word2 >> 5) & 0x3F;
    c[1].g = DdsRound((double)c[1].g * 0xFF / 0x3F);
    c[1].b = (word2)&0x1F;
    c[1].b = DdsRound((double)c[1].b * 0xFF / 0x1F);

    if (oneBitTrans) {
        c[2].r = DdsRound(0.5 * c[0].r + 0.5 * c[1].r);
        c[2].g = DdsRound(0.5 * c[0].g + 0.5 * c[1].g);
        c[2].b = DdsRound(0.5 * c[0].b + 0.5 * c[1].b);

        c[3].r = 0;
        c[3].g = 0;
        c[3].b = 0;
    } else {
        c[2].r = DdsRound(2.0 / 3 * c[0].r + 1.0 / 3 * c[1].r);
        c[2].g = DdsRound(2.0 / 3 * c[0].g + 1.0 / 3 * c[1].g);
        c[2].b = DdsRound(2.0 / 3 * c[0].b + 1.0 / 3 * c[1].b);

        c[3].r = DdsRound(1.0 / 3 * c[0].r + 2.0 / 3 * c[1].r);
        c[3].g = DdsRound(1.0 / 3 * c[0].g + 2.0 / 3 * c[1].g);
        c[3].b = DdsRound(1.0 / 3 * c[0].b + 2.0 / 3 * c[1].b);
    }

    w = (*pData++);

    if (colors == 3) {
        for (i = 0; i < 4; i++) {
            if (i == 2)
                w = (*pData++);

            for (j = 0; j < 4; j++) {
                colorIndex = w & 0x03;

                dest[((y + i) * width) + (x + j)] = RGBA_MAKE(c[colorIndex].b, c[colorIndex].g, c[colorIndex].r, alphas[colorIndex]);

                w >>= 2;
            }
        }
    } else {
        for (i = 0; i < 4; i++) {
            if (i == 2)
                w = (*pData++);

            for (j = 0; j < 4; j++) {
                colorIndex = w & 0x03;

                dest[((y + i) * width) + (x + j)] = RGBA_MAKE(c[colorIndex].b, c[colorIndex].g, c[colorIndex].r,
                    RGBA_GETALPHA(dest[((y + i) * width) + (x + j)]));
                w >>= 2;
            }
        }
    }
}

} // namespace detail

} // namespace nw
