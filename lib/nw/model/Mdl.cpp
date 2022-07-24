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
    // m_nPartNumber = 0;
}

MdlAABBNode::MdlAABBNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::aabb)
{
    // m_ulRender = 0;
    // m_ulShadow = 0;
}

MdlAnimeshNode::MdlAnimeshNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::animmesh)
{
    // m_fSamplePeriod = 0.0f;
    // m_ulVertexSetCount = 0;
    // m_ulTextureSetCount = 0;
}

MdlCameraNode::MdlCameraNode(std::string name)
    : MdlNode(name, MdlNodeType::camera)
{
}

MdlDanglymeshNode::MdlDanglymeshNode(std::string name)
    : MdlTrimeshNode(std::move(name), MdlNodeType::danglymesh)
{
    // m_fDisplacement = 0.0f;
    // m_fTightness = 0.0f;
    // m_fPeriod = 0.0f;
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

    // m_pafSkinWeights.Initialize();
    // m_pasSkinBoneRefs.Initialize();
    // m_pasNodeToBoneMap.Initialize();

    //
    // Initialize variables not initialized by Bioware
    //

    // m_ulNodeToBoneCount = 0;
    // memset(m_asBoneNodeNumbers, 0, sizeof(m_asBoneNodeNumbers));
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

MdlAnimation::MdlAnimation(std::string name)
    : MdlGeometry(MdlGeometryType::animation)
{
    length_ = 1.0f;
    transition_time_ = 0.25f;
    memset(anim_root_, 0, 64);
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

MdlNode* Mdl::add_node(uint32_t type, std::string_view name)
{
    switch (type) {
    default:
        LOG_F(ERROR, "Invalid node type");
        return nullptr;
    case MdlNodeType::dummy:
        model.nodes.emplace_back(std::make_unique<MdlDummyNode>(std::string(name)));
        break;
    case MdlNodeType::patch:
        model.nodes.emplace_back(std::make_unique<MdlPatchNode>(std::string(name)));
        break;
    case MdlNodeType::reference:
        model.nodes.emplace_back(std::make_unique<MdlReferenceNode>(std::string(name)));
        break;
    case MdlNodeType::trimesh:
        model.nodes.emplace_back(std::make_unique<MdlTrimeshNode>(std::string(name)));
        break;
    case MdlNodeType::danglymesh:
        model.nodes.emplace_back(std::make_unique<MdlDanglymeshNode>(std::string(name)));
        break;
    case MdlNodeType::skin:
        model.nodes.emplace_back(std::make_unique<MdlSkinNode>(std::string(name)));
        break;
    case MdlNodeType::animmesh:
        model.nodes.emplace_back(std::make_unique<MdlAnimeshNode>(std::string(name)));
        break;
    case MdlNodeType::emitter:
        model.nodes.emplace_back(std::make_unique<MdlEmitterNode>(std::string(name)));
        break;
    case MdlNodeType::light:
        model.nodes.emplace_back(std::make_unique<MdlLightNode>(std::string(name)));
        break;
    case MdlNodeType::aabb:
        model.nodes.emplace_back(std::make_unique<MdlAABBNode>(std::string(name)));
        break;
    case MdlNodeType::camera:
        model.nodes.emplace_back(std::make_unique<MdlCameraNode>(std::string(name)));
        break;
    }
    return model.nodes.back().get();
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
