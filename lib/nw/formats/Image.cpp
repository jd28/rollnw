#include "Image.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"
#include "../util/string.hpp"

#include <nowide/convert.hpp>
#include <stb/image_DXT.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace nw {

Image::Image(const std::filesystem::path& filename)
    : data_{ResourceData::from_file(filename)}
    , bytes_{nullptr}
{
    is_dds_ = data_.name.type == ResourceType::dds;
    is_loaded_ = parse();
}

Image::Image(ResourceData data)
    : data_{std::move(data)}
    , bytes_{nullptr}
    , is_dds_(data_.name.type == ResourceType::dds)
{
    is_loaded_ = parse();
}

Image::Image(Image&& other)
    : data_{std::move(other.data_)}
    , is_loaded_{other.is_loaded_}
    , bytes_{std::move(other.bytes_)}
    , size_{other.size_}
    , channels_{other.channels_}
    , height_{other.height_}
    , width_{other.width_}
    , is_dds_{other.is_dds_}
{
    other.is_loaded_ = false;
    other.bytes_ = nullptr;
    other.size_ = 0;
}

Image& Image::operator=(Image&& other)
{
    if (this != &other) {
        data_ = std::move(other.data_);
        is_loaded_ = other.is_loaded_;
        bytes_ = other.bytes_;
        size_ = other.size_;
        channels_ = other.channels_;
        height_ = other.height_;
        width_ = other.width_;
        is_dds_ = other.is_dds_;

        other.is_loaded_ = false;
        other.bytes_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

Image::~Image()
{
    if (bytes_) { free(bytes_); }
}

uint32_t Image::channels() const noexcept { return channels_; }

uint8_t* Image::data() { return bytes_; }

uint32_t Image::height() const noexcept { return height_; }

uint32_t Image::width() const noexcept { return width_; }

bool Image::valid() const { return is_loaded_; }

bool Image::write_to(const std::filesystem::path& filename) const
{
    if (!bytes_) return false;

    fs::path temp = fs::temp_directory_path() / filename.filename();
    std::string ext = filename.extension().string();
    const char* temp_path;
#ifdef _MSC_VER
    temp_path = temp.string().c_str();
#else
    temp_path = temp.c_str();
#endif
    int width = static_cast<int>(width_);
    int height = static_cast<int>(height_);
    int channels = static_cast<int>(channels_);

    if (string::icmp(ext, ".dds")) {
        if (!save_image_as_DDS(temp_path, width, height, channels, bytes_)) {
            LOG_F(INFO, "Failed to write DDS");
            return false;
        }
    } else if (string::icmp(ext, ".png")) {
        if (!stbi_write_png(temp_path, width, height, channels, bytes_, 0)) {
            LOG_F(INFO, "Failed to write PNG");
            return false;
        }
    } else if (string::icmp(ext, ".tga")) {
        if (!stbi_write_tga(temp_path, width, height, channels, bytes_)) {
            LOG_F(INFO, "Failed to write TGA");
            return false;
        }
    } else {
        LOG_F(ERROR, "Unknown file conversion type: {}", ext);
        return false;
    }

    return move_file_safely(temp, filename);
}

// ---- Private ---------------------------------------------------------------

bool Image::parse()
{
    if (data_.bytes.size() == 0) { return false; }
    bool result = false;
    if (is_dds_) {
        result = parse_dds();
    } else { // Defer to stb_image
        int width, height, channels;

        bytes_ = stbi_load_from_memory(data_.bytes.data(), static_cast<int>(data_.bytes.size()),
            &width, &height, &channels, 0);

        if (!bytes_) {
            LOG_F(ERROR, "Failed to load image: {}", stbi_failure_reason());
            result = false;
        } else {
            width_ = static_cast<uint32_t>(width);
            height_ = static_cast<uint32_t>(height);
            channels_ = static_cast<uint32_t>(channels);
            result = true;
        }
    }
    data_.bytes.clear(); // If we have bytes we're done with them.
    return result;
}

namespace detail {

struct BiowareDdsHeader {
    uint32_t width;
    uint32_t height;
    uint32_t colors;
    uint32_t reserved[2];
};

void stbi_decode_DXT45_alpha_block(
    unsigned char uncompressed[16 * 4],
    unsigned char compressed[8]);

void stbi_decode_DXT1_block(
    unsigned char uncompressed[16 * 4],
    unsigned char compressed[8]);

void stbi_decode_DXT_color_block(
    unsigned char uncompressed[16 * 4],
    unsigned char compressed[8]);
} // namespace detail

bool Image::parse_dds()
{
    uint32_t magic;
    data_.bytes.read_at(0, &magic, 4);

    if (magic != 0x20534444) {
        return parse_bioware();
    } else {
        return parse_dxt();
    }
}

bool Image::parse_bioware()
{
    detail::BiowareDdsHeader bioware_header;
    // what's here is copy paste from stbi/soil2

    size_t off = 0;
    data_.bytes.read_at(off, &bioware_header, sizeof(detail::BiowareDdsHeader));
    channels_ = bioware_header.colors;
    width_ = bioware_header.width;
    height_ = bioware_header.height;
    off += sizeof(detail::BiowareDdsHeader);

    if (channels_ != 3 && channels_ != 4)
        return false;

    bytes_ = reinterpret_cast<uint8_t*>(malloc(4 * height_ * width_));

    int block_pitch = (width_ + 3) >> 2;
    int num_blocks = block_pitch * ((height_ + 3) >> 2);
    stbi_uc block[16 * 4];
    stbi_uc compressed[8];

    //	now read and decode all the blocks
    for (int i = 0; i < num_blocks; ++i) {
        //	where are we?
        int bx, by, bw = 4, bh = 4;
        int ref_x = 4 * (i % block_pitch);
        int ref_y = 4 * (i / block_pitch);
        //	get the next block's worth of compressed data, and decompress it

        if (channels_ == 4) {
            //	DXT4/5
            memcpy(compressed, data_.bytes.data() + off, 8);
            detail::stbi_decode_DXT45_alpha_block(block, compressed);
            off += 8;
            memcpy(compressed, data_.bytes.data() + off, 8);
            detail::stbi_decode_DXT_color_block(block, compressed);
            off += 8;
        } else {
            memcpy(compressed, data_.bytes.data() + off, 8);
            detail::stbi_decode_DXT1_block(block, compressed);
            off += 8;
        }
        //	is this a partial block?
        if (ref_x + 4 > int(width_)) {
            bw = width_ - ref_x;
        }
        if (ref_y + 4 > int(height_)) {
            bh = height_ - ref_y;
        }
        //	now drop our decompressed data into the buffer
        for (by = 0; by < bh; ++by) {
            int idx = 4 * ((ref_y + by + 0 * width_) * width_ + ref_x);
            for (bx = 0; bx < bw * 4; ++bx) {

                bytes_[idx + bx] = block[by * 16 + bx];
            }
        }
    }

    if (channels_ == 3) { // Gotta switch format
        auto good = reinterpret_cast<uint8_t*>(malloc(3 * height_ * width_));

        for (size_t j = 0; j < height_ * width_; ++j) {
            unsigned char* src = bytes_ + j * 4;
            unsigned char* dest = good + j * 3;
            dest[0] = src[0];
            dest[1] = src[1];
            dest[2] = src[2];
        }
        free(bytes_);
        bytes_ = good;
    }

    return true;
}

bool Image::parse_dxt()
{
    int width, height, channels;

    bytes_ = stbi_load_from_memory(data_.bytes.data(), static_cast<int>(data_.bytes.size()),
        &height, &width, &channels, 0);

    if (bytes_ == nullptr) {
        LOG_F(INFO, "Failed to load DDS: {}", stbi_failure_reason());
        return false;
    }

    width_ = static_cast<uint32_t>(width);
    height_ = static_cast<uint32_t>(height);
    channels_ = static_cast<uint32_t>(channels);

    return true;
}

namespace detail {

//	helper functions
int stbi_convert_bit_range(int c, int from_bits, int to_bits)
{
    int b = (1 << (from_bits - 1)) + c * ((1 << to_bits) - 1);
    return (b + (b >> from_bits)) >> from_bits;
}

void stbi_rgb_888_from_565(unsigned int c, int* r, int* g, int* b)
{
    *r = stbi_convert_bit_range((c >> 11) & 31, 5, 8);
    *g = stbi_convert_bit_range((c >> 05) & 63, 6, 8);
    *b = stbi_convert_bit_range((c >> 00) & 31, 5, 8);
}

void stbi_decode_DXT45_alpha_block(
    unsigned char uncompressed[16 * 4],
    unsigned char compressed[8])
{
    int i, next_bit = 8 * 2;
    unsigned char decode_alpha[8];
    //	each alpha value gets 3 bits, and the 1st 2 bytes are the range
    decode_alpha[0] = compressed[0];
    decode_alpha[1] = compressed[1];
    if (decode_alpha[0] > decode_alpha[1]) {
        //	6 step intermediate
        decode_alpha[2] = uint8_t((6 * decode_alpha[0] + 1 * decode_alpha[1]) / 7);
        decode_alpha[3] = uint8_t((5 * decode_alpha[0] + 2 * decode_alpha[1]) / 7);
        decode_alpha[4] = uint8_t((4 * decode_alpha[0] + 3 * decode_alpha[1]) / 7);
        decode_alpha[5] = uint8_t((3 * decode_alpha[0] + 4 * decode_alpha[1]) / 7);
        decode_alpha[6] = uint8_t((2 * decode_alpha[0] + 5 * decode_alpha[1]) / 7);
        decode_alpha[7] = uint8_t((1 * decode_alpha[0] + 6 * decode_alpha[1]) / 7);
    } else {
        //	4 step intermediate, pluss full and none
        decode_alpha[2] = uint8_t((4 * decode_alpha[0] + 1 * decode_alpha[1]) / 5);
        decode_alpha[3] = uint8_t((3 * decode_alpha[0] + 2 * decode_alpha[1]) / 5);
        decode_alpha[4] = uint8_t((2 * decode_alpha[0] + 3 * decode_alpha[1]) / 5);
        decode_alpha[5] = uint8_t((1 * decode_alpha[0] + 4 * decode_alpha[1]) / 5);
        decode_alpha[6] = 0;
        decode_alpha[7] = 255;
    }
    for (i = 3; i < 16 * 4; i += 4) {
        int idx = 0, bit;
        bit = (compressed[next_bit >> 3] >> (next_bit & 7)) & 1;
        idx += bit << 0;
        ++next_bit;
        bit = (compressed[next_bit >> 3] >> (next_bit & 7)) & 1;
        idx += bit << 1;
        ++next_bit;
        bit = (compressed[next_bit >> 3] >> (next_bit & 7)) & 1;
        idx += bit << 2;
        ++next_bit;
        uncompressed[i] = decode_alpha[idx & 7];
    }
    //	done
}

void stbi_decode_DXT1_block(
    unsigned char uncompressed[16 * 4],
    unsigned char compressed[8])
{
    int next_bit = 4 * 8;
    int i, r, g, b;
    int c0, c1;
    unsigned char decode_colors[4 * 4];
    //	find the 2 primary colors
    c0 = compressed[0] + (compressed[1] << 8);
    c1 = compressed[2] + (compressed[3] << 8);
    stbi_rgb_888_from_565(c0, &r, &g, &b);
    decode_colors[0] = uint8_t(r);
    decode_colors[1] = uint8_t(g);
    decode_colors[2] = uint8_t(b);
    decode_colors[3] = 255;
    stbi_rgb_888_from_565(c1, &r, &g, &b);
    decode_colors[4] = uint8_t(r);
    decode_colors[5] = uint8_t(g);
    decode_colors[6] = uint8_t(b);
    decode_colors[7] = 255;
    if (c0 > c1) {
        //	no alpha, 2 interpolated colors
        decode_colors[8] = uint8_t((2 * decode_colors[0] + decode_colors[4]) / 3);
        decode_colors[9] = uint8_t((2 * decode_colors[1] + decode_colors[5]) / 3);
        decode_colors[10] = uint8_t((2 * decode_colors[2] + decode_colors[6]) / 3);
        decode_colors[11] = 255;
        decode_colors[12] = uint8_t((decode_colors[0] + 2 * decode_colors[4]) / 3);
        decode_colors[13] = uint8_t((decode_colors[1] + 2 * decode_colors[5]) / 3);
        decode_colors[14] = uint8_t((decode_colors[2] + 2 * decode_colors[6]) / 3);
        decode_colors[15] = 255;
    } else {
        //	1 interpolated color, alpha
        decode_colors[8] = uint8_t((decode_colors[0] + decode_colors[4]) / 2);
        decode_colors[9] = uint8_t((decode_colors[1] + decode_colors[5]) / 2);
        decode_colors[10] = uint8_t((decode_colors[2] + decode_colors[6]) / 2);
        decode_colors[11] = 255;
        decode_colors[12] = 0;
        decode_colors[13] = 0;
        decode_colors[14] = 0;
        decode_colors[15] = 0;
    }
    //	decode the block
    for (i = 0; i < 16 * 4; i += 4) {
        int idx = ((compressed[next_bit >> 3] >> (next_bit & 7)) & 3) * 4;
        next_bit += 2;
        uncompressed[i + 0] = decode_colors[idx + 0];
        uncompressed[i + 1] = decode_colors[idx + 1];
        uncompressed[i + 2] = decode_colors[idx + 2];
        uncompressed[i + 3] = decode_colors[idx + 3];
    }
    //	done
}

void stbi_decode_DXT_color_block(
    unsigned char uncompressed[16 * 4],
    unsigned char compressed[8])
{
    int next_bit = 4 * 8;
    int i, r, g, b;
    int c0, c1;
    unsigned char decode_colors[4 * 3];
    //	find the 2 primary colors
    c0 = compressed[0] + (compressed[1] << 8);
    c1 = compressed[2] + (compressed[3] << 8);
    stbi_rgb_888_from_565(c0, &r, &g, &b);
    decode_colors[0] = uint8_t(r);
    decode_colors[1] = uint8_t(g);
    decode_colors[2] = uint8_t(b);
    stbi_rgb_888_from_565(c1, &r, &g, &b);
    decode_colors[3] = uint8_t(r);
    decode_colors[4] = uint8_t(g);
    decode_colors[5] = uint8_t(b);
    //	Like DXT1, but no choicees:
    //	no alpha, 2 interpolated colors
    decode_colors[6] = uint8_t((2 * decode_colors[0] + decode_colors[3]) / 3);
    decode_colors[7] = uint8_t((2 * decode_colors[1] + decode_colors[4]) / 3);
    decode_colors[8] = uint8_t((2 * decode_colors[2] + decode_colors[5]) / 3);
    decode_colors[9] = uint8_t((decode_colors[0] + 2 * decode_colors[3]) / 3);
    decode_colors[10] = uint8_t((decode_colors[1] + 2 * decode_colors[4]) / 3);
    decode_colors[11] = uint8_t((decode_colors[2] + 2 * decode_colors[5]) / 3);
    //	decode the block
    for (i = 0; i < 16 * 4; i += 4) {
        int idx = ((compressed[next_bit >> 3] >> (next_bit & 7)) & 3) * 3;
        next_bit += 2;
        uncompressed[i + 0] = decode_colors[idx + 0];
        uncompressed[i + 1] = decode_colors[idx + 1];
        uncompressed[i + 2] = decode_colors[idx + 2];
    }
    //	done
}

} // namespace detail

} // namespace nw
