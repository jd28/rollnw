#include "Mdl.hpp"

#include "../kernel/Strings.hpp"
#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/string.hpp"
#include "MdlTextParser.hpp"

namespace nw {

const std::unordered_map<std::string_view, std::pair<uint32_t, uint32_t>> MdlControllerType::map = {
    // Common
    {"position", {MdlControllerType::Position, MdlNodeFlags::header}},
    {"orientation", {MdlControllerType::Orientation, MdlNodeFlags::header}},
    {"scale", {MdlControllerType::Scale, MdlNodeFlags::header}},
    {"wirecolor", {MdlControllerType::Wirecolor, MdlNodeFlags::header}},
    // Light
    {"color", {MdlControllerType::Color, MdlNodeFlags::light}},
    {"radius", {MdlControllerType::Radius, MdlNodeFlags::light}},
    {"shadowradius", {MdlControllerType::ShadowRadius, MdlNodeFlags::light}},
    {"verticaldisplacement", {MdlControllerType::VerticalDisplacement, MdlNodeFlags::light}},
    {"multiplier", {MdlControllerType::Multiplier, MdlNodeFlags::light}},
    // Emitter
    {"alphaEnd", {MdlControllerType::AlphaEnd, MdlNodeFlags::emitter}},
    {"alphaStart", {MdlControllerType::AlphaStart, MdlNodeFlags::emitter}},
    {"birthrate", {MdlControllerType::BirthRate, MdlNodeFlags::emitter}},
    {"bounce_co", {MdlControllerType::Bounce_Co, MdlNodeFlags::emitter}},
    {"colorEnd", {MdlControllerType::ColorEnd, MdlNodeFlags::emitter}},
    {"colorStart", {MdlControllerType::ColorStart, MdlNodeFlags::emitter}},
    {"combinetime", {MdlControllerType::CombineTime, MdlNodeFlags::emitter}},
    {"drag", {MdlControllerType::Drag, MdlNodeFlags::emitter}},
    {"fps", {MdlControllerType::FPS, MdlNodeFlags::emitter}},
    {"frameEnd", {MdlControllerType::FrameEnd, MdlNodeFlags::emitter}},
    {"frameStart", {MdlControllerType::FrameStart, MdlNodeFlags::emitter}},
    {"grav", {MdlControllerType::Grav, MdlNodeFlags::emitter}},
    {"lifeExp", {MdlControllerType::LifeExp, MdlNodeFlags::emitter}},
    {"mass", {MdlControllerType::Mass, MdlNodeFlags::emitter}},
    {"p2p_bezier2", {MdlControllerType::P2P_Bezier2, MdlNodeFlags::emitter}},
    {"p2p_bezier3", {MdlControllerType::P2P_Bezier3, MdlNodeFlags::emitter}},
    {"particleRot", {MdlControllerType::ParticleRot, MdlNodeFlags::emitter}},
    {"randvel", {MdlControllerType::RandVel, MdlNodeFlags::emitter}},
    {"sizeStart", {MdlControllerType::SizeStart, MdlNodeFlags::emitter}},
    {"sizeEnd", {MdlControllerType::SizeEnd, MdlNodeFlags::emitter}},
    {"sizeStart_y", {MdlControllerType::SizeStart_Y, MdlNodeFlags::emitter}},
    {"sizeEnd_y", {MdlControllerType::SizeEnd_Y, MdlNodeFlags::emitter}},
    {"spread", {MdlControllerType::Spread, MdlNodeFlags::emitter}},
    {"threshold", {MdlControllerType::Threshold, MdlNodeFlags::emitter}},
    {"velocity", {MdlControllerType::Velocity, MdlNodeFlags::emitter}},
    {"xsize", {MdlControllerType::XSize, MdlNodeFlags::emitter}},
    {"ysize", {MdlControllerType::YSize, MdlNodeFlags::emitter}},
    {"blurlength", {MdlControllerType::BlurLength, MdlNodeFlags::emitter}},
    {"lightningDelay", {MdlControllerType::LightningDelay, MdlNodeFlags::emitter}},
    {"lightningRadius", {MdlControllerType::LightningRadius, MdlNodeFlags::emitter}},
    {"lightningScale", {MdlControllerType::LightningScale, MdlNodeFlags::emitter}},
    {"detonate", {MdlControllerType::Detonate, MdlNodeFlags::emitter}},
    {"alphaMid", {MdlControllerType::AlphaMid, MdlNodeFlags::emitter}},
    {"colorMid", {MdlControllerType::ColorMid, MdlNodeFlags::emitter}},
    {"percentStart", {MdlControllerType::PercentStart, MdlNodeFlags::emitter}},
    {"percentMid", {MdlControllerType::PercentMid, MdlNodeFlags::emitter}},
    {"percentEnd", {MdlControllerType::PercentEnd, MdlNodeFlags::emitter}},
    {"sizeMid", {MdlControllerType::SizeMid, MdlNodeFlags::emitter}},
    {"sizeMid_y", {MdlControllerType::SizeMid_Y, MdlNodeFlags::emitter}},
    // Meshes
    {"selfillumcolor", {MdlControllerType::SelfIllumColor, MdlNodeFlags::mesh}},
    {"alpha", {MdlControllerType::Alpha, MdlNodeFlags::mesh | MdlNodeFlags::emitter}},
};

std::pair<uint32_t, uint32_t> MdlControllerType::lookup(std::string_view cont)
{
    // Common
    if (string::icmp(cont, "position")) {
        return {MdlControllerType::Position, MdlNodeFlags::header};
    } else if (string::icmp(cont, "orientation")) {
        return {MdlControllerType::Orientation, MdlNodeFlags::header};
    } else if (string::icmp(cont, "scale")) {
        return {MdlControllerType::Scale, MdlNodeFlags::header};
    } else if (string::icmp(cont, "wirecolor")) {
        return {MdlControllerType::Wirecolor, MdlNodeFlags::header};
    }
    // Light
    else if (string::icmp(cont, "color")) {
        return {MdlControllerType::Color, MdlNodeFlags::light};
    } else if (string::icmp(cont, "radius")) {
        return {MdlControllerType::Radius, MdlNodeFlags::light};
    } else if (string::icmp(cont, "shadowradius")) {
        return {MdlControllerType::ShadowRadius, MdlNodeFlags::light};
    } else if (string::icmp(cont, "verticaldisplacement")) {
        return {MdlControllerType::VerticalDisplacement, MdlNodeFlags::light};
    } else if (string::icmp(cont, "multiplier")) {
        return {MdlControllerType::Multiplier, MdlNodeFlags::light};
    }
    // Emitter
    else if (string::icmp(cont, "alphaEnd")) {
        return {MdlControllerType::AlphaEnd, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "alphaStart")) {
        return {MdlControllerType::AlphaStart, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "birthrate")) {
        return {MdlControllerType::BirthRate, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "bounce_co")) {
        return {MdlControllerType::Bounce_Co, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "colorEnd")) {
        return {MdlControllerType::ColorEnd, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "colorStart")) {
        return {MdlControllerType::ColorStart, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "combinetime")) {
        return {MdlControllerType::CombineTime, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "drag")) {
        return {MdlControllerType::Drag, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "fps")) {
        return {MdlControllerType::FPS, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "frameEnd")) {
        return {MdlControllerType::FrameEnd, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "frameStart")) {
        return {MdlControllerType::FrameStart, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "grav")) {
        return {MdlControllerType::Grav, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "lifeExp")) {
        return {MdlControllerType::LifeExp, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "mass")) {
        return {MdlControllerType::Mass, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "p2p_bezier2")) {
        return {MdlControllerType::P2P_Bezier2, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "p2p_bezier3")) {
        return {MdlControllerType::P2P_Bezier3, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "particleRot")) {
        return {MdlControllerType::ParticleRot, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "randvel")) {
        return {MdlControllerType::RandVel, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "sizeStart")) {
        return {MdlControllerType::SizeStart, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "sizeEnd")) {
        return {MdlControllerType::SizeEnd, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "sizeStart_y")) {
        return {MdlControllerType::SizeStart_Y, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "sizeEnd_y")) {
        return {MdlControllerType::SizeEnd_Y, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "spread")) {
        return {MdlControllerType::Spread, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "threshold")) {
        return {MdlControllerType::Threshold, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "velocity")) {
        return {MdlControllerType::Velocity, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "xsize")) {
        return {MdlControllerType::XSize, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "ysize")) {
        return {MdlControllerType::YSize, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "blurlength")) {
        return {MdlControllerType::BlurLength, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "lightningDelay")) {
        return {MdlControllerType::LightningDelay, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "lightningRadius")) {
        return {MdlControllerType::LightningRadius, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "lightningScale")) {
        return {MdlControllerType::LightningScale, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "lightningSubDiv")) {
        return {MdlControllerType::LightningSubDiv, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "detonate")) {
        return {MdlControllerType::Detonate, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "alphaMid")) {
        return {MdlControllerType::AlphaMid, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "colorMid")) {
        return {MdlControllerType::ColorMid, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "percentStart")) {
        return {MdlControllerType::PercentStart, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "percentMid")) {
        return {MdlControllerType::PercentMid, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "percentEnd")) {
        return {MdlControllerType::PercentEnd, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "sizeMid")) {
        return {MdlControllerType::SizeMid, MdlNodeFlags::emitter};
    } else if (string::icmp(cont, "sizeMid_y")) {
        return {MdlControllerType::SizeMid_Y, MdlNodeFlags::emitter};
    }
    // Meshes
    else if (string::icmp(cont, "selfillumcolor")) {
        return {MdlControllerType::SelfIllumColor, MdlNodeFlags::mesh};
    } else if (string::icmp(cont, "alpha")) {
        return {MdlControllerType::Alpha, MdlNodeFlags::mesh | MdlNodeFlags::emitter};
    }
    return {0, 0};
}

// -- Nodes -------------------------------------------------------------------

MdlNode::MdlNode(std::string name_, uint32_t type_)
    : name{name_}
    , type{type_}
{
}

void MdlNode::add_controller_data(std::string_view name_, uint32_t type_, std::vector<float> data_,
    int rows_, int columns_)
{
    MdlControllerKey k{
        nw::kernel::strings().intern(name_),
        type_,
        rows_,
        static_cast<int>(controller_keys.size()),
        static_cast<int>(controller_data.size()),
        columns_,
    };

    controller_keys.push_back(k);

    for (float f : data_) {
        controller_data.push_back(f);
    }
}

std::pair<const MdlControllerKey*, std::span<const float>> MdlNode::get_controller(uint32_t type_) const
{
    std::span<const float> result;
    for (const auto& c : controller_keys) {
        if (c.type == type_) {
            if (c.columns != -1) {
                result = {&controller_data[c.data_offset], static_cast<size_t>(c.rows * c.columns)};
            }
            return std::make_pair(&c, result);
        }
    }
    return std::make_pair(nullptr, result);
}

MdlAABBNode::MdlAABBNode(std::string name_)
    : MdlTrimeshNode(std::move(name_), MdlNodeType::aabb)
{
}

MdlAnimeshNode::MdlAnimeshNode(std::string name_)
    : MdlTrimeshNode(std::move(name_), MdlNodeType::animmesh)
{
}

MdlCameraNode::MdlCameraNode(std::string name_)
    : MdlNode(std::move(name_), MdlNodeType::camera)
{
}

MdlDanglymeshNode::MdlDanglymeshNode(std::string name_)
    : MdlTrimeshNode(std::move(name_), MdlNodeType::danglymesh)
{
}

MdlDummyNode::MdlDummyNode(std::string name_)
    : MdlNode(std::move(name_), MdlNodeType::dummy)
{
}

MdlEmitterNode::MdlEmitterNode(std::string name_)
    : MdlNode(std::move(name_), MdlNodeType::emitter)
{
}

MdlLightNode::MdlLightNode(std::string name_)
    : MdlNode(std::move(name_), MdlNodeType::light)
{
}

MdlPatchNode::MdlPatchNode(std::string name_)
    : MdlNode(std::move(name_), MdlNodeType::patch)
{
}

MdlReferenceNode::MdlReferenceNode(std::string name_)
    : MdlNode(std::move(name_), MdlNodeType::reference)
    , reattachable{false}
{
}

MdlSkinNode::MdlSkinNode(std::string name_)
    : MdlTrimeshNode(std::move(name_), MdlNodeType::skin)
{
}

MdlTrimeshNode::MdlTrimeshNode(std::string name_, uint32_t type_)
    : MdlNode(std::move(name_), type_)
    , diffuse{0.8, 0.8, 0.8}
{
}

// -- Geometry ----------------------------------------------------------------

MdlGeometry::MdlGeometry(MdlGeometryType type_)
    : type{type_}
{
}

MdlModel::MdlModel()
    : MdlGeometry(MdlGeometryType::model)
    , classification{MdlModelClass::invalid}
    , ignorefog{1}
    , bmin{-5.0f, -5.0f, -1.0f}
    , bmax{5.0f, 5.0f, 10.0f}
    , radius{7.0f}
    , animationscale{1.0f}
{
}

MdlAnimation::MdlAnimation(std::string name_)
    : MdlGeometry(MdlGeometryType::animation)
{
    name = name_;
}

// -- Mdl -------------------------------------------------------------------

Mdl::Mdl(const std::string& filename)
    : bytes_{ByteArray::from_file(filename)}
{
    if (bytes_[0] == 0) {
        // MdlBinaryParser p{ByteView(bytes_.data(), bytes_.size()), this};
        // loaded_ = parse_binary();
    } else {
        MdlTextParser p{bytes_.string_view(), this};
        loaded_ = p.parse();
    }
}

std::unique_ptr<MdlNode> Mdl::make_node(uint32_t type, std::string_view name)
{
    switch (type) {
    default:
        LOG_F(ERROR, "Invalid node type: {}, name: {}", type, name);
        return {};
    case MdlNodeType::dummy:
        return std::make_unique<MdlDummyNode>(std::string(name));
    case MdlNodeType::patch:
        return std::make_unique<MdlPatchNode>(std::string(name));
    case MdlNodeType::reference:
        return std::make_unique<MdlReferenceNode>(std::string(name));
    case MdlNodeType::trimesh:
        return std::make_unique<MdlTrimeshNode>(std::string(name));
    case MdlNodeType::danglymesh:
        return std::make_unique<MdlDanglymeshNode>(std::string(name));
    case MdlNodeType::skin:
        return std::make_unique<MdlSkinNode>(std::string(name));
    case MdlNodeType::animmesh:
        return std::make_unique<MdlAnimeshNode>(std::string(name));
    case MdlNodeType::emitter:
        return std::make_unique<MdlEmitterNode>(std::string(name));
    case MdlNodeType::light:
        return std::make_unique<MdlLightNode>(std::string(name));
    case MdlNodeType::aabb:
        return std::make_unique<MdlAABBNode>(std::string(name));
    case MdlNodeType::camera:
        return std::make_unique<MdlCameraNode>(std::string(name));
    }
}

bool Mdl::parse_binary()
{
    return true;
}

bool Mdl::valid() const
{
    return loaded_;
}

void write_out(std::ostream& out, const MdlDanglymeshNode* node, bool is_anim)
{
    if (!is_anim) {
        out << fmt::format("  displacement {}\n", node->displacement);
        out << fmt::format("  period {}\n", node->period);
        out << fmt::format("  tightness {}\n", node->tightness);
        out << fmt::format("  tightness {}\n", node->tightness);
        out << fmt::format("  constraints {}\n", node->constraints.size());
        for (auto c : node->constraints) {
            out << fmt::format("    {}\n", c);
        }
    }
}

void write_out(std::ostream& out, const MdlEmitterNode* node, bool is_anim)
{
    if (!is_anim) {
        out << fmt::format("  blastlength {}\n", node->blastlength);
        out << fmt::format("  blastradius {}\n", node->blastradius);
        out << fmt::format("  blend {}\n", node->blend);
        out << fmt::format("  chunkname {}\n", node->chunkname);
        out << fmt::format("  deadspace {}\n", node->deadspace);
        out << fmt::format("  loop {}\n", node->loop);
        out << fmt::format("  render {}\n", node->render);
        out << fmt::format("  renderorder {}\n", node->renderorder);
        out << fmt::format("  spawntype {}\n", node->spawntype);
        out << fmt::format("  texture {}\n", node->texture);
        out << fmt::format("  twosidedtex {}\n", node->twosidedtex);
        out << fmt::format("  update {}\n", node->update);
        out << fmt::format("  xgrid {}\n", node->xgrid);
        out << fmt::format("  ygrid {}\n", node->ygrid);
        out << fmt::format("  render_sel {}\n", node->render_sel);
        out << fmt::format("  blend_sel {}\n", node->blend_sel);
        out << fmt::format("  update_sel {}\n", node->update_sel);
        out << fmt::format("  spawntype_sel {}\n", node->spawntype_sel);
        out << fmt::format("  opacity {}\n", node->opacity);
        out << fmt::format("  p2p_type {}\n", node->p2p_type);

        out << fmt::format("  affectedByWind {}\n", node->flags & MdlEmitterFlag::AffectedByWind ? 1 : 0);
        out << fmt::format("  bounce {}\n", node->flags & MdlEmitterFlag::Bounce ? 1 : 0);
        out << fmt::format("  inherit {}\n", node->flags & MdlEmitterFlag::Inherit ? 1 : 0);
        out << fmt::format("  inherit_local {}\n", node->flags & MdlEmitterFlag::InheritLocal ? 1 : 0);
        out << fmt::format("  inherit_part {}\n", node->flags & MdlEmitterFlag::InheritPart ? 1 : 0);
        out << fmt::format("  inheritvel {}\n", node->flags & MdlEmitterFlag::InheritVel ? 1 : 0);
        out << fmt::format("  m_isTinted {}\n", node->flags & MdlEmitterFlag::IsTinted ? 1 : 0);
        out << fmt::format("  p2p {}\n", node->flags & MdlEmitterFlag::P2P ? 1 : 0);
        out << fmt::format("  p2p_sel {}\n", node->flags & MdlEmitterFlag::P2PSel ? 1 : 0);
        out << fmt::format("  random {}\n", node->flags & MdlEmitterFlag::Random ? 1 : 0);
        out << fmt::format("  splat {}\n", node->flags & MdlEmitterFlag::Splat ? 1 : 0);
    }
    for (const auto& [k, v] : MdlControllerType::map) {
        if (v.second == MdlNodeFlags::emitter) {
            auto [ckey, cdata] = node->get_controller(v.first);
            if (!ckey || cdata.empty()) continue;
            if (!is_anim && string::endswith(ckey->name.view(), "key")) {
                continue;
            }
            out << fmt::format("         {} ", ckey->name);
            if (ckey->columns > 0) {
                for (size_t i = 0; i < size_t(ckey->rows); ++i) {
                    out << "          ";
                    size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
                    for (size_t j = start; j < stop; ++j) {
                        out << fmt::format(" {: >8.5f}", cdata[j]);
                    }
                    out << '\n';
                }
            }
            out << "         endlist";
        }
    }
}

void write_out(std::ostream& out, const MdlLightNode* node, bool is_anim)
{
    if (!is_anim) {
        out << fmt::format("  ambientonly {}\n", node->ambientonly);
        out << fmt::format("  ndynamictype {}\n", int(node->dynamic));
        out << fmt::format("  affectdynamic {}\n", node->affectdynamic);
        out << fmt::format("  shadow {}\n", node->shadow);
        out << fmt::format("  lightpriority {}\n", node->lightpriority);
        out << fmt::format("  fadingLight {}\n", node->fadinglight);
        out << fmt::format("  radius {}\n", node->flareradius);
        out << fmt::format("  multiplier {}\n", node->multiplier);
        out << fmt::format("  color {:3.2f} {:3.2f} {:3.2f}\n", node->color.x, node->color.y, node->color.z);
    }
    for (const auto& [k, v] : MdlControllerType::map) {
        if (v.second == MdlNodeFlags::light) {
            auto [ckey, cdata] = node->get_controller(v.first);
            if (!ckey || cdata.empty()) continue;
            out << fmt::format("        {}\n", ckey->name);
            if (ckey->columns > 0) {
                for (size_t i = 0; i < size_t(ckey->rows); ++i) {
                    out << "          ";
                    size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
                    for (size_t j = start; j < stop; ++j) {
                        out << fmt::format(" {: >8.5f}", cdata[j]);
                    }
                    out << '\n';
                }
            }
            out << "        endlist\n";
        }
    }
}

void write_out(std::ostream& out, const MdlSkinNode* node, bool is_anim)
{
    if (!is_anim) {
        out << fmt::format("  weights {}\n", node->weights.size());
        for (const auto& w : node->weights) {
            for (size_t i = 0; i < 4; ++i) {
                if (w.bones[i].empty()) break;
                out << fmt::format("    {} {}", w.bones[i], w.weights[i]);
            }
        }
    }
}

void write_out(std::ostream& out, const MdlTrimeshNode* node, MdlModelClass class_, bool is_anim)
{
    if (!is_anim) {
        auto [alpha_key, alpha_data] = node->get_controller(MdlControllerType::Alpha);
        auto [selfilum_key, selfilum_data] = node->get_controller(MdlControllerType::SelfIllumColor);

        if (alpha_key) {
            out << fmt::format("  alpha {}\n", alpha_data[0]);
        }

        if (selfilum_key) {
            out << fmt::format("  selfillumcolor {:.2f} {:.2f} {:.2f}\n", selfilum_data[0], selfilum_data[1], selfilum_data[2]);
        }

        out << fmt::format("  ambient {:.2f} {:.2f} {:.2f}\n", node->ambient.x, node->ambient.y, node->ambient.z);
        out << fmt::format("  diffuse {:.2f} {:.2f} {:.2f}\n", node->diffuse.x, node->diffuse.y, node->diffuse.z);
        out << fmt::format("  specular {:.2f} {:.2f} {:.2f}\n", node->specular.x, node->specular.y, node->specular.z);

        out << fmt::format("  bitmap {}\n", node->bitmap);
        out << fmt::format("  shininess {}\n", node->shininess);

        out << fmt::format("  render {}\n", int(node->render));
        out << fmt::format("  shadow {}\n", int(node->shadow));
        out << fmt::format("  beaming {}\n", int(node->beaming));
        out << fmt::format("  transparencyhint {}\n", int(node->transparencyhint));

        if (class_ == MdlModelClass::tile
            || class_ == MdlModelClass::invalid
            || class_ == MdlModelClass::character) {
            out << fmt::format("  tilefade {}\n", node->tilefade);
        }

        if (class_ == MdlModelClass::tile) {
            out << fmt::format("  rotatetexture {}\n", int(node->rotatetexture));
        }

        if (node->verts.size()) {
            out << fmt::format("  verts {}\n", node->verts.size());
            for (const auto v : node->verts) {
                out << fmt::format("    {: >8.5f} {: >8.5f} {: >8.5f}\n", v.x, v.y, v.z);
            }
        }

        if (node->colors.size()) {
            out << fmt::format("  colors {}\n", node->colors.size());
            for (const auto& c : node->colors) {
                out << fmt::format("    {:.4f} {:.4f} {:.4f}\n", c.x, c.y, c.z);
            }
        }

        if (node->faces.size()) {
            out << fmt::format("  faces {}\n", node->faces.size());
            for (const auto& f : node->faces) {
                out << fmt::format("    {: >3} {: >3} {: >3} {: >2}  {: >3} {: >3} {: >3} {}\n",
                    f.vert_idx[0], f.vert_idx[1], f.vert_idx[2], f.shader_group_idx,
                    f.tvert_idx[0], f.tvert_idx[1], f.tvert_idx[2], f.material_idx);
            }
        }

        if (node->tverts[0].size()) {
            out << fmt::format("  tverts {}\n", node->tverts[0].size());
            for (const auto v : node->tverts[0]) {
                out << fmt::format("    {: >8.5f} {: >8.5f} 0\n", v.x, v.y, v.z);
            }
        }
    } else {
        for (const auto& [k, v] : MdlControllerType::map) {
            if (v.second == MdlNodeFlags::mesh) {
                auto [ckey, cdata] = node->get_controller(v.first);
                if (!ckey || cdata.empty()) continue;
                out << fmt::format("        {}\n", ckey->name);
                if (ckey->columns > 0) {
                    for (size_t i = 0; i < size_t(ckey->rows); ++i) {
                        out << "          ";
                        size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
                        for (size_t j = start; j < stop; ++j) {
                            out << fmt::format(" {: >8.5f}", cdata[j]);
                        }
                        out << '\n';
                    }
                }
                out << "        endlist\n";
            }
        }
    }
}

std::ostream& operator<<(std::ostream& out, const Mdl& mdl)
{
    out << "# Exported from rollnw\n"
        << fmt::format("filedependancy {}\n",
               mdl.model.file_dependency.size() ? mdl.model.file_dependency : "Unknown")
        << fmt::format("newmodel {}\n", mdl.model.name)
        << fmt::format("setsupermodel {} {}\n", mdl.model.name, mdl.model.supermodel_name)
        << fmt::format("classification {}\n", model_class_to_string(mdl.model.classification))
        << fmt::format("setanimationscale {:.2f}\n", mdl.model.animationscale)
        << fmt::format("beginmodelgeom {}\n", mdl.model.name);

    for (const auto& node : mdl.model.nodes) {
        out << fmt::format("node {} {}\n", MdlNodeType::to_string(node->type), node->name);

        auto [pos_key, pos_data] = node->get_controller(MdlControllerType::Position);
        auto [ori_key, ori_data] = node->get_controller(MdlControllerType::Orientation);
        auto [scale_key, scale_data] = node->get_controller(MdlControllerType::Scale);
        auto [wire_key, wire_data] = node->get_controller(MdlControllerType::Wirecolor);

        out << fmt::format("  parent {}\n", node->parent ? node->parent->name : "null");
        if (!node->parent) {
            out << "endnode\n";
            continue;
        }

        out << fmt::format("  position {: >8.5f} {: >8.5f} {: >8.5f}\n", pos_data[0], pos_data[1], pos_data[2]);
        out << fmt::format("  orientation {: >8.5f} {: >8.5f} {: >8.5f} {: >8.5f}\n", ori_data[0], ori_data[1], ori_data[2], ori_data[3]);
        out << fmt::format("  wirecolor {:.2f} {:.2f} {:.2f}\n", wire_data[0], wire_data[1], wire_data[2]);

        if (scale_key) {
            out << fmt::format("  scale {:.2f}\n", scale_data[0]);
        }

        if (dynamic_cast<MdlAABBNode*>(node.get())) {
            auto n = static_cast<MdlAABBNode*>(node.get());
            write_out(out, static_cast<const MdlTrimeshNode*>(n), mdl.model.classification, false);
        } else if (dynamic_cast<MdlAnimeshNode*>(node.get())) {
            auto n = static_cast<MdlAnimeshNode*>(node.get());
            write_out(out, static_cast<const MdlTrimeshNode*>(n), mdl.model.classification, false);
        } else if (dynamic_cast<MdlDanglymeshNode*>(node.get())) {
            write_out(out, static_cast<const MdlTrimeshNode*>(node.get()), mdl.model.classification, false);
            write_out(out, static_cast<MdlDanglymeshNode*>(node.get()), false);
        } else if (dynamic_cast<MdlEmitterNode*>(node.get())) {
            write_out(out, static_cast<MdlEmitterNode*>(node.get()), false);
        } else if (dynamic_cast<MdlLightNode*>(node.get())) {
            write_out(out, static_cast<MdlLightNode*>(node.get()), false);
        } else if (dynamic_cast<MdlReferenceNode*>(node.get())) {
            auto n = static_cast<MdlReferenceNode*>(node.get());
            out << fmt::format("  refmodel {}\n", n->refmodel);
            out << fmt::format("  reattachable {}\n", int(n->reattachable));
        } else if (dynamic_cast<MdlSkinNode*>(node.get())) {
            auto n = static_cast<MdlSkinNode*>(node.get());
            write_out(out, static_cast<const MdlTrimeshNode*>(n), mdl.model.classification, false);
            write_out(out, static_cast<const MdlSkinNode*>(n), false);
        } else if (dynamic_cast<MdlTrimeshNode*>(node.get())) {
            auto n = static_cast<MdlTrimeshNode*>(node.get());
            write_out(out, n, mdl.model.classification, false);
        }

        out << "endnode\n";
    }

    out << fmt::format("endmodelgeom {}\n\n", mdl.model.name);

    if (mdl.model.animations.size()) {
        for (const auto& anim : mdl.model.animations) {
            out << "#ANIM ASCII\n";
            out << fmt::format("newanim {} {}\n", anim->name, mdl.model.name);
            out << fmt::format("  length {}\n", anim->length);
            out << fmt::format("  transtime {}\n", anim->transition_time);
            out << fmt::format("  animroot {}\n", anim->anim_root);

            for (const auto& ev : anim->events) {
                out << fmt::format("  event {:8.5f} {}\n", ev.time, ev.name);
            }

            for (const auto& node : anim->nodes) {
                out << fmt::format("node {} {}\n", MdlNodeType::to_string(node->type), node->name);

                out << fmt::format("  parent {}\n", node->parent ? node->parent->name : "null");
                if (!node->parent) {
                    out << "endnode\n";
                    continue;
                }

                for (const auto& [k, v] : MdlControllerType::map) {
                    if (v.second == MdlNodeFlags::header) {
                        auto [ckey, cdata] = node->get_controller(v.first);
                        if (!ckey || cdata.empty()) continue;
                        out << fmt::format("        {}\n", ckey->name);
                        if (ckey->columns > 0) {
                            for (size_t i = 0; i < size_t(ckey->rows); ++i) {
                                out << "          ";
                                size_t start = i * ckey->columns, stop = (i + 1) * ckey->columns;
                                for (size_t j = start; j < stop; ++j) {
                                    out << fmt::format(" {: >8.5f}", cdata[j]);
                                }
                                out << '\n';
                            }
                        }
                        out << "        endlist\n";
                    }
                }

                if (dynamic_cast<MdlAABBNode*>(node.get())) {
                    auto n = static_cast<MdlAABBNode*>(node.get());
                    write_out(out, static_cast<const MdlTrimeshNode*>(n), mdl.model.classification, true);
                } else if (dynamic_cast<MdlAnimeshNode*>(node.get())) {
                    auto n = static_cast<MdlAnimeshNode*>(node.get());
                    write_out(out, static_cast<const MdlTrimeshNode*>(n), mdl.model.classification, true);
                } else if (dynamic_cast<MdlDanglymeshNode*>(node.get())) {
                    write_out(out, static_cast<const MdlTrimeshNode*>(node.get()), mdl.model.classification, true);
                    write_out(out, static_cast<MdlDanglymeshNode*>(node.get()), true);
                } else if (dynamic_cast<MdlEmitterNode*>(node.get())) {
                    write_out(out, static_cast<MdlEmitterNode*>(node.get()), true);
                } else if (dynamic_cast<MdlLightNode*>(node.get())) {
                    write_out(out, static_cast<MdlLightNode*>(node.get()), true);
                } else if (dynamic_cast<MdlReferenceNode*>(node.get())) {
                    // auto n = static_cast<MdlReferenceNode*>(node.get());
                } else if (dynamic_cast<MdlSkinNode*>(node.get())) {
                    auto n = static_cast<MdlSkinNode*>(node.get());
                    write_out(out, static_cast<const MdlTrimeshNode*>(n), mdl.model.classification, true);
                    write_out(out, static_cast<const MdlSkinNode*>(n), true);
                } else if (dynamic_cast<MdlTrimeshNode*>(node.get())) {
                    auto n = static_cast<MdlTrimeshNode*>(node.get());
                    write_out(out, n, mdl.model.classification, true);
                }
                out << "endnode\n";
            }
            out << fmt::format("doneanim {} {}\n\n", anim->name, mdl.model.name);
        }
    }

    out << fmt::format("donemodel {}\n", mdl.model.name);

    return out;
}

} // namespace nw
