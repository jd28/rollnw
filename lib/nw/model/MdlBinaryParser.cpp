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
        bones.clear();
        weights.clear();
    }

    std::vector<detail::MdlBinaryFace> faces;
    std::vector<glm::vec3> verts;
    std::array<std::vector<glm::vec2>, 4> tverts;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec4> tangents;
    std::vector<std::array<uint16_t, 4>> bones;
    std::vector<glm::vec4> weights;
};

template <size_t N>
std::string to_string(const std::array<char, N>& in)
{
    size_t len = 0;
    while (in[len] && len < N) {
        ++len;
    }
    std::string out{in.data(), len};
    string::tolower(&out);
    return out;
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
    LOG_F(INFO, "[model] parsing animation '{}'", an->name);
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
    LOG_F(INFO, "[model] parsing {}", mdl_->model.name);
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

        off += 4;
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

    auto load_mesh_data = [this](TrimeshNode* mesh, const detail::MdlBinaryMeshHeader& data) {
        mesh->ambient = data.ambient;
        mesh->beaming = data.beaming;
        mesh->bmin = data.bbmin;
        mesh->bmax = data.bbmax;
        mesh->bitmap = detail::to_string(data.texture0);
        // mesh->center = data;
        mesh->diffuse = data.diffuse;
        mesh->materialname = detail::to_string(data.texture3);
        mesh->render = data.render_flag;
        // mesh->renderhint = data.render_hint; -- Need to change a type
        mesh->rotatetexture = data.rotate_texture;
        mesh->shadow = data.shadow;
        mesh->shininess = data.shininess;
        mesh->specular = data.specular;
        // mesh->textures = data; -- Fix type
        mesh->tilefade = data.tile_fade;
        mesh->transparencyhint = data.transparency_hint;
        // mesh->showdispl = data;
        // mesh->displtype = data;
        mesh->lightmapped = data.lightmapped;
        // mesh->multimaterial = data.;

        // mesh->colors

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

        s_ctx.faces.resize(data.faces.length);
        memcpy(s_ctx.faces.data(), bytes_.data() + data.faces.offset + 12, data.faces.length * sizeof(detail::MdlBinaryFace));

        return true;
    };

    auto load_mesh_vertices = [this](TrimeshNode* mesh, const detail::MdlBinaryMeshHeader& data) {
        mesh->vertices.resize(data.vertex_count);
        for (size_t i = 0; i < data.vertex_count; ++i) {
            mesh->vertices[i].position = s_ctx.verts[i];
            if (!s_ctx.normals.empty()) {
                mesh->vertices[i].normal = s_ctx.normals[i];
            }
            if (!s_ctx.tverts[0].empty()) {
                mesh->vertices[i].tex_coords = s_ctx.tverts[0][i];
            }
        }

        mesh->indices.reserve(data.faces.length * 3);
        for (size_t i = 0; i < data.faces.length; ++i) {
            mesh->indices.push_back(s_ctx.faces[i].vertex_indicies[0]);
            mesh->indices.push_back(s_ctx.faces[i].vertex_indicies[1]);
            mesh->indices.push_back(s_ctx.faces[i].vertex_indicies[2]);
        }

        return true;
    };

    if (node->type == NodeType::trimesh) {
        detail::MdlBinaryTrimeshNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryTrimeshNode::s_sizeof);

        auto n = static_cast<TrimeshNode*>(node.get());
        if (!load_mesh_data(n, data.header) || !load_mesh_vertices(n, data.header)) { return false; }
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
        if (!load_mesh_data(n, data.header) || !load_mesh_vertices(n, data.header)) { return false; }

        if (data.vert_constraints.length) {
            n->constraints.resize(data.vert_constraints.length);
            memcpy(n->constraints.data(),
                bytes_.data() + data.vert_constraints.offset + 12,
                sizeof(uint32_t) * data.vert_constraints.length);
        }
        n->displacement = data.displacement;
        n->period = data.period;
        n->tightness = data.tightness;

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
        if (!load_mesh_data(n, data.header) || !load_mesh_vertices(n, data.header)) { return false; }
    } else if (node->type == NodeType::skin) {
        detail::MdlBinarySkinNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinarySkinNode::s_sizeof);
        auto n = static_cast<SkinNode*>(node.get());

        if (!load_mesh_data(n, data.header)) {
            LOG_F(INFO, "[model] skin node: failed to parse mesh header");
            return false;
        }

        for (size_t i = 0; i < 17; ++i) {
            n->bone_nodes[i] = int16_t(data.bone_to_nodes[i]);
        }

        size_t off = header.raw_data_offset + data.bones_ptr + 12;
        for (size_t i = 0; i < s_ctx.verts.size(); ++i) {
            std::array<uint16_t, 4> bones;
            memcpy(bones.data(), bytes_.data() + off, sizeof(uint16_t) * 4);
            off += sizeof(uint16_t) * 4;
            s_ctx.bones.push_back(bones);
        }

        off = header.raw_data_offset + data.weights_ptr + 12;
        for (size_t i = 0; i < s_ctx.verts.size(); ++i) {
            glm::vec4 weights;
            memcpy(&weights.x, bytes_.data() + off, sizeof(float) * 4);
            off += sizeof(float) * 4;
            s_ctx.weights.push_back(weights);
        }

        for (size_t i = 0; i < s_ctx.verts.size(); ++i) {
            glm::vec4 weights;
            memcpy(&weights.x, bytes_.data() + off, sizeof(float) * 4);
            off += sizeof(float) * 4;
            s_ctx.weights.push_back(weights);
        }

        n->vertices.resize(data.header.vertex_count);
        for (size_t i = 0; i < data.header.vertex_count; ++i) {
            n->vertices[i].position = s_ctx.verts[i];
            if (!s_ctx.normals.empty()) {
                n->vertices[i].normal = s_ctx.normals[i];
            }

            if (!s_ctx.tverts[0].empty()) {
                n->vertices[i].tex_coords = s_ctx.tverts[0][i];
            }

            if (!s_ctx.weights.empty()) {
                n->vertices[i].weights = s_ctx.weights[i];
            }

            if (!s_ctx.bones.empty()) {
                const auto& bones = s_ctx.bones.at(i);
                n->vertices[i].bones = glm::ivec4{0};
                for (size_t j = 0; j < 4; ++j) {
                    if (bones[j] == std::numeric_limits<uint16_t>::max()) { break; }
                    n->vertices[i].bones[j] = bones[j];
                }
            }
        }

        n->indices.reserve(data.header.faces.length * 3);
        for (size_t i = 0; i < data.header.faces.length; ++i) {
            n->indices.push_back(s_ctx.faces[i].vertex_indicies[0]);
            n->indices.push_back(s_ctx.faces[i].vertex_indicies[1]);
            n->indices.push_back(s_ctx.faces[i].vertex_indicies[2]);
        }

        n->bone_rotation_inv.resize(data.qbone_ref_inv.length);
        memcpy(n->bone_rotation_inv.data(),
            bytes_.data() + 12 + data.qbone_ref_inv.offset,
            sizeof(glm::quat) * data.qbone_ref_inv.length);

        for (auto& q : n->bone_rotation_inv) {
            std::swap(q.x, q.w); // Bioware stores [w, x, y, z]
        }

        n->bone_translation_inv.resize(data.tbone_ref_inv.length);
        memcpy(n->bone_translation_inv.data(),
            bytes_.data() + 12 + data.tbone_ref_inv.offset,
            sizeof(glm::vec3) * data.tbone_ref_inv.length);

    } else if (node->type == NodeType::animmesh) {
        detail::MdlBinaryAnimmeshNode data;
        memcpy(&data, bytes_.data() + offset + 12, detail::MdlBinaryAnimmeshNode::s_sizeof);

        auto n = static_cast<AnimeshNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    }

    geometry->nodes.push_back(std::move(node));
    auto n = geometry->nodes.back().get();

    // Process all children
    off = nodehead.children.offset;
    for (size_t i = 0; i < nodehead.children.length; ++i) {
        uint32_t ptr = 0;
        memcpy(&ptr, bytes_.data() + off + 12, 4);
        if (!parse_node(ptr, geometry, n)) {
            return false;
        }
        off += 4;
    }

    if (n->children.size() != nodehead.children.length) {
        LOG_F(ERROR, "child count mismatch {} != {}",
            n->children.size(),
            nodehead.children.length);
        return false;
    }

    s_ctx.clear();

    return true;
}

} // namespace nw::model
