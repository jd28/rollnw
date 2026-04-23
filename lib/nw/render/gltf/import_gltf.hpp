#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/render/model.hpp>

#include <filesystem>
#include <memory>

namespace nw::render::gltf {

struct ImportGltfDesc {
    nw::gfx::Context* ctx = nullptr;
    nw::gfx::BindlessTextureIndex fallback_albedo = 0;
    nw::gfx::BindlessTextureIndex fallback_normal = 0;
    nw::gfx::BindlessTextureIndex fallback_surface = 0;
    nw::gfx::BindlessTextureIndex fallback_emissive = 0;
};

std::unique_ptr<nw::render::RenderModel> import_gltf(const std::filesystem::path& path, const ImportGltfDesc& desc);

} // namespace nw::render::gltf
