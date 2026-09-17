#pragma once

#include <RmlUi/Core/FileInterface.h>

namespace nw {
struct Resource;
struct ResourceData;
struct ResourceManager;
}

namespace nw::toolset {

// Required SDK adapter: sources remain live through Rml shutdown. Each cold
// file token owns copied bytes or a stdio file until exactly one Close; tokens
// must originate from Open and cannot be used after Close. Read is a byte batch.
// Null tokens/buffers return zero/false; missing opens and invalid memory seeks
// fail. The stdio partition keeps the platform's existing seek/error behavior.
class ClientRmlFileInterface final : public Rml::FileInterface {
public:
    ClientRmlFileInterface(ResourceManager& ui_resources, ResourceManager& game_resources);

    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle handle) override;
    size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override;
    bool Seek(Rml::FileHandle handle, long offset, int origin) override;
    size_t Tell(Rml::FileHandle handle) override;
    size_t Length(Rml::FileHandle handle) override;

private:
    Resource resource_from_path(Rml::String path) const;
    ResourceData demand(const Rml::String& path) const;

    ResourceManager* ui_resources_ = nullptr;
    ResourceManager* game_resources_ = nullptr;
};

} // namespace nw::toolset
