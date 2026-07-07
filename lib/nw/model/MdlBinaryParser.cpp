#include "MdlBinaryParser.hpp"

#include "../log.hpp"
#include "Mdl.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

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

    Vector<detail::MdlBinaryFace> faces;
    Vector<glm::vec3> verts;
    std::array<Vector<glm::vec2>, 4> tverts;
    Vector<glm::vec3> normals;
    Vector<glm::vec4> tangents;
    Vector<std::array<uint16_t, 4>> bones;
    Vector<glm::vec4> weights;
};

template <size_t N>
String to_string(const std::array<char, N>& in)
{
    size_t len = 0;
    while (len < N && in[len]) {
        ++len;
    }
    String out{in.data(), len};
    string::tolower(&out);
    return out;
}

constexpr size_t mdl_pointer_base = 12;
constexpr size_t max_binary_mdl_nodes = 65536;

} // namespace detail

BinaryParser::BinaryParser(std::span<uint8_t> bytes, Mdl* mdl)
    : mdl_{mdl}
    , bytes_{bytes}
{
}

bool BinaryParser::checked_range(size_t offset, size_t size) const
{
    return offset <= bytes_.size() && size <= bytes_.size() - offset;
}

bool BinaryParser::read_bytes(size_t offset, void* dst, size_t size) const
{
    if (!checked_range(offset, size)) {
        LOG_F(ERROR, "invalid binary mdl: read outside buffer");
        return false;
    }
    if (size > 0) {
        std::memcpy(dst, bytes_.data() + offset, size);
    }
    return true;
}

bool BinaryParser::pointer_offset(uint32_t offset, size_t* result) const
{
    const size_t value = static_cast<size_t>(offset);
    if (value > std::numeric_limits<size_t>::max() - detail::mdl_pointer_base) {
        LOG_F(ERROR, "invalid binary mdl: pointer offset overflow");
        return false;
    }
    *result = value + detail::mdl_pointer_base;
    return checked_range(*result, 0);
}

bool BinaryParser::raw_offset(uint32_t offset, size_t* result) const
{
    size_t base = static_cast<size_t>(header.raw_data_offset);
    if (base > std::numeric_limits<size_t>::max() - static_cast<size_t>(offset)) {
        LOG_F(ERROR, "invalid binary mdl: raw offset overflow");
        return false;
    }
    base += static_cast<size_t>(offset);
    if (base > std::numeric_limits<size_t>::max() - detail::mdl_pointer_base) {
        LOG_F(ERROR, "invalid binary mdl: raw offset overflow");
        return false;
    }
    *result = base + detail::mdl_pointer_base;
    return checked_range(*result, 0);
}

bool BinaryParser::array_offset(const detail::MdlBinaryArray& array, size_t element_size, size_t* result) const
{
    if (array.length == 0) {
        *result = 0;
        return true;
    }
    if (!pointer_offset(array.offset, result)) {
        return false;
    }
    if (element_size != 0 && array.length > std::numeric_limits<size_t>::max() / element_size) {
        LOG_F(ERROR, "invalid binary mdl: array size overflow");
        return false;
    }
    const size_t size = static_cast<size_t>(array.length) * element_size;
    if (!checked_range(*result, size)) {
        LOG_F(ERROR, "invalid binary mdl: array outside buffer");
        return false;
    }
    return true;
}

bool BinaryParser::parse()
{
    if (!read_bytes(0, &header, detail::MdlBinaryHeader::s_sizeof)) {
        LOG_F(ERROR, "invalid binary mdl");
        return false;
    }
    if (header.type != 0
        || !checked_range(header.raw_data_offset, header.raw_data_size)) {
        LOG_F(ERROR, "invalid binary mdl");
        return false;
    }
    return parse_model(detail::mdl_pointer_base);
}

bool BinaryParser::parse_anim(const detail::MdlBinaryAnimationHeader& data)
{
    auto an = std::make_unique<Animation>(detail::to_string(data.geometry_header.name));
    if (!parse_geometry(an.get(), data.geometry_header)) { return false; }
    an->anim_root = detail::to_string(data.root);
    an->length = data.length;
    an->transition_time = data.transition_time;

    size_t off = 0;
    if (!array_offset(data.events, detail::MdlBinaryAnimationEvent::s_sizeof, &off)) {
        return false;
    }
    an->events.reserve(data.events.length);
    for (size_t i = 0; i < data.events.length; ++i) {
        detail::MdlBinaryAnimationEvent event;
        if (!read_bytes(off, &event, sizeof(detail::MdlBinaryAnimationEvent))) {
            return false;
        }
        an->events.push_back({event.after, detail::to_string(event.name)});
        off += sizeof(detail::MdlBinaryAnimationEvent);
    }

    mdl_->model.animations.push_back(std::move(an));
    return true;
}

bool BinaryParser::parse_geometry(Geometry* geometry, const detail::MdlBinaryGeometryHeader& data)
{
    const size_t max_file_nodes = (bytes_.size() / detail::MdlBinaryNodeHeader::s_sizeof) + 1;
    const size_t max_nodes = std::max<size_t>(1, data.node_count);
    if (max_nodes > detail::max_binary_mdl_nodes || max_nodes > max_file_nodes) {
        LOG_F(ERROR, "invalid binary mdl: node count outside file bounds");
        return false;
    }

    geometry->nodes.reserve(max_nodes);
    if (!parse_node(data.root_node_offset, geometry, nullptr, max_nodes, 0)) {
        return false;
    }
    return true;
}

bool BinaryParser::parse_model(uint32_t offset)
{
    detail::MdlBinaryModelHeader model_header;
    if (!read_bytes(offset, &model_header, detail::MdlBinaryModelHeader::s_sizeof)) {
        return false;
    }

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

    size_t off = 0;
    if (!array_offset(model_header.animations, sizeof(uint32_t), &off)) {
        return false;
    }
    mdl_->model.animations.reserve(model_header.animations.length);
    for (size_t i = 0; i < model_header.animations.length; ++i) {
        uint32_t ptr;
        if (!read_bytes(off, &ptr, sizeof(uint32_t))) {
            return false;
        }

        detail::MdlBinaryAnimationHeader data;
        size_t anim_offset = 0;
        if (!pointer_offset(ptr, &anim_offset)
            || !read_bytes(anim_offset, &data, sizeof(detail::MdlBinaryAnimationHeader))) {
            return false;
        }
        if (!parse_anim(data)) {
            return false;
        }

        off += 4;
    }

    return true;
}

namespace {
thread_local detail::GeomCxt s_ctx;
}

bool BinaryParser::parse_node(uint32_t offset, Geometry* geometry, Node* parent, size_t max_nodes, size_t depth)
{
    if (depth >= max_nodes || geometry->nodes.size() >= max_nodes) {
        LOG_F(ERROR, "invalid binary mdl: node recursion exceeds declared node count");
        return false;
    }

    size_t node_offset = 0;
    detail::MdlBinaryNodeHeader nodehead;
    if (!pointer_offset(offset, &node_offset)
        || !read_bytes(node_offset, &nodehead, detail::MdlBinaryNodeHeader::s_sizeof)) {
        return false;
    }

    s_ctx.clear();

    std::unique_ptr<Node> node = mdl_->make_node(nodehead.type, detail::to_string(nodehead.name));
    if (!node) {
        return false;
    }
    node->parent = parent;
    node->inheritcolor = nodehead.inherit_color;

    size_t controller_data_offset = 0;
    if (!array_offset(nodehead.controller_data, sizeof(float), &controller_data_offset)) {
        return false;
    }
    node->controller_data.resize(nodehead.controller_data.length);
    if (!node->controller_data.empty()
        && !read_bytes(controller_data_offset, node->controller_data.data(),
            node->controller_data.size() * sizeof(float))) {
        return false;
    }

    size_t controller_key_offset = 0;
    if (!array_offset(nodehead.controller_keys, detail::MdlBinaryController::s_sizeof, &controller_key_offset)) {
        return false;
    }
    for (size_t i = 0; i < nodehead.controller_keys.length; ++i) {
        detail::MdlBinaryController old_c;
        if (!read_bytes(controller_key_offset, &old_c, sizeof(detail::MdlBinaryController))) {
            return false;
        }
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
        controller_key_offset += sizeof(detail::MdlBinaryController);
    }

    auto read_raw_array = [this](uint32_t raw_ptr, auto& out, size_t count) {
        using Out = std::remove_reference_t<decltype(out)>;
        using Value = typename Out::value_type;

        out.clear();
        if (count == 0) {
            return true;
        }
        if (raw_ptr == std::numeric_limits<uint32_t>::max()) {
            LOG_F(ERROR, "invalid binary mdl: missing raw mesh data");
            return false;
        }
        if (count > std::numeric_limits<size_t>::max() / sizeof(Value)) {
            LOG_F(ERROR, "invalid binary mdl: raw mesh data size overflow");
            return false;
        }

        size_t data_offset = 0;
        const size_t size = count * sizeof(Value);
        if (!raw_offset(raw_ptr, &data_offset) || !checked_range(data_offset, size)) {
            LOG_F(ERROR, "invalid binary mdl: raw mesh data outside buffer");
            return false;
        }

        out.resize(count);
        return read_bytes(data_offset, out.data(), size);
    };

    auto read_pointer_array = [this](const detail::MdlBinaryArray& array, auto& out) {
        using Out = std::remove_reference_t<decltype(out)>;
        using Value = typename Out::value_type;

        out.clear();
        size_t data_offset = 0;
        if (!array_offset(array, sizeof(Value), &data_offset)) {
            return false;
        }
        out.resize(array.length);
        if (out.empty()) {
            return true;
        }
        return read_bytes(data_offset, out.data(), out.size() * sizeof(Value));
    };

    auto reserve_indices = [](auto& indices, size_t face_count) {
        if (face_count > std::numeric_limits<size_t>::max() / 3) {
            LOG_F(ERROR, "invalid binary mdl: index count overflow");
            return false;
        }
        indices.reserve(face_count * 3);
        return true;
    };

    auto load_mesh_data = [&](TrimeshNode* mesh, const detail::MdlBinaryMeshHeader& data) {
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

        if (!read_raw_array(data.vertices, s_ctx.verts, data.vertex_count)) {
            return false;
        }
        if (data.texture0_vert_ptr != std::numeric_limits<uint32_t>::max()
            && !read_raw_array(data.texture0_vert_ptr, s_ctx.tverts[0], data.vertex_count)) {
            return false;
        }
        if (data.vertex_normals_ptr != std::numeric_limits<uint32_t>::max()
            && !read_raw_array(data.vertex_normals_ptr, s_ctx.normals, data.vertex_count)) {
            return false;
        }
        return read_pointer_array(data.faces, s_ctx.faces);
    };

    auto load_mesh_vertices = [&](TrimeshNode* mesh, const detail::MdlBinaryMeshHeader& data) {
        if (s_ctx.verts.size() != data.vertex_count) {
            LOG_F(ERROR, "invalid binary mdl: mesh vertex data missing");
            return false;
        }

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

        if (!reserve_indices(mesh->indices, data.faces.length)) {
            return false;
        }
        for (size_t i = 0; i < data.faces.length; ++i) {
            mesh->indices.push_back(s_ctx.faces[i].vertex_indicies[0]);
            mesh->indices.push_back(s_ctx.faces[i].vertex_indicies[1]);
            mesh->indices.push_back(s_ctx.faces[i].vertex_indicies[2]);
        }

        return true;
    };

    if (node->type == NodeType::trimesh) {
        detail::MdlBinaryTrimeshNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryTrimeshNode::s_sizeof)) { return false; }

        auto n = static_cast<TrimeshNode*>(node.get());
        if (!load_mesh_data(n, data.header) || !load_mesh_vertices(n, data.header)) { return false; }
    } else if (node->type == NodeType::reference) {
        detail::MdlBinaryReferenceNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryReferenceNode::s_sizeof)) { return false; }

        auto n = static_cast<ReferenceNode*>(node.get());
        n->reattachable = data.reattachable;
        n->refmodel = detail::to_string(data.ref);
    } else if (node->type == NodeType::danglymesh) {
        detail::MdlBinaryDanglymeshNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryDanglymeshNode::s_sizeof)) { return false; }

        auto n = static_cast<DanglymeshNode*>(node.get());
        if (!load_mesh_data(n, data.header) || !load_mesh_vertices(n, data.header)) { return false; }

        if (!read_pointer_array(data.vert_constraints, n->constraints)) {
            return false;
        }
        n->displacement = data.displacement;
        n->period = data.period;
        n->tightness = data.tightness;

    } else if (node->type == NodeType::emitter) {
        detail::MdlBinaryEmitterNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryEmitterNode::s_sizeof)) { return false; }

        auto n = static_cast<EmitterNode*>(node.get());
        n->deadspace = data.dead_space;
        n->blastradius = data.blast_radius;
        n->blastlength = data.blast_lenght;
        n->xgrid = data.x_grid;
        n->ygrid = data.y_grid;
        n->spawntype = static_cast<int32_t>(data.space_type);
        n->update = detail::to_string(data.update);
        n->render = detail::to_string(data.render);
        n->blend = detail::to_string(data.blend);
        n->texture = detail::to_string(data.texture);
        n->chunkname = detail::to_string(data.chunk_name);
        n->twosidedtex = data.twosided_texture;
        n->loop = data.loop;
        n->renderorder = data.render_order;
        n->flags = data.flags;
    } else if (node->type == NodeType::light) {
        detail::MdlBinaryLightNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryLightNode::s_sizeof)) { return false; }

        auto n = static_cast<LightNode*>(node.get());
        n->flareradius = data.flare_radius;
        n->lightpriority = data.priority;
        n->ambientonly = static_cast<int32_t>(data.ambient_only);
        n->dynamic = data.dynamic_type != 0;
        n->affectdynamic = data.affect_dynamic;
        n->shadow = data.shadow;
        n->generateflare = data.generate_flare;
        n->fadinglight = data.fading;
    } else if (node->type == NodeType::aabb) {
        detail::MdlBinaryAABBNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryAABBNode::s_sizeof)) { return false; }

        auto n = static_cast<AABBNode*>(node.get());
        if (!load_mesh_data(n, data.header) || !load_mesh_vertices(n, data.header)) { return false; }
    } else if (node->type == NodeType::skin) {
        detail::MdlBinarySkinNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinarySkinNode::s_sizeof)) { return false; }
        auto n = static_cast<SkinNode*>(node.get());

        if (!load_mesh_data(n, data.header)) {
            LOG_F(ERROR, "[model] skin node: failed to parse mesh header");
            return false;
        }

        for (size_t i = 0; i < 17; ++i) {
            n->bone_nodes[i] = int16_t(data.bone_to_nodes[i]);
        }

        if (!read_raw_array(data.bones_ptr, s_ctx.bones, s_ctx.verts.size())
            || !read_raw_array(data.weights_ptr, s_ctx.weights, s_ctx.verts.size())) {
            return false;
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
                n->vertices[i].bones = glm::ivec4{-1};
                for (size_t j = 0; j < 4; ++j) {
                    if (bones[j] == std::numeric_limits<uint16_t>::max()) { break; }
                    n->vertices[i].bones[j] = bones[j];
                }
            }
        }

        if (!reserve_indices(n->indices, data.header.faces.length)) {
            return false;
        }
        for (size_t i = 0; i < data.header.faces.length; ++i) {
            n->indices.push_back(s_ctx.faces[i].vertex_indicies[0]);
            n->indices.push_back(s_ctx.faces[i].vertex_indicies[1]);
            n->indices.push_back(s_ctx.faces[i].vertex_indicies[2]);
        }
    } else if (node->type == NodeType::animmesh) {
        detail::MdlBinaryAnimmeshNode data;
        if (!read_bytes(node_offset, &data, detail::MdlBinaryAnimmeshNode::s_sizeof)) { return false; }

        auto n = static_cast<AnimeshNode*>(node.get());
        if (!load_mesh_data(n, data.header)) { return false; }
    }

    geometry->nodes.push_back(std::move(node));
    auto n = geometry->nodes.back().get();
    if (parent) {
        parent->children.push_back(n);
    }

    size_t child_offset = 0;
    if (!array_offset(nodehead.children, sizeof(uint32_t), &child_offset)) {
        return false;
    }
    for (size_t i = 0; i < nodehead.children.length; ++i) {
        uint32_t ptr = 0;
        if (!read_bytes(child_offset, &ptr, sizeof(uint32_t))) {
            return false;
        }
        if (!parse_node(ptr, geometry, n, max_nodes, depth + 1)) {
            return false;
        }
        child_offset += sizeof(uint32_t);
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
