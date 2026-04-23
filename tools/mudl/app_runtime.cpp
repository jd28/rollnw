#include "app_runtime.hpp"
#include "viewer_runtime.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Module.hpp>
#include <nw/render/render_service.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/game_install.hpp>

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace mudl {

namespace {

static constexpr std::string_view kMudlGltfEnvironmentPath
    = "tests/test_data/renderer/MetalRoughSpheres/glTF/papermill.ktx";

struct KtxHeader {
    uint32_t endianness = 0;
    uint32_t gl_type = 0;
    uint32_t gl_type_size = 0;
    uint32_t gl_format = 0;
    uint32_t gl_internal_format = 0;
    uint32_t gl_base_internal_format = 0;
    uint32_t pixel_width = 0;
    uint32_t pixel_height = 0;
    uint32_t pixel_depth = 0;
    uint32_t number_of_array_elements = 0;
    uint32_t number_of_faces = 0;
    uint32_t number_of_mipmap_levels = 0;
    uint32_t bytes_of_key_value_data = 0;
};

uint32_t read_u32_le(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])
        | (static_cast<uint32_t>(bytes[1]) << 8u)
        | (static_cast<uint32_t>(bytes[2]) << 16u)
        | (static_cast<uint32_t>(bytes[3]) << 24u);
}

uint16_t read_u16_le(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0])
        | static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8u);
}

std::optional<KtxHeader> parse_ktx_header(const std::vector<uint8_t>& bytes)
{
    static constexpr std::array<uint8_t, 12> kKtxMagic{
        0xab, 0x4b, 0x54, 0x58, 0x20, 0x31, 0x31, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a,
    };

    if (bytes.size() < 64 || !std::equal(kKtxMagic.begin(), kKtxMagic.end(), bytes.begin())) {
        return std::nullopt;
    }

    KtxHeader header{};
    header.endianness = read_u32_le(bytes.data() + 12);
    header.gl_type = read_u32_le(bytes.data() + 16);
    header.gl_type_size = read_u32_le(bytes.data() + 20);
    header.gl_format = read_u32_le(bytes.data() + 24);
    header.gl_internal_format = read_u32_le(bytes.data() + 28);
    header.gl_base_internal_format = read_u32_le(bytes.data() + 32);
    header.pixel_width = read_u32_le(bytes.data() + 36);
    header.pixel_height = read_u32_le(bytes.data() + 40);
    header.pixel_depth = read_u32_le(bytes.data() + 44);
    header.number_of_array_elements = read_u32_le(bytes.data() + 48);
    header.number_of_faces = read_u32_le(bytes.data() + 52);
    header.number_of_mipmap_levels = read_u32_le(bytes.data() + 56);
    header.bytes_of_key_value_data = read_u32_le(bytes.data() + 60);
    return header;
}

glm::vec3 latlong_direction(float u, float v)
{
    const float phi = (u - 0.5f) * glm::two_pi<float>();
    const float theta = v * glm::pi<float>();
    const float sin_theta = std::sin(theta);
    return glm::normalize(glm::vec3{
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        std::cos(theta),
    });
}

glm::vec2 env_latlong_uv(const glm::vec3& dir)
{
    const glm::vec3 ndir = glm::normalize(dir);
    const float phi = std::atan2(ndir.y, ndir.x);
    const float theta = std::acos(glm::clamp(ndir.z, -1.0f, 1.0f));
    return glm::vec2{phi / glm::two_pi<float>() + 0.5f, theta / glm::pi<float>()};
}

uint32_t cubemap_face_index(const glm::vec3& dir)
{
    const glm::vec3 ndir = glm::normalize(dir);
    const glm::vec3 ad = glm::abs(ndir);
    if (ad.x >= ad.y && ad.x >= ad.z) {
        return ndir.x >= 0.0f ? 0u : 1u;
    }
    if (ad.y >= ad.x && ad.y >= ad.z) {
        return ndir.y >= 0.0f ? 2u : 3u;
    }
    return ndir.z >= 0.0f ? 4u : 5u;
}

glm::vec2 cubemap_face_uv(uint32_t face, const glm::vec3& dir)
{
    const glm::vec3 ad = glm::abs(dir);
    float sc = 0.0f;
    float tc = 0.0f;

    switch (face) {
    case 0u: sc = -dir.z / ad.x; tc = -dir.y / ad.x; break;
    case 1u: sc =  dir.z / ad.x; tc = -dir.y / ad.x; break;
    case 2u: sc =  dir.x / ad.y; tc =  dir.z / ad.y; break;
    case 3u: sc =  dir.x / ad.y; tc = -dir.z / ad.y; break;
    case 4u: sc =  dir.x / ad.z; tc = -dir.y / ad.z; break;
    default: sc = -dir.x / ad.z; tc = -dir.y / ad.z; break;
    }

    return glm::clamp(glm::vec2{sc, tc} * 0.5f + 0.5f, glm::vec2{0.0f}, glm::vec2{1.0f});
}

struct HdrLatLongImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<glm::vec4> pixels;
};

uint32_t mip_count(uint32_t width, uint32_t height)
{
    uint32_t result = 1;
    while (width > 1u || height > 1u) {
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
        ++result;
    }
    return result;
}

glm::vec4 unpack_half_rgba(const uint8_t* src)
{
    return glm::vec4{
        glm::unpackHalf1x16(read_u16_le(src + 0)),
        glm::unpackHalf1x16(read_u16_le(src + 2)),
        glm::unpackHalf1x16(read_u16_le(src + 4)),
        glm::unpackHalf1x16(read_u16_le(src + 6)),
    };
}

glm::vec4 sample_cubemap_face(const uint8_t* face, uint32_t width, uint32_t height, const glm::vec2& uv)
{
    const float x = glm::clamp(uv.x * static_cast<float>(width) - 0.5f, 0.0f, static_cast<float>(width - 1u));
    const float y = glm::clamp(uv.y * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(x0 + 1u, width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, height - 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const auto load = [&](uint32_t px, uint32_t py) {
        return unpack_half_rgba(face + (static_cast<size_t>(py) * width + px) * 8u);
    };

    const glm::vec4 c00 = load(x0, y0);
    const glm::vec4 c10 = load(x1, y0);
    const glm::vec4 c01 = load(x0, y1);
    const glm::vec4 c11 = load(x1, y1);
    return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
}

glm::vec3 sample_latlong_image(const HdrLatLongImage& image, const glm::vec3& dir)
{
    if (image.width == 0u || image.height == 0u || image.pixels.empty()) {
        return glm::vec3{0.0f};
    }

    const glm::vec2 uv = env_latlong_uv(dir);
    const float x = uv.x * static_cast<float>(image.width) - 0.5f;
    const float y = glm::clamp(
        uv.y * static_cast<float>(image.height) - 0.5f, 0.0f, static_cast<float>(image.height - 1u));
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const int32_t x1 = x0 + 1;
    const uint32_t y1 = std::min(y0 + 1u, image.height - 1u);
    const float tx = x - std::floor(x);
    const float ty = y - static_cast<float>(y0);
    const auto wrap_x = [&](int32_t px) {
        const int32_t width = static_cast<int32_t>(image.width);
        const int32_t wrapped = (px % width + width) % width;
        return static_cast<uint32_t>(wrapped);
    };

    const auto load = [&](uint32_t px, uint32_t py) {
        return glm::vec3(image.pixels[static_cast<size_t>(py) * image.width + px]);
    };

    const glm::vec3 c00 = load(wrap_x(x0), y0);
    const glm::vec3 c10 = load(wrap_x(x1), y0);
    const glm::vec3 c01 = load(wrap_x(x0), y1);
    const glm::vec3 c11 = load(wrap_x(x1), y1);
    return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
}

HdrLatLongImage downsample_latlong_image(const HdrLatLongImage& image)
{
    HdrLatLongImage result{};
    result.width = std::max(1u, image.width / 2u);
    result.height = std::max(1u, image.height / 2u);
    result.pixels.resize(static_cast<size_t>(result.width) * result.height, glm::vec4{0.0f});

    for (uint32_t y = 0; y < result.height; ++y) {
        for (uint32_t x = 0; x < result.width; ++x) {
            const uint32_t src_x0 = std::min(x * 2u, image.width - 1u);
            const uint32_t src_x1 = std::min(src_x0 + 1u, image.width - 1u);
            const uint32_t src_y0 = std::min(y * 2u, image.height - 1u);
            const uint32_t src_y1 = std::min(src_y0 + 1u, image.height - 1u);
            const auto load = [&](uint32_t px, uint32_t py) {
                return image.pixels[static_cast<size_t>(py) * image.width + px];
            };
            result.pixels[static_cast<size_t>(y) * result.width + x] = 0.25f * (
                load(src_x0, src_y0)
                + load(src_x1, src_y0)
                + load(src_x0, src_y1)
                + load(src_x1, src_y1));
        }
    }

    return result;
}

std::vector<HdrLatLongImage> generate_latlong_mip_chain(const HdrLatLongImage& base)
{
    std::vector<HdrLatLongImage> mips;
    mips.push_back(base);
    while (mips.back().width > 1u || mips.back().height > 1u) {
        mips.push_back(downsample_latlong_image(mips.back()));
    }
    return mips;
}

glm::vec3 sample_latlong_image_level(const std::vector<HdrLatLongImage>& mips, const glm::vec3& dir, float mip_level)
{
    if (mips.empty()) {
        return glm::vec3{0.0f};
    }

    const float clamped_mip = glm::clamp(mip_level, 0.0f, static_cast<float>(mips.size() - 1u));
    const uint32_t level0 = static_cast<uint32_t>(std::floor(clamped_mip));
    const uint32_t level1 = std::min(level0 + 1u, static_cast<uint32_t>(mips.size() - 1u));
    const float t = clamped_mip - static_cast<float>(level0);
    const glm::vec3 c0 = sample_latlong_image(mips[level0], dir);
    const glm::vec3 c1 = sample_latlong_image(mips[level1], dir);
    return glm::mix(c0, c1, t);
}

float radical_inverse_vdc(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley(uint32_t index, uint32_t sample_count)
{
    return glm::vec2{
        static_cast<float>(index) / static_cast<float>(sample_count),
        radical_inverse_vdc(index),
    };
}

void orthonormal_basis(const glm::vec3& n, glm::vec3& tangent, glm::vec3& bitangent)
{
    const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
    tangent = glm::normalize(glm::cross(up, n));
    bitangent = glm::cross(n, tangent);
}

glm::vec3 tangent_to_world(const glm::vec3& sample, const glm::vec3& n)
{
    glm::vec3 tangent{0.0f};
    glm::vec3 bitangent{0.0f};
    orthonormal_basis(n, tangent, bitangent);
    return glm::normalize(tangent * sample.x + bitangent * sample.y + n * sample.z);
}

glm::vec3 cosine_sample_hemisphere(const glm::vec2& xi)
{
    const float phi = glm::two_pi<float>() * xi.x;
    const float cos_theta = std::sqrt(1.0f - xi.y);
    const float sin_theta = std::sqrt(xi.y);
    return glm::vec3{
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        cos_theta,
    };
}

glm::vec3 importance_sample_ggx(const glm::vec2& xi, const glm::vec3& n, float roughness)
{
    const float a = std::max(roughness * roughness, 0.001f);
    const float phi = glm::two_pi<float>() * xi.x;
    const float cos_theta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    const float sin_theta = std::sqrt(std::max(1.0f - cos_theta * cos_theta, 0.0f));
    const glm::vec3 h_tangent{
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        cos_theta,
    };
    return tangent_to_world(h_tangent, n);
}

float geometry_schlick_ggx(float ndotv, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return ndotv / (ndotv * (1.0f - k) + k);
}

float geometry_smith(float ndotv, float ndotl, float roughness)
{
    return geometry_schlick_ggx(ndotv, roughness) * geometry_schlick_ggx(ndotl, roughness);
}

glm::vec2 integrate_brdf(float ndotv, float roughness, uint32_t sample_count)
{
    const glm::vec3 v{std::sqrt(std::max(1.0f - ndotv * ndotv, 0.0f)), 0.0f, ndotv};
    const glm::vec3 n{0.0f, 0.0f, 1.0f};

    float a = 0.0f;
    float b = 0.0f;
    for (uint32_t i = 0; i < sample_count; ++i) {
        const glm::vec2 xi = hammersley(i, sample_count);
        const glm::vec3 h = importance_sample_ggx(xi, n, roughness);
        const glm::vec3 l = glm::normalize(2.0f * glm::dot(v, h) * h - v);

        const float ndotl = std::max(l.z, 0.0f);
        const float ndoth = std::max(h.z, 0.0f);
        const float vdoth = std::max(glm::dot(v, h), 0.0f);
        if (ndotl <= 0.0f) {
            continue;
        }

        const float g = geometry_smith(ndotv, ndotl, roughness);
        const float g_vis = (g * vdoth) / std::max(ndoth * ndotv, 1.0e-5f);
        const float fc = std::pow(1.0f - vdoth, 5.0f);
        a += (1.0f - fc) * g_vis;
        b += fc * g_vis;
    }

    return glm::vec2{a, b} / static_cast<float>(sample_count);
}

std::vector<uint16_t> pack_rgba16f_pixels(const std::vector<glm::vec4>& pixels)
{
    std::vector<uint16_t> packed;
    packed.reserve(pixels.size() * 4u);
    for (const auto& pixel : pixels) {
        packed.push_back(glm::packHalf1x16(pixel.r));
        packed.push_back(glm::packHalf1x16(pixel.g));
        packed.push_back(glm::packHalf1x16(pixel.b));
        packed.push_back(glm::packHalf1x16(pixel.a));
    }
    return packed;
}

std::optional<HdrLatLongImage> load_ktx_environment_base(std::string_view path)
{
    const auto env_path = std::filesystem::path{path};
    if (!std::filesystem::exists(env_path)) {
        return std::nullopt;
    }

    std::ifstream input(env_path, std::ios::binary);
    if (!input) {
        LOG_F(WARNING, "Failed to open glTF environment: {}", env_path.string());
        return std::nullopt;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto header = parse_ktx_header(bytes);
    if (!header) {
        LOG_F(WARNING, "Invalid KTX header for glTF environment: {}", env_path.string());
        return std::nullopt;
    }

    if (header->endianness != 0x04030201u
        || header->gl_type != 0x140bu
        || header->gl_type_size != 2u
        || header->gl_format != 0x1908u
        || header->pixel_depth != 0u
        || header->number_of_array_elements != 0u
        || header->number_of_faces != 6u
        || header->pixel_width == 0u
        || header->pixel_height == 0u) {
        LOG_F(WARNING, "Unsupported KTX environment format: {}", env_path.string());
        return std::nullopt;
    }

    size_t offset = 64u + header->bytes_of_key_value_data;
    offset = (offset + 3u) & ~size_t{3u};
    if (offset + 4u > bytes.size()) {
        LOG_F(WARNING, "Truncated KTX environment: {}", env_path.string());
        return std::nullopt;
    }

    const uint32_t image_size = read_u32_le(bytes.data() + offset);
    offset += 4u;
    const size_t pixel_count = static_cast<size_t>(header->pixel_width) * header->pixel_height;
    const size_t expected_size = pixel_count * 4u * sizeof(uint16_t);
    if (image_size < expected_size) {
        LOG_F(WARNING, "Unexpected base KTX image size: {}", env_path.string());
        return std::nullopt;
    }

    std::array<const uint8_t*, 6> faces{};
    size_t face_offset = offset;
    for (uint32_t face = 0; face < 6u; ++face) {
        if (face_offset + image_size > bytes.size()) {
            LOG_F(WARNING, "Truncated KTX cubemap face {}: {}", face, env_path.string());
            return std::nullopt;
        }
        faces[face] = bytes.data() + face_offset;
        face_offset += image_size;
        face_offset = (face_offset + 3u) & ~size_t{3u};
    }

    HdrLatLongImage env{};
    env.width = header->pixel_width * 2u;
    env.height = header->pixel_height;
    env.pixels.resize(static_cast<size_t>(env.width) * env.height);

    for (uint32_t y = 0; y < env.height; ++y) {
        for (uint32_t x = 0; x < env.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(env.width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(env.height);
            const glm::vec3 dir = latlong_direction(u, v);
            const uint32_t face = cubemap_face_index(dir);
            const glm::vec2 face_uv = cubemap_face_uv(face, dir);
            env.pixels[static_cast<size_t>(y) * env.width + x] = sample_cubemap_face(
                faces[face], header->pixel_width, header->pixel_height, face_uv);
        }
    }

    LOG_F(INFO, "Loaded glTF environment: {} ({}x{})", env_path.string(), header->pixel_width, header->pixel_height);
    return env;
}

std::vector<glm::vec4> generate_irradiance_map(const HdrLatLongImage& env, uint32_t width, uint32_t height)
{
    constexpr uint32_t kSampleCount = 256u;
    std::vector<glm::vec4> pixels(static_cast<size_t>(width) * height, glm::vec4{0.0f});

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const glm::vec3 n = latlong_direction(
                (static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            glm::vec3 irradiance{0.0f};
            for (uint32_t i = 0; i < kSampleCount; ++i) {
                const glm::vec3 l = tangent_to_world(cosine_sample_hemisphere(hammersley(i, kSampleCount)), n);
                irradiance += sample_latlong_image(env, l);
            }
            irradiance /= static_cast<float>(kSampleCount);
            pixels[static_cast<size_t>(y) * width + x] = glm::vec4(irradiance, 1.0f);
        }
    }

    return pixels;
}

std::vector<std::vector<uint16_t>> generate_specular_prefilter_mips(
    const HdrLatLongImage& env, uint32_t width, uint32_t height)
{
    const auto source_mips = generate_latlong_mip_chain(env);
    const uint32_t levels = mip_count(width, height);
    std::vector<std::vector<uint16_t>> mips;
    mips.reserve(levels);

    uint32_t mip_width = width;
    uint32_t mip_height = height;
    for (uint32_t level = 0; level < levels; ++level) {
        const float roughness = levels > 1u ? static_cast<float>(level) / static_cast<float>(levels - 1u) : 0.0f;
        const uint32_t sample_count = roughness < 0.10f ? 2048u
            : roughness < 0.35f                          ? 1024u
                                                         : 512u;
        std::vector<glm::vec4> pixels(static_cast<size_t>(mip_width) * mip_height, glm::vec4{0.0f});

        for (uint32_t y = 0; y < mip_height; ++y) {
            for (uint32_t x = 0; x < mip_width; ++x) {
                const glm::vec3 n = latlong_direction(
                    (static_cast<float>(x) + 0.5f) / static_cast<float>(mip_width),
                    (static_cast<float>(y) + 0.5f) / static_cast<float>(mip_height));
                const glm::vec3 v = n;
                glm::vec3 prefiltered{0.0f};
                float total_weight = 0.0f;

                for (uint32_t i = 0; i < sample_count; ++i) {
                    const glm::vec3 h = importance_sample_ggx(hammersley(i, sample_count), n, roughness);
                    const glm::vec3 l = glm::normalize(2.0f * glm::dot(v, h) * h - v);
                    const float ndotl = std::max(glm::dot(n, l), 0.0f);
                    if (ndotl <= 0.0f) {
                        continue;
                    }

                    const float ndoth = std::max(glm::dot(n, h), 0.0f);
                    const float vdoth = std::max(glm::dot(v, h), 0.0f);
                    const float a = std::max(roughness * roughness, 0.001f);
                    const float a2 = a * a;
                    const float denom = std::max((ndoth * ndoth) * (a2 - 1.0f) + 1.0f, 1.0e-5f);
                    const float d = a2 / (glm::pi<float>() * denom * denom);
                    const float pdf = std::max((d * ndoth) / std::max(4.0f * vdoth, 1.0e-5f), 1.0e-5f);
                    const float sa_texel = (4.0f * glm::pi<float>())
                        / static_cast<float>(source_mips.front().width * source_mips.front().height);
                    const float sa_sample = 1.0f / (static_cast<float>(sample_count) * pdf + 1.0e-5f);
                    const float source_mip = roughness <= 0.0f
                        ? 0.0f
                        : 0.5f * std::log2(std::max(sa_sample / sa_texel, 1.0f));

                    prefiltered += sample_latlong_image_level(source_mips, l, source_mip) * ndotl;
                    total_weight += ndotl;
                }

                if (total_weight > 0.0f) {
                    prefiltered /= total_weight;
                }
                pixels[static_cast<size_t>(y) * mip_width + x] = glm::vec4(prefiltered, 1.0f);
            }
        }

        mips.push_back(pack_rgba16f_pixels(pixels));
        mip_width = std::max(1u, mip_width / 2u);
        mip_height = std::max(1u, mip_height / 2u);
    }

    return mips;
}

std::vector<glm::vec4> generate_brdf_lut(uint32_t size)
{
    constexpr uint32_t kSampleCount = 256u;
    std::vector<glm::vec4> pixels(static_cast<size_t>(size) * size, glm::vec4{0.0f});

    for (uint32_t y = 0; y < size; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        for (uint32_t x = 0; x < size; ++x) {
            const float ndotv = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const glm::vec2 brdf = integrate_brdf(ndotv, roughness, kSampleCount);
            pixels[static_cast<size_t>(y) * size + x] = glm::vec4(brdf, 0.0f, 1.0f);
        }
    }

    return pixels;
}

nw::gfx::Handle<nw::gfx::Texture> create_rgba16f_texture(
    nw::gfx::Context* ctx, uint32_t width, uint32_t height, const std::vector<uint16_t>& pixels)
{
    nw::gfx::TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = nw::gfx::Fmt::RGBA16F;
    auto texture = nw::gfx::create_texture(ctx, desc);
    if (!texture.valid()) {
        return {};
    }
    if (!nw::gfx::upload_texture_rgba16f(ctx, texture, pixels.data(), pixels.size() * sizeof(uint16_t))) {
        nw::gfx::destroy_texture(ctx, texture);
        return {};
    }
    return texture;
}

nw::gfx::Handle<nw::gfx::Texture> create_rgba16f_mip_texture(
    nw::gfx::Context* ctx, uint32_t width, uint32_t height, const std::vector<std::vector<uint16_t>>& mips)
{
    std::vector<nw::gfx::TextureMipData> uploads;
    uploads.reserve(mips.size());

    uint32_t mip_width = width;
    uint32_t mip_height = height;
    for (const auto& mip : mips) {
        uploads.push_back(nw::gfx::TextureMipData{
            .data = mip.data(),
            .size = mip.size() * sizeof(uint16_t),
            .width = mip_width,
            .height = mip_height,
        });
        mip_width = std::max(1u, mip_width / 2u);
        mip_height = std::max(1u, mip_height / 2u);
    }

    nw::gfx::TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.mip_levels = static_cast<uint32_t>(mips.size());
    desc.format = nw::gfx::Fmt::RGBA16F;
    auto texture = nw::gfx::create_texture(ctx, desc);
    if (!texture.valid()) {
        return {};
    }
    if (!nw::gfx::upload_texture_rgba16f_mips(ctx, texture, uploads.data(), uploads.size())) {
        nw::gfx::destroy_texture(ctx, texture);
        return {};
    }
    return texture;
}

bool load_gltf_ibl_textures(AppState& state)
{
    const auto env = load_ktx_environment_base(kMudlGltfEnvironmentPath);
    if (!env) {
        return false;
    }

    constexpr uint32_t kDiffuseWidth = 64u;
    constexpr uint32_t kDiffuseHeight = 32u;
    constexpr uint32_t kSpecularWidth = 256u;
    constexpr uint32_t kSpecularHeight = 128u;
    constexpr uint32_t kBrdfLutSize = 256u;

    const auto diffuse_pixels = pack_rgba16f_pixels(generate_irradiance_map(*env, kDiffuseWidth, kDiffuseHeight));
    const auto specular_mips = generate_specular_prefilter_mips(*env, kSpecularWidth, kSpecularHeight);
    const auto brdf_pixels = pack_rgba16f_pixels(generate_brdf_lut(kBrdfLutSize));

    auto diffuse_texture = create_rgba16f_texture(state.gfx_context, kDiffuseWidth, kDiffuseHeight, diffuse_pixels);
    if (!diffuse_texture.valid()) {
        LOG_F(WARNING, "Failed to create glTF irradiance texture");
        return false;
    }

    auto specular_texture = create_rgba16f_mip_texture(
        state.gfx_context, kSpecularWidth, kSpecularHeight, specular_mips);
    if (!specular_texture.valid()) {
        nw::gfx::destroy_texture(state.gfx_context, diffuse_texture);
        LOG_F(WARNING, "Failed to create glTF specular prefilter texture");
        return false;
    }

    auto brdf_texture = create_rgba16f_texture(state.gfx_context, kBrdfLutSize, kBrdfLutSize, brdf_pixels);
    if (!brdf_texture.valid()) {
        nw::gfx::destroy_texture(state.gfx_context, diffuse_texture);
        nw::gfx::destroy_texture(state.gfx_context, specular_texture);
        LOG_F(WARNING, "Failed to create glTF BRDF LUT texture");
        return false;
    }

    state.gltf_ibl_diffuse_texture = diffuse_texture;
    state.gltf_ibl_diffuse_texture_index = nw::gfx::get_bindless_texture_index(state.gfx_context, diffuse_texture);
    state.gltf_ibl_specular_texture = specular_texture;
    state.gltf_ibl_specular_texture_index = nw::gfx::get_bindless_texture_index(state.gfx_context, specular_texture);
    state.gltf_brdf_lut_texture = brdf_texture;
    state.gltf_brdf_lut_texture_index = nw::gfx::get_bindless_texture_index(state.gfx_context, brdf_texture);
    state.gltf_ibl_enabled = true;
    return true;
}

void register_shader_resources()
{
    nw::kernel::services().create();
    auto& resman = nw::kernel::resman();

    // Prefer in-tree shader sources during development so F5 reloads pick up
    // shader-only edits immediately instead of being shadowed by copied build assets.
    resman.add_base_container("lib/nw/render", "shaders", nw::ResourceType::hlsl);

    if (auto* base_path = SDL_GetBasePath()) {
        auto shader_root = std::filesystem::path(base_path) / "shaders";
        resman.add_base_container(shader_root.parent_path(), shader_root.filename().string(), nw::ResourceType::hlsl);
    }
}

} // namespace

bool init_kernel_services(std::string_view module_path, nw::Module** loaded_module)
{
    auto install_info = nw::probe_nwn_install(nw::GameVersion::vEE);
    if (install_info.install.empty()) {
        install_info = nw::probe_nwn_install(nw::GameVersion::v1_69);
    }

    if (install_info.install.empty()) {
        LOG_F(ERROR, "Could not find NWN installation. Set NWN_ROOT environment variable.");
        return false;
    }

    LOG_F(INFO, "Found NWN install: {}", install_info.install);
    nw::kernel::config().set_paths(install_info.install, install_info.user);
    register_shader_resources();

    if (!module_path.empty()) {
        auto* mod = nw::kernel::load_module(std::filesystem::path{module_path}, false);
        if (!mod) {
            LOG_F(ERROR, "Failed to load module: {}", module_path);
            return false;
        }
        if (loaded_module) {
            *loaded_module = mod;
        }
    } else {
        nw::kernel::services().start();
    }
    return true;
}

bool init_render_runtime(AppState& state)
{
    state.shader_provider = std::make_unique<nw::render::ShaderProvider>(
        state.gfx_context, &nw::kernel::resman());
    if (!state.shader_provider->initialize()) {
        LOG_F(ERROR, "Failed to initialize shader provider");
        return false;
    }

    auto* render_service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!render_service) {
        render_service = nw::kernel::services().add<nw::render::RenderService>();
    }
    render_service->configure(state.gfx_context, state.shader_provider.get());
    return true;
}

bool ensure_gltf_ibl_textures(AppState& state)
{
    if (state.gltf_ibl_enabled) {
        return true;
    }
    if (!state.gfx_context) {
        LOG_F(WARNING, "Cannot initialize glTF IBL textures without a graphics context");
        return false;
    }

    LOG_F(INFO, "Initializing glTF IBL textures");
    if (!load_gltf_ibl_textures(state)) {
        LOG_F(WARNING, "Failed to initialize glTF IBL textures");
        return false;
    }
    return true;
}

bool init_graphics(AppState& state, SDL_Window* window)
{
    nw::gfx::CoreConfig core_cfg{};
    core_cfg.app_name = "mudl";
    core_cfg.enable_validation = state.enable_validation;

    state.gfx_core = nw::gfx::create_core(core_cfg);
    if (!state.gfx_core) {
        LOG_F(ERROR, "Failed to create gfx core");
        return false;
    }

    nw::gfx::ContextDesc ctx_desc{};
    ctx_desc.window = window;
    ctx_desc.width = static_cast<uint32_t>(state.window_width);
    ctx_desc.height = static_cast<uint32_t>(state.window_height);

    state.gfx_context = nw::gfx::create_context(state.gfx_core, ctx_desc);
    if (!state.gfx_context) {
        LOG_F(ERROR, "Failed to create gfx context");
        return false;
    }

    return true;
}

bool command_is_headless(std::string_view command)
{
    return command == "frames" || command == "screenshot" || command == "turntable"
        || command == "compute-smoke" || command == "nwn-animation-smoke"
        || command == "area-screenshot";
}

void shutdown_graphics(AppState& state)
{
    if (state.gfx_context) {
        const auto destroy_gltf_texture = [&](auto& texture, auto& index) {
            if (texture.valid()) {
                nw::gfx::destroy_texture(state.gfx_context, texture);
                texture = {};
                index = 0;
            }
        };

        destroy_gltf_texture(state.gltf_ibl_diffuse_texture, state.gltf_ibl_diffuse_texture_index);
        destroy_gltf_texture(state.gltf_ibl_specular_texture, state.gltf_ibl_specular_texture_index);
        destroy_gltf_texture(state.gltf_brdf_lut_texture, state.gltf_brdf_lut_texture_index);
        state.gltf_ibl_enabled = false;
    }
    if (auto* render_service = nw::kernel::services().get_mut<nw::render::RenderService>()) {
        render_service->shutdown_renderer();
    }
    if (state.gfx_context) {
        nw::gfx::destroy_context(state.gfx_context);
        state.gfx_context = nullptr;
    }
    if (state.gfx_core) {
        nw::gfx::destroy_core(state.gfx_core);
        state.gfx_core = nullptr;
    }
}

bool reload_renderer_runtime(AppState& state)
{
    if (!state.gfx_context || !state.shader_provider) {
        LOG_F(ERROR, "Cannot reload shaders before render runtime is initialized");
        return false;
    }

    auto* render_service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!render_service) {
        LOG_F(ERROR, "Render service is not registered");
        return false;
    }

    nw::gfx::wait_idle(state.gfx_context);
    state.renderer.reset();

    if (!render_service->reload_shaders()) {
        LOG_F(ERROR, "Failed to reload render service shaders");
        return false;
    }

    state.renderer = std::make_unique<Renderer>(state.gfx_context);
    if (!state.renderer->initialize(state.shader_provider.get())) {
        LOG_F(ERROR, "Failed to rebuild mudl renderer after shader reload");
        state.renderer.reset();
        return false;
    }

    if (state.loaded_scene_kind != LoadedSceneKind::none) {
        reload_current_scene(state);
    }

    LOG_F(INFO, "Reloaded shaders and rebuilt render pipelines");
    return true;
}

void request_renderer_reload(AppState& state)
{
    state.renderer_reload_requested = true;
}

bool run_compute_smoke(AppState& state)
{
    auto shader = state.shader_provider->get_shader("render_compute_smoke.cs.hlsl");
    if (!shader.valid()) {
        LOG_F(ERROR, "Failed to compile compute smoke shader");
        return false;
    }

    nw::gfx::ComputePipelineDesc pipeline_desc{};
    pipeline_desc.cs = shader;
    pipeline_desc.uses_uniforms = false;
    pipeline_desc.uses_storage_buffer = false;
    auto pipeline = nw::gfx::create_compute_pipeline(state.gfx_context, pipeline_desc);
    if (!pipeline.valid()) {
        LOG_F(ERROR, "Failed to create compute smoke pipeline");
        return false;
    }

    auto* cmd = nw::gfx::begin_frame(state.gfx_context);
    if (!cmd) {
        nw::gfx::destroy_pipeline(state.gfx_context, pipeline);
        LOG_F(ERROR, "Failed to begin frame for compute smoke");
        return false;
    }

    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_dispatch(cmd, 1, 1, 1);
    nw::gfx::end_frame(state.gfx_context);
    nw::gfx::destroy_pipeline(state.gfx_context, pipeline);
    LOG_F(INFO, "Compute smoke dispatch succeeded");
    return true;
}

} // namespace mudl
