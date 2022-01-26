#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"

#include <string>

namespace nw {

struct Waypoint : public ObjectBase {
    Waypoint();
    Waypoint(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Waypoint(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;

    // ObjectBase overrides
    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Waypoint* as_waypoint() override { return this; }
    virtual const Waypoint* as_waypoint() const override { return this; }

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    LocString description;
    std::string linked_to;
    LocString map_note;

    uint8_t appearance;
    bool has_map_note = false;
    bool map_note_enabled = false;

private:
    bool valid_ = false;
};

} // namespace nw
