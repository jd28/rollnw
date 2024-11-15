#include "VisualTransform.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

void from_json(const nlohmann::json& archive, VisualTransformValue& value)
{
    archive.at("lerp_type").get_to(value.lerp_type);
    archive.at("timer_type").get_to(value.timer_type);
    archive.at("value_to").get_to(value.value_to);
}

void to_json(nlohmann::json& archive, const VisualTransformValue& value)
{
    archive["lerp_type"] = value.lerp_type;
    archive["timer_type"] = value.timer_type;
    archive["value_to"] = value.value_to;
}

bool deserialize(const GffStruct gff, VisualTransformValue& self)
{
    return gff["LerpType"].get_to(self.lerp_type)
        && gff["TimerType"].get_to(self.timer_type)
        && gff["ValueTo"].get_to(self.value_to);
}

bool serialize(GffBuilderStruct& archive, const VisualTransformValue& self)
{
    archive.add_field("LerpType", self.lerp_type);
    archive.add_field("TimerType", self.timer_type);
    archive.add_field("ValueTo", self.value_to);
    return true;
}

void from_json(const nlohmann::json& archive, VisualTransform& value)
{
    archive.at("animation_speed").get_to(value.animation_speed);
    archive.at("rotate_x").get_to(value.rotate_x);
    archive.at("rotate_y").get_to(value.rotate_y);
    archive.at("rotate_z").get_to(value.rotate_z);
    archive.at("scale_x").get_to(value.scale_x);
    archive.at("scale_y").get_to(value.scale_y);
    archive.at("scale_z").get_to(value.scale_z);
    archive.at("transform_x").get_to(value.transform_x);
    archive.at("transform_y").get_to(value.transform_y);
    archive.at("transform_z").get_to(value.transform_z);
    archive.at("scope").get_to(value.scope);
}

void to_json(nlohmann::json& archive, const VisualTransform& value)
{
    archive["animation_speed"] = value.animation_speed;
    archive["rotate_x"] = value.rotate_x;
    archive["rotate_y"] = value.rotate_y;
    archive["rotate_z"] = value.rotate_z;
    archive["scale_x"] = value.scale_x;
    archive["scale_y"] = value.scale_y;
    archive["scale_z"] = value.scale_z;
    archive["transform_x"] = value.transform_x;
    archive["transform_y"] = value.transform_y;
    archive["transform_z"] = value.transform_z;
    archive["scope"] = value.scope;
}

bool deserialize(const GffStruct gff, VisualTransform& self)
{
    auto st = gff["AnimationSpeed"].get<GffStruct>();
    if (!st || !deserialize(*st, self.animation_speed)) { return false; }
    st = gff["RotateX"].get<GffStruct>();
    if (!st || !deserialize(*st, self.rotate_x)) { return false; }
    st = gff["RotateY"].get<GffStruct>();
    if (!st || !deserialize(*st, self.rotate_y)) { return false; }
    st = gff["RotateZ"].get<GffStruct>();
    if (!st || !deserialize(*st, self.rotate_z)) { return false; }
    st = gff["ScaleX"].get<GffStruct>();
    if (!st || !deserialize(*st, self.scale_x)) { return false; }
    st = gff["ScaleY"].get<GffStruct>();
    if (!st || !deserialize(*st, self.scale_y)) { return false; }
    st = gff["ScaleZ"].get<GffStruct>();
    if (!st || !deserialize(*st, self.scale_z)) { return false; }
    st = gff["TranslateX"].get<GffStruct>();
    if (!st || !deserialize(*st, self.transform_x)) { return false; }
    st = gff["TranslateY"].get<GffStruct>();
    if (!st || !deserialize(*st, self.transform_y)) { return false; }
    st = gff["TranslateZ"].get<GffStruct>();
    if (!st || !deserialize(*st, self.transform_z)) { return false; }
    gff.get_to("Scope", self.scope);
    return true;
}

bool serialize(GffBuilderStruct& archive, const VisualTransform& self)
{

    serialize(archive.add_struct("AnimationSpeed", 0), self.animation_speed);
    serialize(archive.add_struct("RotateX", 0), self.rotate_x);
    serialize(archive.add_struct("RotateY", 0), self.rotate_y);
    serialize(archive.add_struct("RotateZ", 0), self.rotate_z);
    serialize(archive.add_struct("ScaleX", 0), self.scale_x);
    serialize(archive.add_struct("ScaleY", 0), self.scale_y);
    serialize(archive.add_struct("ScaleZ", 0), self.scale_z);
    serialize(archive.add_struct("TranslateX", 0), self.transform_x);
    serialize(archive.add_struct("TranslateY", 0), self.transform_y);
    serialize(archive.add_struct("TranslateZ", 0), self.transform_z);
    archive.add_field("Scope", self.scope);
    return true;
}

} // namespace nw
