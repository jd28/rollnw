#include "Mdl.hpp"

#include "../kernel/Resources.hpp"
#include "../kernel/Strings.hpp"
#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/string.hpp"
#include "MdlBinaryParser.hpp"
#include "MdlTextParser.hpp"

namespace nw::model {

const std::unordered_map<std::string_view, std::pair<uint32_t, uint32_t>> ControllerType::map = {
    // Common
    {"position", {ControllerType::Position, NodeFlags::header}},
    {"orientation", {ControllerType::Orientation, NodeFlags::header}},
    {"scale", {ControllerType::Scale, NodeFlags::header}},
    {"wirecolor", {ControllerType::Wirecolor, NodeFlags::header}},
    // Light
    {"color", {ControllerType::Color, NodeFlags::light}},
    {"radius", {ControllerType::Radius, NodeFlags::light}},
    {"shadowradius", {ControllerType::ShadowRadius, NodeFlags::light}},
    {"verticaldisplacement", {ControllerType::VerticalDisplacement, NodeFlags::light}},
    {"multiplier", {ControllerType::Multiplier, NodeFlags::light}},
    // Emitter
    {"alphaEnd", {ControllerType::AlphaEnd, NodeFlags::emitter}},
    {"alphaStart", {ControllerType::AlphaStart, NodeFlags::emitter}},
    {"birthrate", {ControllerType::BirthRate, NodeFlags::emitter}},
    {"bounce_co", {ControllerType::Bounce_Co, NodeFlags::emitter}},
    {"colorEnd", {ControllerType::ColorEnd, NodeFlags::emitter}},
    {"colorStart", {ControllerType::ColorStart, NodeFlags::emitter}},
    {"combinetime", {ControllerType::CombineTime, NodeFlags::emitter}},
    {"drag", {ControllerType::Drag, NodeFlags::emitter}},
    {"fps", {ControllerType::FPS, NodeFlags::emitter}},
    {"frameEnd", {ControllerType::FrameEnd, NodeFlags::emitter}},
    {"frameStart", {ControllerType::FrameStart, NodeFlags::emitter}},
    {"grav", {ControllerType::Grav, NodeFlags::emitter}},
    {"lifeExp", {ControllerType::LifeExp, NodeFlags::emitter}},
    {"mass", {ControllerType::Mass, NodeFlags::emitter}},
    {"p2p_bezier2", {ControllerType::P2P_Bezier2, NodeFlags::emitter}},
    {"p2p_bezier3", {ControllerType::P2P_Bezier3, NodeFlags::emitter}},
    {"particleRot", {ControllerType::ParticleRot, NodeFlags::emitter}},
    {"randvel", {ControllerType::RandVel, NodeFlags::emitter}},
    {"sizeStart", {ControllerType::SizeStart, NodeFlags::emitter}},
    {"sizeEnd", {ControllerType::SizeEnd, NodeFlags::emitter}},
    {"sizeStart_y", {ControllerType::SizeStart_Y, NodeFlags::emitter}},
    {"sizeEnd_y", {ControllerType::SizeEnd_Y, NodeFlags::emitter}},
    {"spread", {ControllerType::Spread, NodeFlags::emitter}},
    {"threshold", {ControllerType::Threshold, NodeFlags::emitter}},
    {"velocity", {ControllerType::Velocity, NodeFlags::emitter}},
    {"xsize", {ControllerType::XSize, NodeFlags::emitter}},
    {"ysize", {ControllerType::YSize, NodeFlags::emitter}},
    {"blurlength", {ControllerType::BlurLength, NodeFlags::emitter}},
    {"lightningDelay", {ControllerType::LightningDelay, NodeFlags::emitter}},
    {"lightningRadius", {ControllerType::LightningRadius, NodeFlags::emitter}},
    {"lightningScale", {ControllerType::LightningScale, NodeFlags::emitter}},
    {"detonate", {ControllerType::Detonate, NodeFlags::emitter}},
    {"alphaMid", {ControllerType::AlphaMid, NodeFlags::emitter}},
    {"colorMid", {ControllerType::ColorMid, NodeFlags::emitter}},
    {"percentStart", {ControllerType::PercentStart, NodeFlags::emitter}},
    {"percentMid", {ControllerType::PercentMid, NodeFlags::emitter}},
    {"percentEnd", {ControllerType::PercentEnd, NodeFlags::emitter}},
    {"sizeMid", {ControllerType::SizeMid, NodeFlags::emitter}},
    {"sizeMid_y", {ControllerType::SizeMid_Y, NodeFlags::emitter}},
    {"lockAxes", {ControllerType::lock_axes, NodeFlags::emitter}},
    {"spawnType", {ControllerType::spawn_type, NodeFlags::emitter}},
    {"random", {ControllerType::random, NodeFlags::emitter}},
    {"inherit", {ControllerType::inherit, NodeFlags::emitter}},
    {"inherit_local", {ControllerType::inherit_local, NodeFlags::emitter}},
    // Meshes
    {"selfillumcolor", {ControllerType::SelfIllumColor, NodeFlags::mesh}},
    {"alpha", {ControllerType::Alpha, NodeFlags::mesh | NodeFlags::emitter}},
};

std::pair<uint32_t, uint32_t> ControllerType::lookup(std::string_view cont)
{
    // Common
    if (string::icmp(cont, "position")) {
        return {ControllerType::Position, NodeFlags::header};
    } else if (string::icmp(cont, "orientation")) {
        return {ControllerType::Orientation, NodeFlags::header};
    } else if (string::icmp(cont, "scale")) {
        return {ControllerType::Scale, NodeFlags::header};
    } else if (string::icmp(cont, "wirecolor")) {
        return {ControllerType::Wirecolor, NodeFlags::header};
    }
    // Light
    else if (string::icmp(cont, "color")) {
        return {ControllerType::Color, NodeFlags::light};
    } else if (string::icmp(cont, "radius")) {
        return {ControllerType::Radius, NodeFlags::light};
    } else if (string::icmp(cont, "shadowradius")) {
        return {ControllerType::ShadowRadius, NodeFlags::light};
    } else if (string::icmp(cont, "verticaldisplacement")) {
        return {ControllerType::VerticalDisplacement, NodeFlags::light};
    } else if (string::icmp(cont, "multiplier")) {
        return {ControllerType::Multiplier, NodeFlags::light};
    }
    // Emitter
    else if (string::icmp(cont, "alphaEnd")) {
        return {ControllerType::AlphaEnd, NodeFlags::emitter};
    } else if (string::icmp(cont, "alphaStart")) {
        return {ControllerType::AlphaStart, NodeFlags::emitter};
    } else if (string::icmp(cont, "birthrate")) {
        return {ControllerType::BirthRate, NodeFlags::emitter};
    } else if (string::icmp(cont, "bounce_co")) {
        return {ControllerType::Bounce_Co, NodeFlags::emitter};
    } else if (string::icmp(cont, "colorEnd")) {
        return {ControllerType::ColorEnd, NodeFlags::emitter};
    } else if (string::icmp(cont, "colorStart")) {
        return {ControllerType::ColorStart, NodeFlags::emitter};
    } else if (string::icmp(cont, "combinetime")) {
        return {ControllerType::CombineTime, NodeFlags::emitter};
    } else if (string::icmp(cont, "drag")) {
        return {ControllerType::Drag, NodeFlags::emitter};
    } else if (string::icmp(cont, "fps")) {
        return {ControllerType::FPS, NodeFlags::emitter};
    } else if (string::icmp(cont, "frameEnd")) {
        return {ControllerType::FrameEnd, NodeFlags::emitter};
    } else if (string::icmp(cont, "frameStart")) {
        return {ControllerType::FrameStart, NodeFlags::emitter};
    } else if (string::icmp(cont, "grav")) {
        return {ControllerType::Grav, NodeFlags::emitter};
    } else if (string::icmp(cont, "lifeExp")) {
        return {ControllerType::LifeExp, NodeFlags::emitter};
    } else if (string::icmp(cont, "mass")) {
        return {ControllerType::Mass, NodeFlags::emitter};
    } else if (string::icmp(cont, "p2p_bezier2")) {
        return {ControllerType::P2P_Bezier2, NodeFlags::emitter};
    } else if (string::icmp(cont, "p2p_bezier3")) {
        return {ControllerType::P2P_Bezier3, NodeFlags::emitter};
    } else if (string::icmp(cont, "particleRot")) {
        return {ControllerType::ParticleRot, NodeFlags::emitter};
    } else if (string::icmp(cont, "randvel")) {
        return {ControllerType::RandVel, NodeFlags::emitter};
    } else if (string::icmp(cont, "sizeStart")) {
        return {ControllerType::SizeStart, NodeFlags::emitter};
    } else if (string::icmp(cont, "sizeEnd")) {
        return {ControllerType::SizeEnd, NodeFlags::emitter};
    } else if (string::icmp(cont, "sizeStart_y")) {
        return {ControllerType::SizeStart_Y, NodeFlags::emitter};
    } else if (string::icmp(cont, "sizeEnd_y")) {
        return {ControllerType::SizeEnd_Y, NodeFlags::emitter};
    } else if (string::icmp(cont, "spread")) {
        return {ControllerType::Spread, NodeFlags::emitter};
    } else if (string::icmp(cont, "threshold")) {
        return {ControllerType::Threshold, NodeFlags::emitter};
    } else if (string::icmp(cont, "velocity")) {
        return {ControllerType::Velocity, NodeFlags::emitter};
    } else if (string::icmp(cont, "xsize")) {
        return {ControllerType::XSize, NodeFlags::emitter};
    } else if (string::icmp(cont, "ysize")) {
        return {ControllerType::YSize, NodeFlags::emitter};
    } else if (string::icmp(cont, "blurlength")) {
        return {ControllerType::BlurLength, NodeFlags::emitter};
    } else if (string::icmp(cont, "lightningDelay")) {
        return {ControllerType::LightningDelay, NodeFlags::emitter};
    } else if (string::icmp(cont, "lightningRadius")) {
        return {ControllerType::LightningRadius, NodeFlags::emitter};
    } else if (string::icmp(cont, "lightningScale")) {
        return {ControllerType::LightningScale, NodeFlags::emitter};
    } else if (string::icmp(cont, "lightningSubDiv")) {
        return {ControllerType::LightningSubDiv, NodeFlags::emitter};
    } else if (string::icmp(cont, "detonate")) {
        return {ControllerType::Detonate, NodeFlags::emitter};
    } else if (string::icmp(cont, "alphaMid")) {
        return {ControllerType::AlphaMid, NodeFlags::emitter};
    } else if (string::icmp(cont, "colorMid")) {
        return {ControllerType::ColorMid, NodeFlags::emitter};
    } else if (string::icmp(cont, "percentStart")) {
        return {ControllerType::PercentStart, NodeFlags::emitter};
    } else if (string::icmp(cont, "percentMid")) {
        return {ControllerType::PercentMid, NodeFlags::emitter};
    } else if (string::icmp(cont, "percentEnd")) {
        return {ControllerType::PercentEnd, NodeFlags::emitter};
    } else if (string::icmp(cont, "sizeMid")) {
        return {ControllerType::SizeMid, NodeFlags::emitter};
    } else if (string::icmp(cont, "sizeMid_y")) {
        return {ControllerType::SizeMid_Y, NodeFlags::emitter};
    } else if (string::icmp(cont, "lockAxes")) {
        return {ControllerType::lock_axes, NodeFlags::emitter};
    } else if (string::icmp(cont, "spawnType")) {
        return {ControllerType::spawn_type, NodeFlags::emitter};
    } else if (string::icmp(cont, "random")) {
        return {ControllerType::random, NodeFlags::emitter};
    } else if (string::icmp(cont, "inherit")) {
        return {ControllerType::inherit, NodeFlags::emitter};
    } else if (string::icmp(cont, "inherit_local")) {
        return {ControllerType::inherit_local, NodeFlags::emitter};
    }
    // Meshes
    else if (string::icmp(cont, "selfillumcolor")) {
        return {ControllerType::SelfIllumColor, NodeFlags::mesh};
    } else if (string::icmp(cont, "alpha")) {
        return {ControllerType::Alpha, NodeFlags::mesh | NodeFlags::emitter};
    }
    return {0, 0};
}

// -- Nodes -------------------------------------------------------------------

Node::Node(std::string name_, uint32_t type_)
    : name{name_}
    , type{type_}
{
}

void Node::add_controller_data(std::string_view name_, uint32_t type_, std::vector<float> times_,
    std::vector<float> data_, int rows_, int columns_)
{
    ControllerKey k{
        nw::kernel::strings().intern(name_),
        type_,
        rows_,
        static_cast<int>(controller_keys.size()),
        static_cast<int>(controller_data.size()),
        static_cast<int>(controller_data.size() + times_.size()),
        columns_,
        string::endswith(name_, "key"),
    };

    controller_keys.push_back(k);

    for (float f : times_) {
        controller_data.push_back(f);
    }

    // Four columns is an axis-angle (in radians) rotation, convert to quaternion
    if (columns_ == 4) {
        for (size_t i = 0; i < data_.size(); i += 4) {
            auto quat = glm::angleAxis(data_[i + 3], glm::vec3{data_[i + 0], data_[i + 1], data_[i + 2]});
            controller_data.push_back(quat.x);
            controller_data.push_back(quat.y);
            controller_data.push_back(quat.z);
            controller_data.push_back(quat.w);
        }
    } else {
        for (float f : data_) {
            controller_data.push_back(f);
        }
    }
}

ControllerValue Node::get_controller(uint32_t type_, bool key) const
{
    std::span<const float> time;
    std::span<const float> data;
    for (const auto& c : controller_keys) {
        if (c.type == type_) {
            if ((key && !c.is_key) || c.columns == -1) { continue; }
            time = {&controller_data[c.time_offset], static_cast<size_t>(c.rows)};
            data = {&controller_data[c.data_offset], static_cast<size_t>(c.rows * c.columns)};
            return {&c, time, data};
        }
    }
    return {nullptr, time, data};
}

AABBNode::AABBNode(std::string name_)
    : TrimeshNode(std::move(name_), NodeType::aabb)
{
}

AnimeshNode::AnimeshNode(std::string name_)
    : TrimeshNode(std::move(name_), NodeType::animmesh)
{
}

CameraNode::CameraNode(std::string name_)
    : Node(std::move(name_), NodeType::camera)
{
}

DanglymeshNode::DanglymeshNode(std::string name_)
    : TrimeshNode(std::move(name_), NodeType::danglymesh)
{
}

DummyNode::DummyNode(std::string name_)
    : Node(std::move(name_), NodeType::dummy)
{
}

EmitterNode::EmitterNode(std::string name_)
    : Node(std::move(name_), NodeType::emitter)
{
}

LightNode::LightNode(std::string name_)
    : Node(std::move(name_), NodeType::light)
{
}

PatchNode::PatchNode(std::string name_)
    : Node(std::move(name_), NodeType::patch)
{
}

ReferenceNode::ReferenceNode(std::string name_)
    : Node(std::move(name_), NodeType::reference)
    , reattachable{false}
{
}

SkinNode::SkinNode(std::string name_)
    : TrimeshNode(std::move(name_), NodeType::skin)
{
    for (size_t i = 0; i < 64; ++i) {
        bone_nodes[i] = -1;
    }
}

TrimeshNode::TrimeshNode(std::string name_, uint32_t type_)
    : Node(std::move(name_), type_)
    , diffuse{0.8, 0.8, 0.8}
{
}

// -- Geometry ----------------------------------------------------------------

Geometry::Geometry(GeometryType type_)
    : type{type_}
{
}

Node* Geometry::find(const std::regex& re)
{
    for (auto& node : nodes) {
        if (std::regex_match(node->name, re)) {
            return node.get();
        }
    }
    return nullptr;
}

const Node* Geometry::find(const std::regex& re) const
{
    return const_cast<Geometry*>(this)->find(re);
}

Model::Model()
    : Geometry(GeometryType::model)
    , classification{ModelClass::invalid}
    , ignorefog{1}
    , bmin{-5.0f, -5.0f, -1.0f}
    , bmax{5.0f, 5.0f, 10.0f}
    , radius{7.0f}
    , animationscale{1.0f}
{
}

Animation* Model::find_animation(std::string_view name)
{
    for (auto& node : animations) {
        if (string::icmp(node->name, name)) {
            return node.get();
        }
    }
    return nullptr;
}

const Animation* Model::find_animation(std::string_view name) const
{
    return const_cast<Model*>(this)->find_animation(name);
}

Animation::Animation(std::string name_)
    : Geometry(GeometryType::animation)
{
    name = name_;
}

// --  -------------------------------------------------------------------

Mdl::Mdl(const std::filesystem::path& filename)
    : Mdl{ResourceData::from_file(filename)}
{
}

Mdl::Mdl(ResourceData data)
    : data_{std::move(data)}
{
    if (data_.bytes.size() == 0) {
        LOG_F(ERROR, "[model] no data received");
        return;
    }
    if (data_.bytes[0] == 0) {
        BinaryParser p{std::span<uint8_t>(data_.bytes.data(), data_.bytes.size()), this};
        loaded_ = p.parse();
    } else {
        try {
            TextParser p{data_.bytes.string_view(), this};
            loaded_ = p.parse();
        } catch (std::exception& e) {
            LOG_F(ERROR, "failed to parse model <unknown>: {}", e.what());
            loaded_ = false;
        }
    }

    if (!model.supermodel_name.empty() && !string::icmp(model.supermodel_name, "null")) {
        auto b = nw::kernel::resman().demand({model.supermodel_name, ResourceType::mdl});
        if (b.bytes.size()) {
            LOG_F(INFO, "[model] loading super model: {}", model.supermodel_name);
            model.supermodel = std::make_unique<Mdl>(std::move(b));
        }
    }
}

std::unique_ptr<Node> Mdl::make_node(uint32_t type, std::string_view name)
{
    switch (type) {
    default:
        LOG_F(ERROR, "Invalid node type: {}, name: {}", type, name);
        return {};
    case NodeType::dummy:
        return std::make_unique<DummyNode>(std::string(name));
    case NodeType::patch:
        return std::make_unique<PatchNode>(std::string(name));
    case NodeType::reference:
        return std::make_unique<ReferenceNode>(std::string(name));
    case NodeType::trimesh:
        return std::make_unique<TrimeshNode>(std::string(name));
    case NodeType::danglymesh:
        return std::make_unique<DanglymeshNode>(std::string(name));
    case NodeType::skin:
        return std::make_unique<SkinNode>(std::string(name));
    case NodeType::animmesh:
        return std::make_unique<AnimeshNode>(std::string(name));
    case NodeType::emitter:
        return std::make_unique<EmitterNode>(std::string(name));
    case NodeType::light:
        return std::make_unique<LightNode>(std::string(name));
    case NodeType::aabb:
        return std::make_unique<AABBNode>(std::string(name));
    case NodeType::camera:
        return std::make_unique<CameraNode>(std::string(name));
    }
}

bool Mdl::valid() const
{
    return loaded_;
}

// void write_out(std::ostream& out, const DanglymeshNode* node, bool is_anim)
// {
//     if (!is_anim) {
//         out << fmt::format("  displacement {}\n", node->displacement);
//         out << fmt::format("  period {}\n", node->period);
//         out << fmt::format("  tightness {}\n", node->tightness);
//         out << fmt::format("  tightness {}\n", node->tightness);
//         out << fmt::format("  constraints {}\n", node->constraints.size());
//         for (auto c : node->constraints) {
//             out << fmt::format("    {}\n", c);
//         }
//     }
// }

// void write_out(std::ostream& out, const EmitterNode* node, bool is_anim)
// {
//     if (!is_anim) {
//         out << fmt::format("  blastlength {}\n", node->blastlength);
//         out << fmt::format("  blastradius {}\n", node->blastradius);
//         out << fmt::format("  blend {}\n", node->blend);
//         out << fmt::format("  chunkname {}\n", node->chunkname);
//         out << fmt::format("  deadspace {}\n", node->deadspace);
//         out << fmt::format("  loop {}\n", node->loop);
//         out << fmt::format("  render {}\n", node->render);
//         out << fmt::format("  renderorder {}\n", node->renderorder);
//         out << fmt::format("  spawntype {}\n", node->spawntype);
//         out << fmt::format("  texture {}\n", node->texture);
//         out << fmt::format("  twosidedtex {}\n", node->twosidedtex);
//         out << fmt::format("  update {}\n", node->update);
//         out << fmt::format("  xgrid {}\n", node->xgrid);
//         out << fmt::format("  ygrid {}\n", node->ygrid);
//         out << fmt::format("  render_sel {}\n", node->render_sel);
//         out << fmt::format("  blend_sel {}\n", node->blend_sel);
//         out << fmt::format("  update_sel {}\n", node->update_sel);
//         out << fmt::format("  spawntype_sel {}\n", node->spawntype_sel);
//         out << fmt::format("  opacity {}\n", node->opacity);
//         out << fmt::format("  p2p_type {}\n", node->p2p_type);

//         out << fmt::format("  affectedByWind {}\n", node->flags & EmitterFlag::AffectedByWind ? 1 : 0);
//         out << fmt::format("  bounce {}\n", node->flags & EmitterFlag::Bounce ? 1 : 0);
//         out << fmt::format("  inherit {}\n", node->flags & EmitterFlag::Inherit ? 1 : 0);
//         out << fmt::format("  inherit_local {}\n", node->flags & EmitterFlag::InheritLocal ? 1 : 0);
//         out << fmt::format("  inherit_part {}\n", node->flags & EmitterFlag::InheritPart ? 1 : 0);
//         out << fmt::format("  inheritvel {}\n", node->flags & EmitterFlag::InheritVel ? 1 : 0);
//         out << fmt::format("  m_isTinted {}\n", node->flags & EmitterFlag::IsTinted ? 1 : 0);
//         out << fmt::format("  p2p {}\n", node->flags & EmitterFlag::P2P ? 1 : 0);
//         out << fmt::format("  p2p_sel {}\n", node->flags & EmitterFlag::P2PSel ? 1 : 0);
//         out << fmt::format("  random {}\n", node->flags & EmitterFlag::Random ? 1 : 0);
//         out << fmt::format("  splat {}\n", node->flags & EmitterFlag::Splat ? 1 : 0);
//     }
//     for (const auto& [k, v] : ControllerType::map) {
//         if (v.second == NodeFlags::emitter) {
//             auto [ckey, cdata] = node->get_controller(v.first);
//             if (!ckey || cdata.empty()) continue;
//             if (!is_anim && string::endswith(ckey->name.view(), "key")) {
//                 continue;
//             }
//             out << fmt::format("         {} ", ckey->name);
//             if (ckey->columns > 0) {
//                 for (size_t i = 0; i < size_t(ckey->rows); ++i) {
//                     out << "          ";
//                     size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
//                     for (size_t j = start; j < stop; ++j) {
//                         out << fmt::format(" {: >8.5f}", cdata[j]);
//                     }
//                     out << '\n';
//                 }
//             }
//             out << "         endlist";
//         }
//     }
// }

// void write_out(std::ostream& out, const LightNode* node, bool is_anim)
// {
//     if (!is_anim) {
//         out << fmt::format("  ambientonly {}\n", node->ambientonly);
//         out << fmt::format("  ndynamictype {}\n", int(node->dynamic));
//         out << fmt::format("  affectdynamic {}\n", node->affectdynamic);
//         out << fmt::format("  shadow {}\n", node->shadow);
//         out << fmt::format("  lightpriority {}\n", node->lightpriority);
//         out << fmt::format("  fadingLight {}\n", node->fadinglight);
//         out << fmt::format("  radius {}\n", node->flareradius);
//         out << fmt::format("  multiplier {}\n", node->multiplier);
//         out << fmt::format("  color {:3.2f} {:3.2f} {:3.2f}\n", node->color.x, node->color.y, node->color.z);
//     }
//     for (const auto& [k, v] : ControllerType::map) {
//         if (v.second == NodeFlags::light) {
//             auto [ckey, cdata] = node->get_controller(v.first);
//             if (!ckey || cdata.empty()) continue;
//             out << fmt::format("        {}\n", ckey->name);
//             if (ckey->columns > 0) {
//                 for (size_t i = 0; i < size_t(ckey->rows); ++i) {
//                     out << "          ";
//                     size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
//                     for (size_t j = start; j < stop; ++j) {
//                         out << fmt::format(" {: >8.5f}", cdata[j]);
//                     }
//                     out << '\n';
//                 }
//             }
//             out << "        endlist\n";
//         }
//     }
// }

// void write_out(std::ostream& out, const SkinNode* node, bool is_anim)
// {
//     if (!is_anim) {
//         out << fmt::format("  weights {}\n", node->weights.size());
//         for (const auto& w : node->weights) {
//             for (size_t i = 0; i < 4; ++i) {
//                 if (w.bones[i].empty()) break;
//                 out << fmt::format("    {} {}", w.bones[i], w.weights[i]);
//             }
//         }
//     }
// }

// void write_out(std::ostream& out, const TrimeshNode* node, ModelClass class_, bool is_anim)
// {
//     if (!is_anim) {
//         auto [alpha_key, alpha_data] = node->get_controller(ControllerType::Alpha);
//         auto [selfilum_key, selfilum_data] = node->get_controller(ControllerType::SelfIllumColor);

//         if (alpha_key) {
//             out << fmt::format("  alpha {}\n", alpha_data[0]);
//         }

//         if (selfilum_key) {
//             out << fmt::format("  selfillumcolor {:.2f} {:.2f} {:.2f}\n", selfilum_data[0], selfilum_data[1], selfilum_data[2]);
//         }

//         out << fmt::format("  ambient {:.2f} {:.2f} {:.2f}\n", node->ambient.x, node->ambient.y, node->ambient.z);
//         out << fmt::format("  diffuse {:.2f} {:.2f} {:.2f}\n", node->diffuse.x, node->diffuse.y, node->diffuse.z);
//         out << fmt::format("  specular {:.2f} {:.2f} {:.2f}\n", node->specular.x, node->specular.y, node->specular.z);

//         out << fmt::format("  bitmap {}\n", node->bitmap);
//         out << fmt::format("  shininess {}\n", node->shininess);

//         out << fmt::format("  render {}\n", int(node->render));
//         out << fmt::format("  shadow {}\n", int(node->shadow));
//         out << fmt::format("  beaming {}\n", int(node->beaming));
//         out << fmt::format("  transparencyhint {}\n", int(node->transparencyhint));

//         if (class_ == ModelClass::tile
//             || class_ == ModelClass::invalid
//             || class_ == ModelClass::character) {
//             out << fmt::format("  tilefade {}\n", node->tilefade);
//         }

//         if (class_ == ModelClass::tile) {
//             out << fmt::format("  rotatetexture {}\n", int(node->rotatetexture));
//         }

//         if (node->vertices.size()) {
//             out << fmt::format("  verts {}\n", node->vertices.size());
//             for (const auto& v : node->vertices) {
//                 out << fmt::format("    {: >8.5f} {: >8.5f} {: >8.5f}\n", v.position.x, v.position.y, v.position.z);
//             }
//         }

//         if (node->colors.size()) {
//             out << fmt::format("  colors {}\n", node->colors.size());
//             for (const auto& c : node->colors) {
//                 out << fmt::format("    {:.4f} {:.4f} {:.4f}\n", c.x, c.y, c.z);
//             }
//         }

//         if (node->indices.size()) {
//             out << fmt::format("  faces {}\n", node->indices.size() / 3);
//             for (size_t i = 0; i < node->indices.size(); i += 3) {
//                 out << fmt::format("    {: >3} {: >3} {: >3} {: >2}  {: >3} {: >3} {: >3} {}\n",
//                     node->indices[i], node->indices[i + 1], node->indices[i + 2], 1,
//                     node->indices[i], node->indices[i + 1], node->indices[i + 2], 1);
//             }
//         }

//         if (node->vertices.size()) {
//             out << fmt::format("  tverts {}\n", node->vertices.size());
//             for (const auto& v : node->vertices) {
//                 out << fmt::format("    {: >8.5f} {: >8.5f} 0\n", v.tex_coords.x, v.tex_coords.y);
//             }
//         }
//     } else {
//         for (const auto& [k, v] : ControllerType::map) {
//             if (v.second == NodeFlags::mesh) {
//                 auto [ckey, cdata] = node->get_controller(v.first);
//                 if (!ckey || cdata.empty()) continue;
//                 out << fmt::format("        {}\n", ckey->name);
//                 if (ckey->columns > 0) {
//                     for (size_t i = 0; i < size_t(ckey->rows); ++i) {
//                         out << "          ";
//                         size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
//                         for (size_t j = start; j < stop; ++j) {
//                             out << fmt::format(" {: >8.5f}", cdata[j]);
//                         }
//                         out << '\n';
//                     }
//                 }
//                 out << "        endlist\n";
//             }
//         }
//     }
// }

// std::ostream& operator<<(std::ostream& out, const Mdl& mdl)
// {
//     out << "# Exported from rollnw\n"
//         << fmt::format("filedependancy {}\n",
//                mdl.model.file_dependency.size() ? mdl.model.file_dependency : "Unknown")
//         << fmt::format("newmodel {}\n", mdl.model.name)
//         << fmt::format("setsupermodel {} {}\n", mdl.model.name, mdl.model.supermodel_name)
//         << fmt::format("classification {}\n", model_class_to_string(mdl.model.classification))
//         << fmt::format("setanimationscale {:.2f}\n", mdl.model.animationscale)
//         << fmt::format("beginmodelgeom {}\n", mdl.model.name);

//     for (const auto& node : mdl.model.nodes) {
//         out << fmt::format("node {} {}\n", NodeType::to_string(node->type), node->name);

//         auto [pos_key, pos_data] = node->get_controller(ControllerType::Position);
//         auto [ori_key, ori_data] = node->get_controller(ControllerType::Orientation);
//         auto [scale_key, scale_data] = node->get_controller(ControllerType::Scale);
//         auto [wire_key, wire_data] = node->get_controller(ControllerType::Wirecolor);

//         out << fmt::format("  parent {}\n", node->parent ? node->parent->name : "null");
//         if (!node->parent) {
//             out << "endnode\n";
//             continue;
//         }

//         out << fmt::format("  position {: >8.5f} {: >8.5f} {: >8.5f}\n", pos_data[0], pos_data[1], pos_data[2]);
//         out << fmt::format("  orientation {: >8.5f} {: >8.5f} {: >8.5f} {: >8.5f}\n", ori_data[0], ori_data[1], ori_data[2], ori_data[3]);
//         out << fmt::format("  wirecolor {:.2f} {:.2f} {:.2f}\n", wire_data[0], wire_data[1], wire_data[2]);

//         if (scale_key) {
//             out << fmt::format("  scale {:.2f}\n", scale_data[0]);
//         }

//         if (dynamic_cast<AABBNode*>(node.get())) {
//             auto n = static_cast<AABBNode*>(node.get());
//             write_out(out, static_cast<const TrimeshNode*>(n), mdl.model.classification, false);
//         } else if (dynamic_cast<AnimeshNode*>(node.get())) {
//             auto n = static_cast<AnimeshNode*>(node.get());
//             write_out(out, static_cast<const TrimeshNode*>(n), mdl.model.classification, false);
//         } else if (dynamic_cast<DanglymeshNode*>(node.get())) {
//             write_out(out, static_cast<const TrimeshNode*>(node.get()), mdl.model.classification, false);
//             write_out(out, static_cast<DanglymeshNode*>(node.get()), false);
//         } else if (dynamic_cast<EmitterNode*>(node.get())) {
//             write_out(out, static_cast<EmitterNode*>(node.get()), false);
//         } else if (dynamic_cast<LightNode*>(node.get())) {
//             write_out(out, static_cast<LightNode*>(node.get()), false);
//         } else if (dynamic_cast<ReferenceNode*>(node.get())) {
//             auto n = static_cast<ReferenceNode*>(node.get());
//             out << fmt::format("  refmodel {}\n", n->refmodel);
//             out << fmt::format("  reattachable {}\n", int(n->reattachable));
//         } else if (dynamic_cast<SkinNode*>(node.get())) {
//             auto n = static_cast<SkinNode*>(node.get());
//             write_out(out, static_cast<const TrimeshNode*>(n), mdl.model.classification, false);
//             write_out(out, static_cast<const SkinNode*>(n), false);
//         } else if (dynamic_cast<TrimeshNode*>(node.get())) {
//             auto n = static_cast<TrimeshNode*>(node.get());
//             write_out(out, n, mdl.model.classification, false);
//         }

//         out << "endnode\n";
//     }

//     out << fmt::format("endmodelgeom {}\n\n", mdl.model.name);

//     if (mdl.model.animations.size()) {
//         for (const auto& anim : mdl.model.animations) {
//             out << "#ANIM ASCII\n";
//             out << fmt::format("newanim {} {}\n", anim->name, mdl.model.name);
//             out << fmt::format("  length {}\n", anim->length);
//             out << fmt::format("  transtime {}\n", anim->transition_time);
//             out << fmt::format("  animroot {}\n", anim->anim_root);

//             for (const auto& ev : anim->events) {
//                 out << fmt::format("  event {:8.5f} {}\n", ev.time, ev.name);
//             }

//             for (const auto& node : anim->nodes) {
//                 out << fmt::format("node {} {}\n", NodeType::to_string(node->type), node->name);

//                 out << fmt::format("  parent {}\n", node->parent ? node->parent->name : "null");
//                 if (!node->parent) {
//                     out << "endnode\n";
//                     continue;
//                 }

//                 for (const auto& [k, v] : ControllerType::map) {
//                     if (v.second == NodeFlags::header) {
//                         auto [ckey, cdata] = node->get_controller(v.first);
//                         if (!ckey || cdata.empty()) continue;
//                         out << fmt::format("        {}\n", ckey->name);
//                         if (ckey->columns > 0) {
//                             for (size_t i = 0; i < size_t(ckey->rows); ++i) {
//                                 out << "          ";
//                                 size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
//                                 for (size_t j = start; j < stop; ++j) {
//                                     out << fmt::format(" {: >8.5f}", cdata[j]);
//                                 }
//                                 out << '\n';
//                             }
//                         }
//                         out << "        endlist\n";
//                     }
//                 }

//                 if (dynamic_cast<AABBNode*>(node.get())) {
//                     auto n = static_cast<AABBNode*>(node.get());
//                     write_out(out, static_cast<const TrimeshNode*>(n), mdl.model.classification, true);
//                 } else if (dynamic_cast<AnimeshNode*>(node.get())) {
//                     auto n = static_cast<AnimeshNode*>(node.get());
//                     write_out(out, static_cast<const TrimeshNode*>(n), mdl.model.classification, true);
//                 } else if (dynamic_cast<DanglymeshNode*>(node.get())) {
//                     write_out(out, static_cast<const TrimeshNode*>(node.get()), mdl.model.classification, true);
//                     write_out(out, static_cast<DanglymeshNode*>(node.get()), true);
//                 } else if (dynamic_cast<EmitterNode*>(node.get())) {
//                     write_out(out, static_cast<EmitterNode*>(node.get()), true);
//                 } else if (dynamic_cast<LightNode*>(node.get())) {
//                     write_out(out, static_cast<LightNode*>(node.get()), true);
//                 } else if (dynamic_cast<ReferenceNode*>(node.get())) {
//                     // auto n = static_cast<ReferenceNode*>(node.get());
//                 } else if (dynamic_cast<SkinNode*>(node.get())) {
//                     auto n = static_cast<SkinNode*>(node.get());
//                     write_out(out, static_cast<const TrimeshNode*>(n), mdl.model.classification, true);
//                     write_out(out, static_cast<const SkinNode*>(n), true);
//                 } else if (dynamic_cast<TrimeshNode*>(node.get())) {
//                     auto n = static_cast<TrimeshNode*>(node.get());
//                     write_out(out, n, mdl.model.classification, true);
//                 }
//                 out << "endnode\n";
//             }
//             out << fmt::format("doneanim {} {}\n\n", anim->name, mdl.model.name);
//         }
//     }

//     out << fmt::format("donemodel {}\n", mdl.model.name);

//     return out;
// }

} // namespace nw::model
