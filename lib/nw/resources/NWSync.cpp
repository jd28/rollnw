#include "NWSync.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"
#include "../util/scope_exit.hpp"
#include "../util/templates.hpp"

#include <fmt/format.h>
#include <nowide/convert.hpp>

#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace std::literals;

namespace nw {

namespace detail {
void sqlite3_deleter(sqlite3* db) { sqlite3_close(db); }
} // namespace detail

NWSyncManifest::NWSyncManifest(std::string manifest, NWSync* parent)
    : manifest_{std::move(manifest)}
    , parent_{parent}
{
}

std::vector<ResourceDescriptor> NWSyncManifest::all() const
{
    std::vector<ResourceDescriptor> result;

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    SCOPE_EXIT([stmt] { sqlite3_finalize(stmt); });
    const char sql[] = "SELECT resref, restype from manifest_resrefs where manifest_sha1 = ?";

    if (SQLITE_OK != sqlite3_prepare_v2(parent_->meta(), sql, sizeof(sql), &stmt, &tail)) {
        LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(parent_->meta()));
        return result;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, manifest_.c_str(), static_cast<int>(manifest_.size()), nullptr)) {
        LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(parent_->meta()));
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ResourceDescriptor rd = {
            Resource{std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))),
                static_cast<ResourceType::type>(sqlite3_column_int(stmt, 1))},
            0, // No good way..
            0,
            this};

        result.push_back(rd);
    }
    return result;
}

bool NWSyncManifest::contains(Resource res) const
{
    return !!stat(res);
}

ResourceData NWSyncManifest::demand(Resource res) const
{
    ResourceData result;

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    SCOPE_EXIT([stmt] { sqlite3_finalize(stmt); });
    sqlite3_prepare_v2(parent_->meta(), R"x(SELECT resref_sha1
                                          FROM manifest_resrefs
                                          WHERE manifest_sha1 = ? AND resref = ? and restype = ?)x",
        -1, &stmt, &tail);

    sqlite3_bind_text(stmt, 1, manifest_.c_str(), static_cast<int>(manifest_.size()), nullptr);
    sqlite3_bind_text(stmt, 2, res.resref.view().data(), static_cast<int>(res.resref.length()), nullptr);
    sqlite3_bind_int(stmt, 3, res.type);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        LOG_F(ERROR, "Failed to find: {}", res.filename());
        return result;
    }

    for (auto& s : parent_->shards()) {
        sqlite3_stmt* data_stmt = nullptr;
        const char* data_tail = nullptr;
        SCOPE_EXIT([data_stmt] { sqlite3_finalize(data_stmt); });
        if (SQLITE_OK != sqlite3_prepare_v2(s.get(), R"x(SELECT data
                                          FROM resrefs
                                          WHERE sha1 = ?)x",
                -1, &data_stmt, &data_tail)) {
            LOG_F(ERROR, "sqlite3: {}", sqlite3_errmsg(s.get()));
            continue;
        }

        if (SQLITE_OK != sqlite3_bind_text(data_stmt, 1, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), -1, nullptr)) {
            LOG_F(ERROR, "sqlite3: {}", sqlite3_errmsg(s.get()));
            continue;
        }

        if (sqlite3_step(data_stmt) == SQLITE_ROW) {
            result.name = res;
            result.bytes = decompress({reinterpret_cast<const uint8_t*>(sqlite3_column_blob(data_stmt, 0)),
                                          static_cast<uint32_t>(sqlite3_column_bytes(data_stmt, 0))},
                "NSYC");
            return result;
        }
    }

    return result;
}

int NWSyncManifest::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    if (!std::filesystem::is_directory(output)) {
        // Needs to be create_directories to simplify usage.  Alternatively,
        // could assert that path must already exist.  Needs error handling.
        std::filesystem::create_directories(output);
    }

    int count = 0;

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    SCOPE_EXIT([stmt] { sqlite3_finalize(stmt); });
    const char sql[] = "SELECT resref, restype from manifest_resrefs where manifest_sha1 = ?";

    if (SQLITE_OK != sqlite3_prepare_v2(parent_->meta(), sql, sizeof(sql), &stmt, &tail)) {
        LOG_F(ERROR, "sqlite3: {}", sqlite3_errmsg(parent_->meta()));
        return count;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, manifest_.c_str(), static_cast<int>(manifest_.size()), nullptr)) {
        LOG_F(ERROR, "sqlite3: {}", sqlite3_errmsg(parent_->meta()));
        return count;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Resource r(std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))),
            static_cast<ResourceType::type>(sqlite3_column_int(stmt, 1)));
        auto fname = r.filename();
        if (std::regex_match(fname, pattern)) {
            auto data = demand(r);
            if (data.bytes.size()) {
                ++count;
                std::ofstream out{output / fs::path(fname), std::ios_base::binary};
                ostream_write(out, data.bytes.data(), data.bytes.size());
            }
        }
    }

    return count;
}

ResourceDescriptor NWSyncManifest::stat(const Resource& res) const
{
    ResourceDescriptor result;

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    SCOPE_EXIT([stmt] { sqlite3_finalize(stmt); });
    sqlite3_prepare_v2(parent_->meta(), R"x(SELECT created_at
                                          FROM manifest_resrefs
                                          WHERE manifest_sha1 = ? AND resref = ? and restype = ?)x",
        -1, &stmt, &tail);

    sqlite3_bind_text(stmt, 1, manifest_.c_str(), static_cast<int>(manifest_.size()), nullptr);
    sqlite3_bind_text(stmt, 2, res.resref.view().data(), static_cast<int>(res.resref.length()), nullptr);
    sqlite3_bind_int(stmt, 3, res.type);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        LOG_F(ERROR, "Failed to find: {}", res.filename());
        return result;
    }

    result.name = res;
    result.mtime = sqlite3_column_int(stmt, 0);
    result.size = ~0u; // Punting
    result.parent = this;

    return result;
}

void NWSyncManifest::visit(std::function<void(const Resource&)> callback) const noexcept
{
    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    SCOPE_EXIT([stmt] { sqlite3_finalize(stmt); });
    const char sql[] = "SELECT resref, restype from manifest_resrefs where manifest_sha1 = ?";

    if (SQLITE_OK != sqlite3_prepare_v2(parent_->meta(), sql, sizeof(sql), &stmt, &tail)) {
        LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(parent_->meta()));
        return;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, manifest_.c_str(), static_cast<int>(manifest_.size()), nullptr)) {
        LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(parent_->meta()));
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        callback(Resource{std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))),
            static_cast<ResourceType::type>(sqlite3_column_int(stmt, 1))});
    }
}

// == NWSync ==================================================================
NWSync::NWSync()
    : meta_{nullptr, detail::sqlite3_deleter}
{
}

NWSync::NWSync(const std::filesystem::path& path)
    : meta_{nullptr, detail::sqlite3_deleter}
{
    if (!fs::exists(path)) {
        throw std::invalid_argument(fmt::format("file '{}' does not exist", path));
    }

    path_ = std::filesystem::canonical(path);
    is_loaded_ = load();
}

bool NWSync::is_loaded() const noexcept
{
    return is_loaded_;
}

NWSyncManifest* NWSync::get(std::string_view manifest)
{
    absl::string_view v{manifest.data(), manifest.length()};
    auto it = map_.find(v);
    return it != std::end(map_) ? &it->second : nullptr;
}

std::vector<std::string> NWSync::manifests()
{
    std::vector<std::string> result;

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    SCOPE_EXIT([stmt] { sqlite3_finalize(stmt); });
    const char sql[] = "SELECT sha1 FROM manifests";
    if (SQLITE_OK != sqlite3_prepare_v2(meta_.get(), sql, sizeof(sql), &stmt, &tail)) {
        LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(meta_.get()));
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* m = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (!m) {
            break;
        }
        result.push_back(m);
    }

    return result;
}

size_t NWSync::shard_count() const noexcept
{
    return shards_.size();
}

bool NWSync::load()
{
    sqlite3* db = nullptr;
    fs::path db_path = path_ / "nwsyncmeta.sqlite3";
    if (!fs::exists(db_path)) {
        LOG_F(ERROR, "'{}' does not exist", db_path);
        return false;
    }
    if (SQLITE_OK != sqlite3_open(path_to_string(db_path).c_str(), &db)) {
        LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(db));
        return false;
    }
    meta_ = sqlite3_ptr(db, detail::sqlite3_deleter);

    int i = 0;
    while (true) {
        db = nullptr;
        auto db_name = fmt::format("nwsyncdata_{}.sqlite3", i);
        db_path = path_ / db_name;

        if (!fs::exists(db_path)) {
            break;
        }
        if (SQLITE_OK != sqlite3_open(path_to_string(db_path).c_str(), &db)) {
            LOG_F(ERROR, "sqlite3 error: {}", sqlite3_errmsg(db));
            return false;
        }
        shards_.emplace_back(db, detail::sqlite3_deleter);
        ++i;
    }

    for (const auto& m : manifests()) {
        map_.emplace(m, NWSyncManifest{m, this});
    }

    return true;
}

} // namespace nw
