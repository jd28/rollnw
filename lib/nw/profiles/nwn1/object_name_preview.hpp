#pragma once

#include "../../config.hpp"
#include "../../objects/ObjectHandle.hpp"

#include <filesystem>

namespace nwn1 {

nw::String preview_object_name_from_file(const std::filesystem::path& path,
    nw::StringView gff_serial_id, nw::ObjectType object_type);
nw::String preview_creature_name_from_file(const std::filesystem::path& path);
nw::String preview_area_name_from_file(const std::filesystem::path& path);

} // namespace nwn1
