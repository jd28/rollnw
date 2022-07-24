#include "Mdl.hpp"

#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/string.hpp"
#include "MdlTextParser.hpp"

namespace nw {

// -- Nodes -------------------------------------------------------------------

MdlNode::MdlNode(std::string name, uint32_t type)
    : name{name}
    , type{type}
{
}

MdlAABBNode::MdlAABBNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::aabb)
{
}

MdlAnimeshNode::MdlAnimeshNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::animmesh)
{
}

MdlCameraNode::MdlCameraNode(std::string name)
    : MdlNode(name, MdlNodeType::camera)
{
}

MdlDanglymeshNode::MdlDanglymeshNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::danglymesh)
{
}

MdlDummyNode::MdlDummyNode(std::string name)
    : MdlNode(name, MdlNodeType::dummy)
{
}

MdlEmitterNode::MdlEmitterNode(std::string name)
    : MdlNode(name, MdlNodeType::emitter)
{
}

MdlLightNode::MdlLightNode(std::string name)
    : MdlNode(name, MdlNodeType::light)
{
}

MdlPatchNode::MdlPatchNode(std::string name)
    : MdlNode(name, MdlNodeType::patch)
{
}

MdlReferenceNode::MdlReferenceNode(std::string name)
    : MdlNode(name, MdlNodeType::reference)
    , reattachable{false}
{
}

MdlSkinNode::MdlSkinNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::skin)
{
}

MdlTrimeshNode::MdlTrimeshNode(std::string name, uint32_t type)
    : MdlNode(std::move(name), type)
    , diffuse{0.8, 0.8, 0.8}
{
}

// -- Geometry ----------------------------------------------------------------

MdlGeometry::MdlGeometry(MdlGeometryType type)
    : type{type}
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

MdlNode* Mdl::add_node(uint32_t type, std::string_view name, bool is_anim)
{
    auto& holder = is_anim ? model.anim_nodes : model.nodes;
    switch (type) {
    default:
        LOG_F(ERROR, "Invalid node type");
        return nullptr;
    case MdlNodeType::dummy:
        holder.emplace_back(std::make_unique<MdlDummyNode>(std::string(name)));
        break;
    case MdlNodeType::patch:
        holder.emplace_back(std::make_unique<MdlPatchNode>(std::string(name)));
        break;
    case MdlNodeType::reference:
        holder.emplace_back(std::make_unique<MdlReferenceNode>(std::string(name)));
        break;
    case MdlNodeType::trimesh:
        holder.emplace_back(std::make_unique<MdlTrimeshNode>(std::string(name)));
        break;
    case MdlNodeType::danglymesh:
        holder.emplace_back(std::make_unique<MdlDanglymeshNode>(std::string(name)));
        break;
    case MdlNodeType::skin:
        holder.emplace_back(std::make_unique<MdlSkinNode>(std::string(name)));
        break;
    case MdlNodeType::animmesh:
        holder.emplace_back(std::make_unique<MdlAnimeshNode>(std::string(name)));
        break;
    case MdlNodeType::emitter:
        holder.emplace_back(std::make_unique<MdlEmitterNode>(std::string(name)));
        break;
    case MdlNodeType::light:
        holder.emplace_back(std::make_unique<MdlLightNode>(std::string(name)));
        break;
    case MdlNodeType::aabb:
        holder.emplace_back(std::make_unique<MdlAABBNode>(std::string(name)));
        break;
    case MdlNodeType::camera:
        holder.emplace_back(std::make_unique<MdlCameraNode>(std::string(name)));
        break;
    }
    return holder.back().get();
}

bool Mdl::parse_binary()
{
    return true;
}

bool Mdl::valid() const
{
    return loaded_;
}

} // namespace nw
