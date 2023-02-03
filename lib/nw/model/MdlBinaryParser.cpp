#include "MdlBinaryParser.hpp"

#include "../log.hpp"
#include "Mdl.hpp"

#include <limits>

namespace nw::model {

namespace detail {

struct GeomCxt {
    void clear()
    {
        faces.clear();
        for (auto& it : tverts) {
            it.clear();
        }
        verts.clear();
        normals.clear();
        tangents.clear();
    }

    std::vector<detail::MdlBinaryFace> faces;
    std::vector<glm::vec3> verts;
    std::array<std::vector<glm::vec2>, 4> tverts;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec4> tangents;
};

template <size_t N>
std::string to_string(const std::array<char, N>& in)
{
    for (size_t i = 0; i < N; ++i) {
        if (!in[i]) { return {in.data(), i}; }
    }
    return {in.data(), N};
}

} // namespace detail

BinaryParser::BinaryParser(std::span<uint8_t> bytes, Mdl* mdl)
    : mdl_{mdl}
    , bytes_{bytes}
{
}

bool BinaryParser::parse()
{
    memcpy(&header, bytes_.data(), 12);
    if (header.type != 0
        || header.raw_data_offset >= bytes_.size()
        || header.raw_data_offset + header.raw_data_size > bytes_.size()) {
        LOG_F(ERROR, "invalid binary mdl");
        return false;
    }
    return parse_model(12);
}

bool BinaryParser::parse_anim(const detail::MdlBinaryAnimationHeader& data)
{
    auto an = std::make_unique<Animation>(detail::to_string(data.geometry_header.name));
    if (!parse_geometry(an.get(), data.geometry_header)) { return false; }
    an->anim_root = detail::to_string(data.root);
    an->length = data.length;
    an->transition_time = data.transition_time;

    an->events.reserve(data.events.length);
    auto off = data.events.offset;
    for (size_t i = 0; i < data.events.length; ++i) {
        detail::MdlBinaryAnimationEvent event;
        memcpy(&event, bytes_.data() + off + 12, sizeof(detail::MdlBinaryAnimationEvent));
        an->events.push_back({event.after, detail::to_string(event.name)});
        off += sizeof(detail::MdlBinaryAnimationEvent);
    }

    mdl_->model.animations.push_back(std::move(an));
    return true;
}

bool BinaryParser::parse_geometry(Geometry* geometry, const detail::MdlBinaryGeometryHeader& data)
{
    uint32_t off;
    memcpy(&off, bytes_.data() + data.root_node_offset, 4);
    geometry->nodes.reserve(data.node_count);
    if (!parse_node(data.root_node_offset, geometry)) {
        return false;
    }
    return true;
}

bool BinaryParser::parse_model(uint32_t offset)
{
    detail::MdlBinaryModelHeader model_header;
    memcpy(&model_header, bytes_.data() + offset, detail::MdlBinaryModelHeader::s_sizeof);

    mdl_->model.name = detail::to_string(model_header.geometry_header.name);
    mdl_->model.animationscale = model_header.animation_scale;
    mdl_->model.bmax = model_header.bbmax;
    mdl_->model.bmin = model_header.bbmin;
    mdl_->model.classification = static_cast<ModelClass>(model_header.classification);
    mdl_->model.ignorefog = model_header.ignorefog;
    mdl_->model.radius = model_header.radius;
    mdl_->model.supermodel_name = detail::to_string(model_header.supermodel);

    if (!parse_geometry(&mdl_->model, model_header.geometry_header)) {
        return false;
    }

    mdl_->model.animations.reserve(model_header.animations.length);
    uint32_t off = model_header.animations.offset;
    for (size_t i = 0; i < model_header.animations.length; ++i) {
        uint32_t ptr;
        memcpy(&ptr, bytes_.data() + off + 12, 4);

        detail::MdlBinaryAnimationHeader data;
        memcpy(&data, bytes_.data() + ptr + 12, sizeof(detail::MdlBinaryAnimationHeader));
        if (!parse_anim(data)) {
            return false;
        }

        offset += 4;
    }

    return true;
}

bool BinaryParser::parse_node(uint32_t offset, Geometry* geometry, Node* parent)
{
    static thread_local detail::GeomCxt s_ctx;

    detail::MdlBinaryNodeHeader nodehead;
    memcpy(&nodehead, bytes_.data() + offset + 12, detail::MdlBinaryNodeHeader::s_sizeof);

    std::unique_ptr<Node> node = mdl_->make_node(nodehead.type, detail::to_string(nodehead.name));
    if (parent) {
        parent->children.push_back(node.get());
    }
    node->parent = parent;
    node->inheritcolor = nodehead.inherit_color;

    // Controllers
    node->controller_data.resize(nodehead.controller_data.length);
    memcpy(node->controller_data.data(), bytes_.data() + nodehead.controller_data.offset + 12,
        nodehead.controller_data.length * 4);

    auto off = nodehead.controller_keys.offset + 12;
    for (size_t i = 0; i < nodehead.controller_keys.length; ++i) {
        detail::MdlBinaryController old_c;
        // old_c.time_index;
        memcpy(&old_c, bytes_.data() + off, sizeof(detail::MdlBinaryController));
        ControllerKey new_c{
            {},
            static_cast<uint32_t>(old_c.type),
            old_c.rows,
            static_cast<int>(i),
            old_c.time_index,
            old_c.data_index,
            old_c.columns,
            geometry->type == GeometryType::animation};

        node->controller_keys.push_back(new_c);
        off += sizeof(detail::MdlBinaryController);
    }

    auto load_mesh_data = [this](TrimeshNode* node, const detail::MdlBinaryMeshHeader& data) {
        node->ambient = data.ambient;
        node->beaming = data.beaming;
        node->bmin = data.bbmin;
        node->bmax = data.bbmax;
        node->bitmap = detail::to_string(data.texture0);
        // node->center = data;
        node->diffuse = data.diffuse;
        node->materialname = detail::to_string(data.texture3);
        // node->gizmo = data;
        // node->danglymesh = data;
        // node->period = data.per;
        // node->tightness = data;
        //  node->displacement = data;
        node->render = data.render_flag;
        // node->renderhint = data.render_hint; -- Need to change a type
        node->rotatetexture = data.rotate_texture;
        node->shadow = data.shadow;
        node->shininess = data.shininess;
        node->specular = data.specular;
        // node->textures = data; -- Fix type
        node->tilefade = data.tile_fade;
        node->transparencyhint = data.transparency_hint;
        // node->showdispl = data;
        // node->displtype = data;
        node->lightmapped = data.lightmapped;
        // node->multimaterial = data.;

        // node->colors

        if (data.vertices != std::numeric_limits<uint32_t>::max()) {
            s_ctx.verts.resize(data.vertex_count);
            memcpy(s_ctx.verts.data(), bytes_.data() + header.raw_data_offset + data.vertices + 12,
                data.vertex_count * sizeof(glm::vec3));
        }

        if (data.texture0_vert_ptr != std::numeric_limits<uint32_t>::max()) {
            s_ctx.tverts[0].resize(data.vertex_count);
            memcpy(s_ctx.tverts[0].data(), bytes_.data() + header.raw_data_offset + data.texture0_vert_ptr + 12,
                data.vertex_count * sizeof(glm::vec2));
        }

        if (data.vertex_normals_ptr != std::numeric_limits<uint32_t>::max()) {
            s_ctx.normals.resize(data.vertex_count);
            memcpy(s_ctx.normals.data(), bytes_.data() + header.raw_data_offset + data.vertex_normals_ptr + 12,
                data.vertex_count * sizeof(glm::vec3));
        }

        node->vertices.resize(data.vertex_count);
        for (size_t i = 0; i < data.vertex_count; ++i) {
            node->vertices[i].position = s_ctx.verts[i];
            if (!s_ctx.normals.empty()) {
                node->vertices[i].normal = s_ctx.normals[i];
            }
            if (!s_ctx.tverts[0].empty()) {
                node->vertices[i].tex_coords = s_ctx.tverts[0][i];
            }
        }

        s_ctx.faces.resize(data.faces.length);
        memcpy(s_ctx.faces.data(), bytes_.data() + data.faces.offset + 12, data.faces.length * sizeof(detail::MdlBinaryFace));

        node->indices.reserve(data.faces.length * 3);
        for (size_t i = 0; i < data.faces.length; ++i) {
            node->indices.push_back(s_ctx.faces[i].vertex_indicies[0]);
            node->indices.push_back(s_ctx.faces[i].vertex_indicies[1]);
            node->indices.push_back(s_ctx.faces[i].vertex_indicies[2]);
        }

        return true;
    };

    if (node->type == NodeType::trimesh) {
        detail::MdlBinaryTrimeshNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryTrimeshNode::s_sizeof);

        auto n = static_cast<TrimeshNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    } else if (node->type == NodeType::reference) {
        detail::MdlBinaryReferenceNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryReferenceNode::s_sizeof);

        auto n = static_cast<ReferenceNode*>(node.get());
        n->reattachable = data.reattachable;
        n->refmodel = detail::to_string(data.ref);
    } else if (node->type == NodeType::danglymesh) {
        detail::MdlBinaryDanglymeshNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryDanglymeshNode::s_sizeof);

        auto n = static_cast<DanglymeshNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    } else if (node->type == NodeType::emitter) {
        detail::MdlBinaryEmitterNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryEmitterNode::s_sizeof);

        auto n = static_cast<EmitterNode*>(node.get());
    } else if (node->type == NodeType::light) {
        detail::MdlBinaryLightNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryLightNode::s_sizeof);

        auto n = static_cast<LightNode*>(node.get());
    } else if (node->type == NodeType::aabb) {
        detail::MdlBinaryAABBNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryAABBNode::s_sizeof);

        auto n = static_cast<AABBNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    } else if (node->type == NodeType::skin) {
        detail::MdlBinarySkinNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinarySkinNode::s_sizeof);

        auto n = static_cast<SkinNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    } else if (node->type == NodeType::animmesh) {
        detail::MdlBinaryAnimmeshNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryAnimmeshNode::s_sizeof);

        auto n = static_cast<AnimeshNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    }

    geometry->nodes.push_back(std::move(node));

    // Process all children
    off = nodehead.children.offset;
    for (size_t i = 0; i < nodehead.children.length; ++i) {
        uint32_t ptr = 0;
        memcpy(&ptr, bytes_.data() + off + 12, 4);
        if (!parse_node(ptr, geometry, node.get())) {
            return false;
        }
        off += 4;
    }

    s_ctx.clear();

    return true;
}

} // namespace nw::model
