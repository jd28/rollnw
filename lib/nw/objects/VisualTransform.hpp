#pragma once

#include <nlohmann/json_fwd.hpp>

namespace nw {

struct GffStruct;
struct GffBuilderStruct;

struct VisualTransformValue {
    int lerp_type = 0;
    int timer_type = 0;
    float value_to = 0.0f;

    bool operator==(const VisualTransformValue&) const noexcept = default;
    auto operator<=>(const VisualTransformValue&) const noexcept = default;
};

void from_json(const nlohmann::json& archive, VisualTransformValue& value);
void to_json(nlohmann::json& archive, const VisualTransformValue& value);

bool deserialize(const GffStruct gff, VisualTransformValue& self);
bool serialize(GffBuilderStruct& archive, const VisualTransformValue& self);

struct VisualTransform {
    VisualTransformValue animation_speed;
    VisualTransformValue rotate_x;
    VisualTransformValue rotate_y;
    VisualTransformValue rotate_z;
    VisualTransformValue scale_x;
    VisualTransformValue scale_y;
    VisualTransformValue scale_z;
    VisualTransformValue transform_x;
    VisualTransformValue transform_y;
    VisualTransformValue transform_z;
    int scope = 0;

    bool operator==(const VisualTransform&) const noexcept = default;
    auto operator<=>(const VisualTransform&) const noexcept = default;
};

void from_json(const nlohmann::json& archive, VisualTransform& value);
void to_json(nlohmann::json& archive, const VisualTransform& value);

bool deserialize(const GffStruct gff, VisualTransform& self);
bool serialize(GffBuilderStruct& archive, const VisualTransform& self);

} // namespace nw
