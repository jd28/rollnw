#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/i18n/Tlk.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/StaticErf.hpp>
#include <nw/resources/StaticKey.hpp>
#include <nw/resources/assets.hpp>
#include <nw/serialization/Gff.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace {

constexpr size_t max_input_size = 1u << 20;

struct TempDir {
    std::filesystem::path path;

    TempDir()
    {
        static std::atomic<uint64_t> next_id{0};
        const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        path = std::filesystem::temp_directory_path()
            / ("rollnw-fuzz-format-" + std::to_string(tid) + "-" + std::to_string(next_id.fetch_add(1)));
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

struct TempFile {
    TempDir dir;
    std::filesystem::path path;

    TempFile(const char* filename, const uint8_t* data, size_t size)
        : path{dir.path / filename}
    {
        std::ofstream out{path, std::ios::binary};
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

struct KeyHeader {
    char type[4] = {'K', 'E', 'Y', ' '};
    char version[4] = {'V', '1', ' ', ' '};
    uint32_t bif_count = 0;
    uint32_t key_count = 0;
    uint32_t offset_file_table = 0;
    uint32_t offset_key_table = 0;
    uint32_t year = 0;
    uint32_t day_of_year = 0;
    char reserved[32] = {};
};

struct FileTable {
    uint32_t size = 0;
    uint32_t name_offset = 0;
    uint16_t name_size = 0;
    uint16_t drives = 0;
};

nw::ResourceData make_resource(const uint8_t* data, size_t size, nw::ResourceType::type type)
{
    nw::ResourceData result;
    result.name.type = type;
    result.bytes.append(data, size);
    return result;
}

void fuzz_gff(const uint8_t* data, size_t size)
{
    nw::Gff gff{make_resource(data, size, nw::ResourceType::gff)};
    if (gff.valid()) {
        auto top = gff.toplevel();
        (void)top.size();
        if (top.size() != 0) {
            (void)top[0].valid();
        }
    }
}

void fuzz_erf(const uint8_t* data, size_t size)
{
    TempFile file{"fuzz.erf", data, size};
    nw::Erf erf{file.path};
    if (erf.valid()) {
        erf.visit([&](const nw::Resource& res) {
            (void)erf.demand(res).bytes.size();
        });
    }
}

void fuzz_static_erf(const uint8_t* data, size_t size)
{
    TempFile file{"fuzz.hak", data, size};
    nw::StaticErf erf{file.path};
    if (erf.valid()) {
        erf.visit([&](nw::Resource, const nw::ContainerKey* key) {
            (void)erf.demand(key).bytes.size();
        });
    }
}

void fuzz_static_key(const uint8_t* data, size_t size)
{
    TempFile file{"fuzz.key", data, size};
    nw::StaticKey key{file.path};
    if (key.valid()) {
        key.visit([&](nw::Resource, const nw::ContainerKey* container_key) {
            (void)key.demand(container_key).bytes.size();
        });
    }
}

void fuzz_bif_via_key(const uint8_t* data, size_t size)
{
    TempDir dir;
    const auto key_dir = dir.path / "keys";
    const auto bif_dir = dir.path / "bifs";
    std::filesystem::create_directories(key_dir);
    std::filesystem::create_directories(bif_dir);

    const auto bif_path = bif_dir / "fuzz.bif";
    {
        std::ofstream bif{bif_path, std::ios::binary};
        bif.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    const std::string bif_name = "bifs/fuzz.bif";
    KeyHeader header{};
    FileTable table{};
    header.bif_count = 1;
    header.key_count = 0;
    header.offset_file_table = sizeof(KeyHeader);
    header.offset_key_table = sizeof(KeyHeader) + sizeof(FileTable) + static_cast<uint32_t>(bif_name.size());
    table.size = static_cast<uint32_t>(size);
    table.name_offset = sizeof(KeyHeader) + sizeof(FileTable);
    table.name_size = static_cast<uint16_t>(bif_name.size());

    const auto key_path = key_dir / "fuzz.key";
    {
        std::ofstream key{key_path, std::ios::binary};
        key.write(reinterpret_cast<const char*>(&header), sizeof(header));
        key.write(reinterpret_cast<const char*>(&table), sizeof(table));
        key.write(bif_name.data(), static_cast<std::streamsize>(bif_name.size()));
    }

    nw::StaticKey key{key_path};
    (void)key.valid();
}

void fuzz_tlk(const uint8_t* data, size_t size)
{
    TempFile file{"fuzz.tlk", data, size};
    nw::Tlk tlk{file.path};
    if (tlk.valid()) {
        (void)tlk.get(0);
        if (tlk.size() != 0) {
            (void)tlk.get(static_cast<uint32_t>(tlk.size() - 1));
            (void)tlk.get(static_cast<uint32_t>(tlk.size()));
        }
    }
}

void fuzz_plt(const uint8_t* data, size_t size)
{
    nw::Plt plt{make_resource(data, size, nw::ResourceType::plt)};
    (void)plt.valid();
    (void)plt.width();
    (void)plt.height();
}

void fuzz_image_dds(const uint8_t* data, size_t size)
{
    nw::Image image{make_resource(data, size, nw::ResourceType::dds), false};
    (void)image.valid();
}

void fuzz_mdl_binary(const uint8_t* data, size_t size)
{
    nw::ResourceData resource = make_resource(data, size, nw::ResourceType::mdl);
    if (resource.bytes.size() != 0) {
        resource.bytes[0] = 0;
    }
    nw::model::Mdl mdl{std::move(resource)};
    (void)mdl.valid();
}

void fuzz_twoda(const uint8_t* data, size_t size)
{
    nw::TwoDA twoda{make_resource(data, size, nw::ResourceType::twoda)};
    if (twoda.is_valid()) {
        (void)twoda.rows();
        (void)twoda.columns();
        if (twoda.rows() != 0 && twoda.columns() != 0) {
            (void)twoda.get_raw(0, 0);
        }
    }
}

void fuzz_selected(uint8_t selector, const uint8_t* data, size_t size)
{
    switch (selector) {
    case 'G':
        fuzz_gff(data, size);
        break;
    case 'E':
        fuzz_erf(data, size);
        break;
    case 'S':
        fuzz_static_erf(data, size);
        break;
    case 'K':
        fuzz_static_key(data, size);
        break;
    case 'B':
        fuzz_bif_via_key(data, size);
        break;
    case 'T':
        fuzz_tlk(data, size);
        break;
    case 'P':
        fuzz_plt(data, size);
        break;
    case 'I':
        fuzz_image_dds(data, size);
        break;
    case 'M':
        fuzz_mdl_binary(data, size);
        break;
    case '2':
        fuzz_twoda(data, size);
        break;
    default:
        switch (selector % 10) {
        case 0:
            fuzz_gff(data, size);
            break;
        case 1:
            fuzz_erf(data, size);
            break;
        case 2:
            fuzz_static_erf(data, size);
            break;
        case 3:
            fuzz_static_key(data, size);
            break;
        case 4:
            fuzz_bif_via_key(data, size);
            break;
        case 5:
            fuzz_tlk(data, size);
            break;
        case 6:
            fuzz_plt(data, size);
            break;
        case 7:
            fuzz_image_dds(data, size);
            break;
        case 8:
            fuzz_mdl_binary(data, size);
            break;
        case 9:
            fuzz_twoda(data, size);
            break;
        }
        break;
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!data || size <= 1 || size > max_input_size) { return 0; }

    try {
        fuzz_selected(data[0], data + 1, size - 1);
    } catch (...) {
        // Invalid parser inputs are expected; memory safety is the fuzz target.
    }

    return 0;
}
