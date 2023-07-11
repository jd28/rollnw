#pragma once

#include "../util/compression.hpp"
#include "Container.hpp"

#include <absl/container/flat_hash_map.h>
#include <nowide/fstream.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <vector>

namespace nw {

// This currently only implements reading from the client database and shards.
// It's, to quote Bob Ross, a happy accident that's all that needs to be done.
// Creating server side is best done by the official tools (https://github.com/Beamdog/nwsync)
// and downloading/writing to the client databases is best handled by the game itself.
//
// There are things about this that probably aren't ideal.  After investigating some
// C++ sqlite3 wrappers, decided to just use the C interface.
// The game uses the sqlite_modern_cpp library at least in some capacity.
//
// There are some other implementation strategies that might be interesting.
// attaching the nwsyncdata_* databases, creating a view, and letting sqlite3
// figure what shard a particular resource is in.

/// sqlite3 database pointer wrapper
using sqlite3_ptr = std::unique_ptr<sqlite3, void (*)(sqlite3*)>;

struct NWSync;

/// Abstracts over manifests, treating them as a nw::Container
struct NWSyncManifest : public Container {
    NWSyncManifest() = default;
    NWSyncManifest(std::string manifest, NWSync* parent);

    virtual std::vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return manifest_; };
    virtual const std::string& path() const override { return manifest_; };
    virtual size_t size() const override { return 0; }
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return true; } // [TODO] Figure something better out.
    virtual void visit(std::function<void(const Resource&)> callback) const noexcept override;

private:
    std::string manifest_;
    NWSync* parent_ = nullptr;
};

struct NWSync {
    NWSync();
    explicit NWSync(const std::filesystem::path& path);
    NWSync(const NWSync&) = delete;
    NWSync(NWSync&&) = default;
    ~NWSync() = default;

    /// Get a particular manifest as a container
    NWSyncManifest* get(std::string_view manifest);

    /// Get if NWSync was successfully loaded
    bool is_loaded() const noexcept;

    /// Get list of all manifests
    std::vector<std::string> manifests();

    /// Get the number of shards
    size_t shard_count() const noexcept;

    /// Get a pointer to the nwsyncmeta database
    sqlite3* meta() { return meta_.get(); }

    /// List of all shards as active Sqlite3 connections
    std::vector<sqlite3_ptr>& shards() { return shards_; }

    NWSync& operator=(const NWSync&) = delete;
    NWSync& operator=(NWSync&&) = default;

private:
    bool load();

    std::filesystem::path path_;
    sqlite3_ptr meta_;
    std::vector<sqlite3_ptr> shards_;
    absl::flat_hash_map<std::string, NWSyncManifest> map_;

    bool is_loaded_ = false;
};

} // namespace nw
