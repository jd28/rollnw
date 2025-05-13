#pragma once

#include "../i18n/LocString.hpp"
#include "../resources/assets.hpp"
#include "LocalData.hpp"
#include "Location.hpp"
#include "ObjectBase.hpp"

#include <stduuid/uuid.h>

#include <string>

namespace nw {

struct Common {
    bool from_json(const nlohmann::json& archive, SerializationProfile profile, ObjectType object_type);
    nlohmann::json to_json(SerializationProfile profile, ObjectType object_type) const;

    Common();
    explicit Common(nw::MemoryResource* allocator);

    /// Clears all data members and resets to default constructed.
    void clear();

    /// Determines if common component was successfully parsed
    bool valid() { return valid_; }

    uuids::uuid uuid;
    Resref resref;
    InternedString tag;
    LocString name;

    LocalData locals;
    Location location;

    // Blueprint only
    String comment;
    uint8_t palette_id = std::numeric_limits<uint8_t>::max();

protected:
    bool valid_ = false;
};

bool deserialize(Common& self, const GffStruct& archive, SerializationProfile profile, ObjectType object_type);

} // namespace nw
