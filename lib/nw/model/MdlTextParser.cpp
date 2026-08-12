#include "MdlTextParser.hpp"

#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/scope_exit.hpp"
#include "../util/templates.hpp"
#include "Mdl.hpp"

#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

using namespace std::literals;

namespace nw::model {

using string::icmp;

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

    Vector<Face> faces;
    Vector<glm::vec3> verts;
    std::array<Vector<glm::vec3>, 4> tverts;
    Vector<glm::vec3> normals;
    Vector<glm::vec4> tangents;
    Vector<std::array<String, 4>> bones;
    Vector<glm::vec4> weights;
};

inline bool is_newline(StringView tk)
{
    if (tk.empty()) return false;
    return tk[0] == '\r' || tk[0] == '\n';
}

TextParser::TextParser(StringView buffer, Mdl* mdl)
    : tokens_(buffer, "#", false)
    , mdl_{mdl}
{
}

constexpr bool validate_tokens(std::initializer_list<StringView> tokens)
{
    for (auto tk : tokens) {
        if (tk.empty() || is_newline(tk)) {
            return false;
        }
    }
    return true;
}

bool parse_tokens(Tokenizer& tokens, StringView name, bool& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<bool>(tk)) {
        out = *res;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse bool, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, StringView name, String& out)
{
    auto tk = tokens.next();
    if (is_newline(tk)) { // Some things are there but don't have value
        out = "";
        tokens.put_back(tk);
        return true;
    } else if (!tk.empty()) {
        out = String(tk);
        string::tolower(&out);
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse string, line: {}", name, tokens.line());
    return false;
}

bool parse_line_tokens(Tokenizer& tokens, StringView name, String& out)
{
    auto tk = tokens.next();
    if (is_newline(tk)) {
        out = "";
        tokens.put_back(tk);
        return true;
    } else if (tk.empty()) {
        LOG_F(ERROR, "{}: Failed to parse line string, line: {}", name, tokens.line());
        return false;
    }

    out = String(tk);
    while (true) {
        tk = tokens.next();
        if (tk.empty()) { break; }
        if (is_newline(tk)) {
            tokens.put_back(tk);
            break;
        }
        out += " ";
        out += String(tk);
    }
    string::tolower(&out);
    return true;
}

bool parse_tokens(Tokenizer& tokens, StringView name, int32_t& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<int32_t>(tk)) {
        out = *res;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse int32_t, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, StringView name, uint32_t& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<uint32_t>(tk)) {
        out = *res;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse uint32_t, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, StringView name, float& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<float>(tk)) {
        out = *res;
        return true;
    }

    LOG_F(ERROR, "{}: Failed to parse float - got '{}', line: {}", name, tk, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, StringView name, glm::vec2& out)
{
    if (!parse_tokens(tokens, name, out.x) || !parse_tokens(tokens, name, out.y)) {
        LOG_F(ERROR, "{}: Failed to parse Vector2, line: {}", name, tokens.line());
        return false;
    }

    return true;
}

bool parse_tokens(Tokenizer& tokens, StringView name, glm::vec3& out)
{
    auto x = string::from<float>(tokens.next());
    auto y = string::from<float>(tokens.next());
    auto z = string::from<float>(tokens.next());

    if (x && y && z) {
        out.x = *x;
        out.y = *y;
        out.z = *z;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse vec3, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, StringView name, glm::vec4& out)
{
    if (!parse_tokens(tokens, name, out.x)
        || !parse_tokens(tokens, name, out.y)
        || !parse_tokens(tokens, name, out.z)
        || !parse_tokens(tokens, name, out.w)) {
        LOG_F(ERROR, "{}: Failed to parse Vector4, line: {}", name, tokens.line());
        return false;
    }

    return true;
}

bool parse_tokens(Tokenizer& tokens, StringView name, glm::quat& out)
{
    if (!parse_tokens(tokens, name, out.x)
        || !parse_tokens(tokens, name, out.y)
        || !parse_tokens(tokens, name, out.z)
        || !parse_tokens(tokens, name, out.w)) {
        LOG_F(ERROR, "{}: Failed to parse quaternion, line: {}", name, tokens.line());
        return false;
    }
    return true;
}

bool parse_tokens(Tokenizer& tokens, StringView name, Face& out)
{
    if (!parse_tokens(tokens, name, out.vert_idx[0])
        || !parse_tokens(tokens, name, out.vert_idx[1])
        || !parse_tokens(tokens, name, out.vert_idx[2])
        || !parse_tokens(tokens, name, out.shader_group_idx)
        || !parse_tokens(tokens, name, out.tvert_idx[0])
        || !parse_tokens(tokens, name, out.tvert_idx[1])
        || !parse_tokens(tokens, name, out.tvert_idx[2])
        || !parse_tokens(tokens, name, out.material_idx)) {
        LOG_F(ERROR, "Failed to parse Face, line: {}", tokens.line());
        return false;
    }
    return true;
}

bool parse_tokens(Tokenizer& tokens, StringView name, AABBNode* node)
{
    // Will have to create the tree structure later.
    while (true) {
        AABBEntry out;
        if (!parse_tokens(tokens, name, out.bmin)
            || !parse_tokens(tokens, name, out.bmax)
            || !parse_tokens(tokens, name, out.leaf_face)) {
            LOG_F(ERROR, "Failed to parse Face, line: {}", tokens.line());
            return false;
        }
        node->entries.push_back(out);
        tokens.next(); // Drop new line.
        auto tk = tokens.next();
        // If the next token is a new line or empty the AABB is done.
        if (tokens.is_newline(tk) || tk.empty() || !string::from<float>(tk)) {
            tokens.put_back(tk);
            break;
        } else {
            tokens.put_back(tk);
        }
    }
    return true;
}

template <typename T>
bool parse_tokens(Tokenizer& tokens, StringView name, Vector<T>& out)
{
    uint32_t size;
    if (!parse_tokens(tokens, name, size)) return false;
    out.reserve(size);
    tokens.next(); // drop new line.
    for (uint32_t i = 0; i < size; ++i) {
        T v;
        if (!parse_tokens(tokens, name, v)) return false;
        out.push_back(std::move(v));
        tokens.next(); // drop new line.
    }
    auto tk = tokens.next();
    if (!icmp(tk, "endlist")) { tokens.put_back(tk); }
    return true;
}

template <typename T, typename VertType>
bool cleanup_geometry(Model* model, T* n, const GeomCxt& geomctx)
{
    if (geomctx.verts.empty()) {
        return true;
    }

    const size_t source_vertex_count = geomctx.verts.size();
    const bool has_texcoords = !geomctx.tverts[0].empty();
    constexpr auto max_vertex_index = std::numeric_limits<uint16_t>::max();

    auto face_valid = [&](const Face& face) {
        for (size_t corner = 0; corner < 3; ++corner) {
            if (face.vert_idx[corner] >= source_vertex_count
                || face.vert_idx[corner] > max_vertex_index
                || (has_texcoords && face.tvert_idx[corner] >= geomctx.tverts[0].size())) {
                return false;
            }
        }
        return true;
    };

    std::vector<uint8_t> valid_faces(geomctx.faces.size(), 0);
    size_t dropped_faces = 0;
    for (size_t i = 0; i < geomctx.faces.size(); ++i) {
        valid_faces[i] = face_valid(geomctx.faces[i]);
        dropped_faces += valid_faces[i] == 0;
    }
    if (dropped_faces != 0) {
        LOG_F(WARNING, "invalid text mdl: dropped {} mesh faces with invalid position or texture indices", dropped_faces);
    }

    std::vector<glm::vec3> generated_normals(source_vertex_count, glm::vec3{0.0f});
    for (size_t i = 0; i < geomctx.faces.size(); ++i) {
        if (!valid_faces[i]) {
            continue;
        }
        const auto& face = geomctx.faces[i];
        const auto& p0 = geomctx.verts[face.vert_idx[0]];
        const auto& p1 = geomctx.verts[face.vert_idx[1]];
        const auto& p2 = geomctx.verts[face.vert_idx[2]];
        const glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
        const float length_squared = glm::dot(cross, cross);
        if (!std::isfinite(length_squared) || length_squared <= 1.0e-12f) {
            continue;
        }
        const glm::vec3 normal = cross / std::sqrt(length_squared);
        for (const auto vertex_index : face.vert_idx) {
            generated_normals[vertex_index] += normal;
        }
    }

    std::vector<VertType> source_vertices(source_vertex_count);
    for (size_t i = 0; i < source_vertex_count; ++i) {
        auto& vertex = source_vertices[i];
        vertex.position = geomctx.verts[i];
        if (i < geomctx.normals.size()) {
            vertex.normal = geomctx.normals[i];
        } else {
            const float length_squared = glm::dot(generated_normals[i], generated_normals[i]);
            vertex.normal = std::isfinite(length_squared) && length_squared > 1.0e-12f
                ? generated_normals[i] / std::sqrt(length_squared)
                : glm::vec3{0.0f, 0.0f, 1.0f};
        }
        if (i < geomctx.tangents.size()) {
            vertex.tangent = geomctx.tangents[i];
        }
    }

    if constexpr (std::is_same_v<T, SkinNode>) {
        if (geomctx.weights.size() != source_vertex_count || geomctx.bones.size() != source_vertex_count) {
            LOG_F(ERROR, "invalid text mdl: skin vertex, weight, and bone counts differ");
            return false;
        }
        for (size_t vertex_index = 0; vertex_index < source_vertex_count; ++vertex_index) {
            auto& vertex = source_vertices[vertex_index];
            vertex.weights = geomctx.weights[vertex_index];
            vertex.bones = glm::ivec4{-1};
            const auto& bones = geomctx.bones[vertex_index];
            for (size_t lane = 0; lane < bones.size(); ++lane) {
                if (bones[lane].empty()) {
                    break;
                }
                for (size_t node_index = 0; node_index < model->nodes.size(); ++node_index) {
                    if (!string::icmp(model->nodes[node_index]->name, bones[lane])) {
                        continue;
                    }
                    for (size_t bone_index = 0; bone_index < n->bone_nodes.size(); ++bone_index) {
                        if (n->bone_nodes[bone_index] == static_cast<int16_t>(node_index)) {
                            vertex.bones[static_cast<glm::length_t>(lane)] = static_cast<int>(bone_index);
                            break;
                        }
                        if (n->bone_nodes[bone_index] == -1) {
                            vertex.bones[static_cast<glm::length_t>(lane)] = static_cast<int>(bone_index);
                            n->bone_nodes[bone_index] = static_cast<int16_t>(node_index);
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    n->indices.clear();
    n->indices.reserve((geomctx.faces.size() - dropped_faces) * 3);
    std::vector<uint32_t> output_source_indices;
    if (!has_texcoords) {
        n->vertices = std::move(source_vertices);
        for (size_t i = 0; i < geomctx.faces.size(); ++i) {
            if (!valid_faces[i]) {
                continue;
            }
            for (const auto vertex_index : geomctx.faces[i].vert_idx) {
                n->indices.push_back(static_cast<uint16_t>(vertex_index));
            }
        }
    } else {
        const size_t corner_count = (geomctx.faces.size() - dropped_faces) * 3;
        n->vertices.clear();
        n->vertices.reserve(std::min(corner_count, static_cast<size_t>(max_vertex_index) + 1));
        output_source_indices.reserve(n->vertices.capacity());
        std::unordered_map<uint64_t, uint16_t> corner_vertices;
        corner_vertices.reserve(corner_count);

        // The render vertex stream has one index, so every distinct position/UV pair is a vertex.
        for (size_t i = 0; i < geomctx.faces.size(); ++i) {
            if (!valid_faces[i]) {
                continue;
            }
            const auto& face = geomctx.faces[i];
            for (size_t corner = 0; corner < 3; ++corner) {
                const uint32_t source_index = face.vert_idx[corner];
                const uint32_t texcoord_index = face.tvert_idx[corner];
                const uint64_t key = (static_cast<uint64_t>(source_index) << 32u) | texcoord_index;
                auto [it, inserted] = corner_vertices.try_emplace(key, 0);
                if (inserted) {
                    if (n->vertices.size() > max_vertex_index) {
                        LOG_F(ERROR, "invalid text mdl: expanded mesh exceeds 16-bit vertex index range");
                        return false;
                    }
                    it->second = static_cast<uint16_t>(n->vertices.size());
                    auto vertex = source_vertices[source_index];
                    const auto& texcoord = geomctx.tverts[0][texcoord_index];
                    vertex.tex_coords = glm::vec2{texcoord.x, texcoord.y};
                    n->vertices.push_back(vertex);
                    output_source_indices.push_back(source_index);
                }
                n->indices.push_back(it->second);
            }
        }

        const auto remap_vertex_values = [&](auto& values) {
            if (values.size() != source_vertex_count) {
                return;
            }
            using Value = typename std::decay_t<decltype(values)>::value_type;
            std::vector<Value> remapped;
            remapped.reserve(output_source_indices.size());
            for (const auto source_index : output_source_indices) {
                remapped.push_back(values[source_index]);
            }
            values = std::move(remapped);
        };
        remap_vertex_values(n->colors);
        if (auto* animmesh = dynamic_cast<AnimeshNode*>(n)) {
            remap_vertex_values(animmesh->animverts);
            remap_vertex_values(animmesh->animtverts);
        }
        if (auto* danglymesh = dynamic_cast<DanglymeshNode*>(n)) {
            remap_vertex_values(danglymesh->constraints);
        }
    }

    if (geomctx.tangents.empty()) {
        std::vector<glm::vec3> tangent_sums(n->vertices.size(), glm::vec3{0.0f});
        std::vector<glm::vec3> bitangent_sums(n->vertices.size(), glm::vec3{0.0f});
        for (size_t i = 0; i + 2 < n->indices.size(); i += 3) {
            const auto i0 = n->indices[i];
            const auto i1 = n->indices[i + 1];
            const auto i2 = n->indices[i + 2];
            const auto& v0 = n->vertices[i0];
            const auto& v1 = n->vertices[i1];
            const auto& v2 = n->vertices[i2];
            const glm::vec3 edge1 = v1.position - v0.position;
            const glm::vec3 edge2 = v2.position - v0.position;
            const glm::vec2 uv1 = v1.tex_coords - v0.tex_coords;
            const glm::vec2 uv2 = v2.tex_coords - v0.tex_coords;
            const float determinant = uv1.x * uv2.y - uv2.x * uv1.y;
            if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-12f) {
                continue;
            }
            const float inverse = 1.0f / determinant;
            const glm::vec3 tangent = (edge1 * uv2.y - edge2 * uv1.y) * inverse;
            const glm::vec3 bitangent = (edge2 * uv1.x - edge1 * uv2.x) * inverse;
            tangent_sums[i0] += tangent;
            tangent_sums[i1] += tangent;
            tangent_sums[i2] += tangent;
            bitangent_sums[i0] += bitangent;
            bitangent_sums[i1] += bitangent;
            bitangent_sums[i2] += bitangent;
        }

        for (size_t i = 0; i < n->vertices.size(); ++i) {
            auto& vertex = n->vertices[i];
            const glm::vec3 orthogonal = tangent_sums[i]
                - vertex.normal * glm::dot(vertex.normal, tangent_sums[i]);
            const float length_squared = glm::dot(orthogonal, orthogonal);
            if (!std::isfinite(length_squared) || length_squared <= 1.0e-12f) {
                vertex.tangent = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
                continue;
            }
            const glm::vec3 tangent = orthogonal / std::sqrt(length_squared);
            const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangent_sums[i]) < 0.0f
                ? -1.0f
                : 1.0f;
            vertex.tangent = glm::vec4{tangent, handedness};
        }
    }

    return true;
}

bool TextParser::parse_controller(Node* node, StringView name, uint32_t type)
{
    size_t start_line = tokens_.line();
    StringView tk = tokens_.next();
    while (is_newline(tk))
        tk = tokens_.next();

    Vector<float> time;
    Vector<float> data;
    data.reserve(128);

    // Special case detonate
    if (name == "detonate") {
        // This is stupid, but oh well.  If there is something there just drop it.
        // if there isn't put crap back the way it was.
        auto opt = string::from<float>(tk);
        if (!opt) {
            tokens_.put_back(tk);
            tokens_.put_back("\n");
        }
        node->add_controller_data(name, type, {}, data, 1, -1);
        return true;
    } else if (name == "detonatekey") {
        auto opt = string::from<float>(tk);
        if (!opt) {
            LOG_F(ERROR, "Failed to parse float: {}, {}", name, tokens_.line());
            return false;
        }
        data.push_back(*opt);

        node->add_controller_data(name, type, {}, data, 1, 1);
        return true;
    }

    if (!string::endswith(name, "key")) {
        // All the data is going to be on one line or there may be no data
        while (!tk.empty() && !is_newline(tk)) {
            auto opt = string::from<float>(tk);
            if (!opt) {
                LOG_F(ERROR, "Failed to parse float: {}, {}", name, tokens_.line());
                return false;
            }
            data.push_back(*opt);
            tk = tokens_.next();
        }
        node->add_controller_data(name, type, {}, data, 1, int(data.size()));
        return true;
    }

    int max_rows = -1;
    if (start_line == tokens_.line()) {
        tokens_.put_back(tk);
        if (!parse_tokens(tokens_, "key row count", max_rows)) {
            return false;
        }
        tk = tokens_.next();
        while (is_newline(tk))
            tk = tokens_.next();
    }

    int colsize = -1;
    int rows = 0;

    bool is_time = true;
    while (!tk.empty()) {
        if (is_newline(tk)) {
            if (colsize == -1) {
                colsize = int(data.size());
            }
            if (data.size() % colsize) {
                LOG_F(ERROR, "{}: Mismatched column size, expected: {}, got: {}, on line: {}",
                    name, colsize, data.size() % colsize, tokens_.line());
                return false;
            }
            ++rows;
            is_time = true;
            if (max_rows > 0) { --max_rows; }
            while (is_newline(tk))
                tk = tokens_.next();
        }
        if (tk == "endlist" || max_rows == 0) break;

        auto opt = string::from<float>(tk);
        if (!opt) {
            LOG_F(ERROR, "{}: Failed to parse float: {}", name, tk);
            return false;
        }
        if (is_time) {
            time.push_back(*opt);
            is_time = false;
        } else {
            data.push_back(*opt);
        }
        tk = tokens_.next();
    }
    node->add_controller_data(name, type, time, data, rows, colsize);
    // Some controller lists have both row count and 'endlist'..
    if (max_rows == 0 && tk != "endlist") { tokens_.put_back(tk); }
    return tk == "endlist" || max_rows == 0;
}

#define DROP_DATA(name)                     \
    if (icmp(tk, ROLLNW_STRINGIFY(name))) { \
        tokens_.next();                     \
        continue;                           \
    }

#define PARSE_DATA(name, node)                        \
    if (icmp(tk, ROLLNW_STRINGIFY(name))) {           \
        if (!parse_tokens(tokens_, tk, node->name)) { \
            return false;                             \
        } else {                                      \
            continue;                                 \
        }                                             \
    }

#define PARSE_DATA_TO(name, node, target)               \
    if (icmp(tk, ROLLNW_STRINGIFY(name))) {             \
        if (!parse_tokens(tokens_, tk, node->target)) { \
            return false;                               \
        } else {                                        \
            continue;                                   \
        }                                               \
    }

namespace {
thread_local GeomCxt geomctx;
}

bool TextParser::parse_node(Geometry* geometry)
{
    geomctx.clear();

    bool result = true;
    StringView tktype = tokens_.next();
    if (tktype.empty()) {
        LOG_F(ERROR, "Missing node type, line: {}", tokens_.line());
        return false;
    }

    StringView tkname = tokens_.next();
    if (tkname.empty()) {
        LOG_F(ERROR, "Missing node name, line: {}", tokens_.line());
        return false;
    }

    uint32_t type = NodeType::from_string(tktype);

    auto node = mdl_->make_node(type, tkname);
    if (!node) return false;

    StringView tk;
    for (tk = tokens_.next(); !tk.empty() && result; tk = tokens_.next()) {
        if (is_newline(tk)) {
            continue;
        } else if (icmp(tk, "endnode")) {
            break;
        }

        StringView controller_tk = tk;
        if (string::endswith(tk, "bezierkey")) { // No models seem to use this?
            controller_tk = StringView(tk.data(), tk.size() - 9);
        } else if (string::endswith(tk, "key")) {
            // Guess all this is obsolete?
            if (tk == "centerkey" || tk == "gizmokey") {
                tk = tokens_.next();
                while (is_newline(tk))
                    tk = tokens_.next();

                if (tk != "endlist") {
                    LOG_F(ERROR, "invalid controller, {}, line: {}", tk, tokens_.line());
                    return false;
                } else {
                    continue;
                }
            } else if (tk == "birthratekeykey") { // yes..
                tk = "birthratekey";
            }
            controller_tk = StringView(tk.data(), tk.size() - 3);
        } else if (icmp(tk, "setfillumcolor")) {
            controller_tk = "selfillumcolor";
        }

        // Check if it's a controller.
        auto [ctype, ntype] = ControllerType::lookup(controller_tk);
        if (ctype != 0) {
            if (!(node->type & ntype)) {
                LOG_F(ERROR, "Controller set on an incompatible node: {}, line: {}", tk, tokens_.line());
                return false;
            }
            if (!parse_controller(node.get(), tk, ctype)) return false;
            continue;
        } else if (string::endswith(tk, "key") && (node->type & NodeFlags::emitter)) {
            uint32_t keyed_emitter_ctype = 0;
            if (string::icmp(controller_tk, "spawnType")) {
                keyed_emitter_ctype = ControllerType::spawn_type;
            } else if (string::icmp(controller_tk, "random")) {
                keyed_emitter_ctype = ControllerType::random;
            } else if (string::icmp(controller_tk, "inherit")) {
                keyed_emitter_ctype = ControllerType::inherit;
            } else if (string::icmp(controller_tk, "inheritvel")) {
                keyed_emitter_ctype = ControllerType::inheritvel;
            } else if (string::icmp(controller_tk, "inherit_local")) {
                keyed_emitter_ctype = ControllerType::inherit_local;
            } else if (string::icmp(controller_tk, "inherit_part")) {
                keyed_emitter_ctype = ControllerType::inherit_part;
            }

            if (keyed_emitter_ctype != 0) {
                if (!parse_controller(node.get(), tk, keyed_emitter_ctype)) return false;
                continue;
            }

            if (!parse_controller(node.get(), tk, 0)) return false;
            continue;
        }

        if (icmp(tk, "parent")) {
            tk = tokens_.next();
            if (!icmp(tk, "NULL")) {
                bool parent_set = false;
                for (auto& n : reverse(geometry->nodes)) {
                    if (n->name == tk) {
                        node->parent = n.get();
                        n->children.push_back(node.get());
                        parent_set = true;
                        break;
                    }
                }
                if (!parent_set) {
                    LOG_F(ERROR, "Unable to find parent node: '{}'", tk);
                    return false;
                }
            }
            continue;
        }

        if (node->type & NodeFlags::mesh) {
            TrimeshNode* n = static_cast<TrimeshNode*>(node.get());

            PARSE_DATA(beaming, n)
            if (icmp(tk, "bitmap")) {
                if (!parse_line_tokens(tokens_, tk, n->bitmap)) {
                    return false;
                } else {
                    continue;
                }
            }
            PARSE_DATA(bmax, n)
            PARSE_DATA(bmin, n)

            if (icmp(tk, "center")) { // Unused
                tk = tokens_.next();  // undefined or <float>
                if (tk != "undefined") {
                    tokens_.next(); // drop next two <float>
                    tokens_.next();
                }
                continue;
            }

            // An ascii exporter "NWNUnity" has 4 floats for diffuse and specular
            if (icmp(tk, "diffuse")) {
                if (!parse_tokens(tokens_, tk, n->diffuse)) {
                    return false;
                }
                tokens_.next();
                continue;
            }

            if (icmp(tk, "specular")) {
                if (!parse_tokens(tokens_, tk, n->specular)) {
                    return false;
                }
                tokens_.next();
                continue;
            }

            if (icmp(tk, "ambient")) {
                if (!parse_tokens(tokens_, tk, n->specular)) {
                    return false;
                }
                tokens_.next();
                continue;
            }

            PARSE_DATA(colors, n)
            PARSE_DATA(inheritcolor, n)
            PARSE_DATA(materialname, n)
            DROP_DATA(gizmo)
            DROP_DATA(danglymesh)
            DROP_DATA(period)
            DROP_DATA(tightness)
            DROP_DATA(displacement)
            PARSE_DATA(render, n)
            PARSE_DATA(renderhint, n)
            PARSE_DATA(rotatetexture, n)
            PARSE_DATA(shadow, n)
            PARSE_DATA(shininess, n)
            PARSE_DATA_TO(texture0, n, textures[0])
            PARSE_DATA_TO(texture1, n, textures[1])
            PARSE_DATA_TO(texture2, n, textures[2])
            PARSE_DATA(tilefade, n)
            PARSE_DATA(transparencyhint, n)
            PARSE_DATA(showdispl, n)
            PARSE_DATA(displtype, n)
            PARSE_DATA(lightmapped, n)

            if (icmp(tk, "faces")) {
                if (!parse_tokens(tokens_, tk, geomctx.faces)) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "tverts")) {
                if (!parse_tokens(tokens_, tk, geomctx.tverts[0])) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "tverts1")) {
                if (!parse_tokens(tokens_, tk, geomctx.tverts[1])) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "tverts2")) {
                if (!parse_tokens(tokens_, tk, geomctx.tverts[2])) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "tverts3")) {
                if (!parse_tokens(tokens_, tk, geomctx.tverts[3])) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "verts")) {
                if (!parse_tokens(tokens_, tk, geomctx.verts)) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "normals")) {
                if (!parse_tokens(tokens_, tk, geomctx.normals)) {
                    return false;
                } else {
                    continue;
                }
            }
            if (icmp(tk, "tangents")) {
                if (!parse_tokens(tokens_, tk, geomctx.tangents)) {
                    return false;
                } else {
                    continue;
                }
            }

            if (tk == "multimaterial") {
                tk = tokens_.next();
                auto rows = string::from<uint32_t>(tk);
                if (!rows) {
                    LOG_F(ERROR, "expected row count, got {}, line: {}", tk, tokens_.line());
                    return false;
                }

                tk = tokens_.next();
                for (uint32_t i = 0; i < *rows; ++i) {
                    while (is_newline(tk))
                        tk = tokens_.next();

                    String current{tk};
                    for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
                        if (is_newline(tk)) { break; }
                        current += " " + String(tk);
                    }
                    n->multimaterial.push_back(current);
                }
                continue;
            }
        }

        if (node->type & NodeFlags::reference) {
            ReferenceNode* n = static_cast<ReferenceNode*>(node.get());
            PARSE_DATA(reattachable, n)
            PARSE_DATA(refmodel, n)
            if (tk == "Dummy") { // There is a weird "Dummy Dummy" entry in some reference nodes.  Dunno.
                tokens_.next();
                continue;
            }
        }

        if (node->type & NodeFlags::dangly) {
            DanglymeshNode* n = static_cast<DanglymeshNode*>(node.get());
            PARSE_DATA(constraints, n)
            PARSE_DATA(displacement, n)
            PARSE_DATA(period, n)
            PARSE_DATA(tightness, n)
        }

        if (node->type & NodeFlags::emitter) {
            EmitterNode* n = static_cast<EmitterNode*>(node.get());
            PARSE_DATA(blastlength, n)
            PARSE_DATA(blastradius, n)
            PARSE_DATA(blend, n)
            PARSE_DATA(chunkname, n)
            PARSE_DATA(deadspace, n)
            PARSE_DATA(loop, n)
            PARSE_DATA(render, n)
            PARSE_DATA(renderorder, n)
            PARSE_DATA(spawntype, n)
            PARSE_DATA(texture, n)
            PARSE_DATA(twosidedtex, n)
            PARSE_DATA(update, n)
            PARSE_DATA(xgrid, n)
            PARSE_DATA(ygrid, n)
            PARSE_DATA(render_sel, n)
            PARSE_DATA(blend_sel, n)
            PARSE_DATA(update_sel, n)
            PARSE_DATA(spawntype_sel, n)
            PARSE_DATA(opacity, n)
            PARSE_DATA(p2p_type, n)
            PARSE_DATA(tilefade, n)

            bool value;
            if (icmp(tk, "affectedByWind")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::AffectedByWind;
                continue;
            } else if (icmp(tk, "bounce")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::Bounce;
                continue;
            } else if (icmp(tk, "inherit")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::Inherit;
                continue;
            } else if (icmp(tk, "inherit_local")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::InheritLocal;
                continue;
            } else if (icmp(tk, "inherit_part")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::InheritPart;
                continue;
            } else if (icmp(tk, "inheritvel")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::InheritVel;
                continue;
            } else if (icmp(tk, "m_isTinted") || icmp(tk, "m_istnited")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::IsTinted;
                continue;
            } else if (icmp(tk, "p2p")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::P2P;
                continue;
            } else if (icmp(tk, "p2p_sel")) {
                uint32_t v;
                if (!parse_tokens(tokens_, tk, v)) return false;
                if (v) n->flags |= EmitterFlag::P2PSel;
                continue;
            } else if (icmp(tk, "random")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::Random;
                continue;
            } else if (icmp(tk, "splat")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= EmitterFlag::Splat;
                continue;
            }
        }

        if (node->type & NodeFlags::light) {
            LightNode* n = static_cast<LightNode*>(node.get());
            PARSE_DATA(lensflares, n)
            PARSE_DATA(affectdynamic, n)
            PARSE_DATA_TO(affect_dynamic, n, affectdynamic) // yes..
            PARSE_DATA(ambientonly, n)
            PARSE_DATA_TO(ambient_only, n, ambientonly) // yes..
            PARSE_DATA(fadinglight, n)
            PARSE_DATA_TO(fading_light, n, fadinglight)
            PARSE_DATA(flarecolorshifts, n)
            PARSE_DATA(flarepositions, n)
            PARSE_DATA(flareradius, n)
            PARSE_DATA(flaresizes, n)
            PARSE_DATA(generateflare, n)
            if (icmp(tk, "isdynamic") || icmp(tk, "n_dynamic_type")) {
                // Comment out below, a lot of base game models use this.
                // LOG_F(WARNING, "'isdynamic' is obsolete, use 'nDynamicType'");
                if (!parse_tokens(tokens_, tk, n->dynamic)) return false;
                continue;
            }
            PARSE_DATA(lightpriority, n)
            PARSE_DATA_TO(nDynamicType, n, dynamic)
            PARSE_DATA(shadow, n)
            PARSE_DATA_TO(texturenames, n, textures);
        }

        if (node->type & NodeFlags::aabb) {
            AABBNode* n = static_cast<AABBNode*>(node.get());
            if (icmp(tk, "aabb")) {
                if (!parse_tokens(tokens_, "aabb", n)) {
                    return false;
                } else {
                    continue;
                }
            }
        }

        if (node->type & NodeFlags::skin) {
            if (icmp(tk, "weights")) {
                uint32_t size;
                if (!parse_tokens(tokens_, "weights: size", size)) { return false; }
                tokens_.next(); // drop new line.
                for (uint32_t i = 0; i < size; ++i) {
                    std::array<String, 4> bones;
                    glm::vec4 weights{};
                    for (uint32_t j = 0; j < 4; ++j) {
                        if (!parse_tokens(tokens_, "weight: bone", bones[j])
                            || !parse_tokens(tokens_, "weight: value", weights[j])) {
                            LOG_F(ERROR, "Failed to parse skin weight {}, line: {}", i, tokens_.line());
                            return false;
                        }
                        tk = tokens_.next();
                        if (is_newline(tk)) {
                            geomctx.bones.push_back(bones);
                            geomctx.weights.push_back(weights);
                            break;
                        } else {
                            tokens_.put_back(tk);
                        }
                    }
                }
                auto end_tk = tokens_.next();
                if (!icmp(end_tk, "endlist")) { tokens_.put_back(end_tk); }
                continue;
            }
        }

        if (node->type & NodeFlags::anim) {
            AnimeshNode* n = static_cast<AnimeshNode*>(node.get());
            PARSE_DATA(animtverts, n);
            PARSE_DATA(animverts, n);
            PARSE_DATA(sampleperiod, n);
            PARSE_DATA(cliph, n);
            PARSE_DATA(clipw, n);
            PARSE_DATA(clipv, n);
            PARSE_DATA(clipu, n);
        }

        if (tk == "endlist") { // yes.. random ass 'endlist's in some models
            continue;
        }

        LOG_F(ERROR, "Unknown token: '{}', line: {}", tk, tokens_.line());
        return false;
    }

    // Cleanup geometry data
    if (node->type & NodeFlags::skin) {
        SkinNode* n = static_cast<SkinNode*>(node.get());
        if (!cleanup_geometry<SkinNode, SkinVertex>(&mdl_->model, n, geomctx)) {
            return false;
        }
    } else if (node->type & NodeFlags::mesh) {
        TrimeshNode* n = static_cast<TrimeshNode*>(node.get());
        if (!cleanup_geometry<TrimeshNode, Vertex>(&mdl_->model, n, geomctx)) {
            return false;
        }
    }

    geometry->nodes.push_back(std::move(node));

    return tk == "endnode";
}

bool TextParser::parse_geometry()
{
    StringView tk;
    for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
        if (is_newline(tk))
            continue;
        if (tk == "node") {
            if (!parse_node(&mdl_->model)) return false;
        } else if (tk == "endmodelgeom") {
            tokens_.next(); // drop name
            break;
        }
    }
    return tk == "endmodelgeom";
}

bool TextParser::parse_anim()
{
    StringView tk = tokens_.next();
    auto anim = std::make_unique<Animation>(String(tk));
    tokens_.next(); // drop model name

    for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
        if (is_newline(tk)) continue;

        if (icmp(tk, "doneanim")) {
            tokens_.next(); // drop name
            tokens_.next(); // drop model name
            break;
        } else if (icmp(tk, "animroot")) {
            if (!parse_tokens(tokens_, "animroot", anim->anim_root))
                return false;
        } else if (icmp(tk, "event")) {
            AnimationEvent ev;
            if (parse_tokens(tokens_, "time", ev.time)
                && parse_tokens(tokens_, "name", ev.name)) {
                anim->events.push_back(std::move(ev));
            } else {
                LOG_F(ERROR, "event parsing failed");
                return false;
            }
        } else if (icmp(tk, "length")) {
            if (!parse_tokens(tokens_, "length", anim->length))
                return false;
        } else if (icmp(tk, "node")) {
            if (!parse_node(anim.get())) {
                LOG_F(ERROR, "node parsing failed");
                return false;
            }
        } else if (icmp(tk, "transtime")) {
            if (!parse_tokens(tokens_, "transtime", anim->transition_time))
                return false;
        }
    }

    mdl_->model.animations.push_back(std::move(anim));
    return icmp(tk, "doneanim");
}

bool TextParser::parse_model()
{
    StringView tk = tokens_.next();
    mdl_->model.name = String(tk);

    for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
        if (is_newline(tk))
            continue;
        else if (tk == "donemodel") {
            tokens_.next(); // drop model name
            break;
        } else if (tk == "setsupermodel") {
            if (!validate_tokens({tokens_.next()})) { // Don't care about the file name.
                LOG_F(ERROR, "Missing super model file name, line: {}.", tokens_.line());
                return false;
            }
            if (!parse_tokens(tokens_, tk, mdl_->model.supermodel_name)) return false;
        } else if (tk == "classification") {
            String name;
            if (!parse_tokens(tokens_, tk, name)) return false;
            if (icmp(name, "character"))
                mdl_->model.classification = ModelClass::character;
            else if (icmp(name, "door"))
                mdl_->model.classification = ModelClass::door;
            else if (icmp(name, "effect") || icmp(name, "effects"))
                mdl_->model.classification = ModelClass::effect;
            else if (icmp(name, "tile"))
                mdl_->model.classification = ModelClass::tile;
            else if (icmp(name, "item"))
                mdl_->model.classification = ModelClass::item;
            else if (icmp(name, "gui"))
                mdl_->model.classification = ModelClass::gui;
            else if (icmp(name, "unknown") || icmp(name, "other")) // not sure what other is about
                mdl_->model.classification = ModelClass::invalid;
            else {
                LOG_F(ERROR, "Unknown Model Classification {}, line: {}", name, tokens_.line());
                return false;
            }
        } else if (tk == "ignorefog") {
            if (!parse_tokens(tokens_, tk, mdl_->model.ignorefog)) return false;
        } else if (tk == "setanimationscale") {
            if (!parse_tokens(tokens_, tk, mdl_->model.animationscale)) return false;
        } else if (tk == "beginmodelgeom") {
            if (!parse_geometry()) return false;
        } else if (tk == "newanim") {
            if (!parse_anim()) { return false; }
        } else if (icmp(mdl_->model.name, tk)) {
            // I dunno... just random bug in nwmax exporter??
            continue;
        } else {
            LOG_F(ERROR, "unknown token '{}', line: {}", tk, tokens_.line());
            return false;
        }
    }

    return tk == "donemodel";
}

bool TextParser::parse()
{
    bool result = true;
    for (StringView tk = tokens_.next(); !tk.empty() && result; tk = tokens_.next()) {
        if (is_newline(tk)) continue;
        // both spellings of this appear to be present in vanilla models and community tools
        // consume whatever is there till end of line.
        if (tk == "filedependancy" || tk == "filedependency") {
            auto t = tokens_.next();
            if (is_newline(t)) { continue; } // one model it's empty..
            mdl_->model.file_dependency = String(t);
            for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
                if (is_newline(tk)) { break; }
                mdl_->model.file_dependency += " " + String(tk);
            }
        } else if (tk == "newmodel") {
            if (!parse_model()) return false;
        } else {
            LOG_F(ERROR, "unknown token '{}', line: {}", tk, tokens_.line());
            return false;
        }
    }

    return result;
}

} // namespace nw::model
