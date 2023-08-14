#include "MdlTextParser.hpp"

#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/scope_exit.hpp"
#include "../util/templates.hpp"
#include "Mdl.hpp"

#include <absl/container/flat_hash_set.h>
#include <glm/gtx/normal.hpp>

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

    std::vector<Face> faces;
    std::vector<glm::vec3> verts;
    std::array<std::vector<glm::vec3>, 4> tverts;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec4> tangents;
    std::vector<std::array<std::string, 4>> bones;
    std::vector<glm::vec4> weights;
};

inline bool is_newline(std::string_view tk)
{
    if (tk.empty()) return false;
    return tk[0] == '\r' || tk[0] == '\n';
}

TextParser::TextParser(std::string_view buffer, Mdl* mdl)
    : tokens_(buffer, "#", false)
    , mdl_{mdl}
{
}

constexpr bool validate_tokens(std::initializer_list<std::string_view> tokens)
{
    for (auto tk : tokens) {
        if (tk.empty() || is_newline(tk)) {
            return false;
        }
    }
    return true;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, bool& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<bool>(tk)) {
        out = *res;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse bool, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, std::string& out)
{
    auto tk = tokens.next();
    if (is_newline(tk)) { // Some things are there but don't have value
        out = "";
        tokens.put_back(tk);
        return true;
    } else if (!tk.empty()) {
        out = std::string(tk);
        string::tolower(&out);
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse string, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, int32_t& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<int32_t>(tk)) {
        out = *res;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse int32_t, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, uint32_t& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<uint32_t>(tk)) {
        out = *res;
        return true;
    }
    LOG_F(ERROR, "{}: Failed to parse uint32_t, line: {}", name, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, float& out)
{
    auto tk = tokens.next();
    if (auto res = string::from<float>(tk)) {
        out = *res;
        return true;
    }

    LOG_F(ERROR, "{}: Failed to parse float - got '{}', line: {}", name, tk, tokens.line());
    return false;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, glm::vec2& out)
{
    if (!parse_tokens(tokens, name, out.x) || !parse_tokens(tokens, name, out.y)) {
        LOG_F(ERROR, "{}: Failed to parse Vector2, line: {}", name, tokens.line());
        return false;
    }

    return true;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, glm::vec3& out)
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

bool parse_tokens(Tokenizer& tokens, std::string_view name, glm::vec4& out)
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

bool parse_tokens(Tokenizer& tokens, std::string_view name, glm::quat& out)
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

bool parse_tokens(Tokenizer& tokens, std::string_view name, Face& out)
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

bool parse_tokens(Tokenizer& tokens, std::string_view name, AABBNode* node)
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
bool parse_tokens(Tokenizer& tokens, std::string_view name, std::vector<T>& out)
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
void cleanup_geometry(Model* model, T* n, const GeomCxt& geomctx)
{
    if (geomctx.verts.size()) {
        // Create vertex buffer
        n->vertices.reserve(geomctx.verts.size());
        for (auto v : geomctx.verts) {
            VertType nv;
            nv.position = v;
            n->vertices.push_back(nv);
        }

        n->indices.reserve(geomctx.faces.size() * 3);

        absl::flat_hash_set<size_t> visited_indices;
        auto contains_vert = [](const Face& face, uint32_t idx) {
            for (auto i : face.vert_idx) {
                if (i == idx) { return true; }
            }
            return false;
        };

        // Tangents..
        glm::vec3* tan1 = nullptr;
        glm::vec3* tan2 = nullptr;

        if (geomctx.tangents.empty()) {
            tan1 = new glm::vec3[n->vertices.size() * 2];
            tan2 = tan1 + n->vertices.size();
            std::memset(tan1, 0, n->vertices.size() * sizeof(glm::vec3) * 2);
        }

        for (size_t iface = 0; iface < geomctx.faces.size(); ++iface) {
            // Gollect texture coordinates
            auto i1 = geomctx.faces[iface].vert_idx[0];
            auto i2 = geomctx.faces[iface].vert_idx[1];
            auto i3 = geomctx.faces[iface].vert_idx[2];

            auto t1 = geomctx.faces[iface].tvert_idx[0];
            auto t2 = geomctx.faces[iface].tvert_idx[1];
            auto t3 = geomctx.faces[iface].tvert_idx[2];

            auto& v1 = n->vertices.at(i1);
            auto& v2 = n->vertices.at(i2);
            auto& v3 = n->vertices.at(i3);

            // Get tex coords
            if (geomctx.tverts[0].size()) { // dummys don't have textures..
                if (t1 < geomctx.tverts[0].size()) {
                    v1.tex_coords = glm::vec2{geomctx.tverts[0].at(t1).x, geomctx.tverts[0].at(t1).y};
                }

                if (t2 < geomctx.tverts[0].size()) {
                    v2.tex_coords = glm::vec2{geomctx.tverts[0].at(t2).x, geomctx.tverts[0].at(t2).y};
                }

                if (t3 < geomctx.tverts[0].size()) {
                    v3.tex_coords = glm::vec2{geomctx.tverts[0].at(t3).x, geomctx.tverts[0].at(t3).y};
                }
            }

            // Tangents - Not sure if this is right.. just copy pasta from
            // https://gamedev.stackexchange.com/questions/68612/how-to-compute-tangent-and-bitangent-vectors
            if (geomctx.tangents.empty()) {
                float x1 = v2.position.x - v1.position.x;
                float x2 = v3.position.x - v1.position.x;
                float y1 = v2.position.y - v1.position.y;
                float y2 = v3.position.y - v1.position.y;
                float z1 = v2.position.z - v1.position.z;
                float z2 = v3.position.z - v1.position.z;

                float s1 = v2.tex_coords.x - v1.tex_coords.x;
                float s2 = v3.tex_coords.x - v1.tex_coords.x;
                float tn1 = v2.tex_coords.y - v1.tex_coords.y;
                float tn2 = v3.tex_coords.y - v1.tex_coords.y;

                float r = 1.0f / (s1 * tn2 - s2 * tn1);
                glm::vec3 sdir((tn2 * x1 - tn1 * x2) * r, (tn2 * y1 - tn1 * y2) * r,
                    (tn2 * z1 - tn1 * z2) * r);
                glm::vec3 tdir((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
                    (s1 * z2 - s2 * z1) * r);

                tan1[i1] += sdir;
                tan1[i2] += sdir;
                tan1[i3] += sdir;

                tan2[i1] += tdir;
                tan2[i2] += tdir;
                tan2[i3] += tdir;
            }

            for (auto ivert : geomctx.faces[iface].vert_idx) {
                // Create indices
                n->indices.push_back(uint16_t(ivert));

                // Everything else should be done if it's been visited once.
                if (visited_indices.contains(ivert)) { continue; }

                // Generate vertex normals - Code here is basically what is in nwnexplorer..
                // not sure if it's right or not.. and there are different ways of calculating vertex
                // normals.
                if (ivert >= geomctx.normals.size()) {
                    glm::vec3 sum{0.0f, 0.0f, 0.0f};
                    for (size_t jface = iface; jface < geomctx.faces.size(); ++jface) {
                        if (contains_vert(geomctx.faces[jface], ivert)) {
                            auto p1 = n->vertices.at(geomctx.faces[jface].vert_idx[0]).position;
                            auto p2 = n->vertices.at(geomctx.faces[jface].vert_idx[1]).position;
                            auto p3 = n->vertices.at(geomctx.faces[jface].vert_idx[2]).position;
                            sum += glm::triangleNormal(p1, p2, p3);
                        }
                    }
                    n->vertices[ivert].normal = glm::normalize(sum);
                } else {
                    n->vertices[ivert].normal = geomctx.normals.at(ivert);
                }

                if (ivert < geomctx.tangents.size()) {
                    n->vertices[ivert].tangent = geomctx.tangents.at(ivert);
                }

                if constexpr (std::is_same_v<T, SkinNode>) {
                    n->vertices[ivert].weights = geomctx.weights.at(ivert);

                    const auto& bones = geomctx.bones.at(ivert);
                    n->vertices[ivert].bones = glm::ivec4{0};
                    for (size_t i = 0; i < 4; ++i) {
                        if (bones[i].empty()) { break; }
                        for (size_t j = 0; j < model->nodes.size(); ++j) {
                            if (string::icmp(model->nodes[j]->name, bones[i])) {
                                for (size_t k = 0; k < 64; ++k) {
                                    if (n->bone_nodes[k] == int16_t(j)) {
                                        n->vertices[ivert].bones[i] = k;
                                        break;
                                    } else if (n->bone_nodes[k] == -1) {
                                        n->vertices[ivert].bones[i] = k;
                                        n->bone_nodes[k] = int16_t(j);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }

                visited_indices.insert(ivert);
            }
        }

        // More tangent crap..
        if (geomctx.tangents.empty()) {
            for (size_t i = 0; i < n->vertices.size(); ++i) {
                auto& vert = n->vertices[i];
                const auto& t = tan1[i];

                // Gram-Schmidt orthogonalize
                auto temp = glm::normalize(t - vert.normal * glm::dot(vert.normal, t));
                vert.tangent.x = temp.x;
                vert.tangent.y = temp.y;
                vert.tangent.z = temp.z;

                // Calculate handedness
                vert.tangent.w = (glm::dot(glm::cross(vert.normal, t), tan2[i]) < 0.0f)
                    ? -1.0f
                    : 1.0f;
            }
            delete[] tan1;
        }
    }
}

bool TextParser::parse_controller(Node* node, std::string_view name, uint32_t type)
{
    size_t start_line = tokens_.line();
    std::string_view tk = tokens_.next();
    while (is_newline(tk))
        tk = tokens_.next();

    std::vector<float> time;
    std::vector<float> data;
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

bool TextParser::parse_node(Geometry* geometry)
{
    static thread_local GeomCxt geomctx;
    geomctx.clear();

    bool result = true;
    std::string_view tktype = tokens_.next();
    if (tktype.empty()) {
        LOG_F(ERROR, "Missing node type, line: {}", tokens_.line());
        return false;
    }

    std::string_view tkname = tokens_.next();
    if (tkname.empty()) {
        LOG_F(ERROR, "Missing node name, line: {}", tokens_.line());
        return false;
    }

    uint32_t type = NodeType::from_string(tktype);

    auto node = mdl_->make_node(type, tkname);
    if (!node) return false;

    std::string_view tk;
    for (tk = tokens_.next(); !tk.empty() && result; tk = tokens_.next()) {
        if (is_newline(tk)) {
            continue;
        } else if (icmp(tk, "endnode")) {
            break;
        }

        std::string_view controller_tk = tk;
        if (string::endswith(tk, "bezierkey")) { // No models seem to use this?
            controller_tk = std::string_view(tk.data(), tk.size() - 9);
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
            controller_tk = std::string_view(tk.data(), tk.size() - 3);
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
            PARSE_DATA(bitmap, n)
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

                    std::string current{tk};
                    for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
                        if (is_newline(tk)) { break; }
                        current += " " + std::string(tk);
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
            SkinNode* n = static_cast<SkinNode*>(node.get());
            if (icmp(tk, "weights")) {
                uint32_t size;
                if (!parse_tokens(tokens_, "weights: size", size)) { return false; }
                tokens_.next(); // drop new line.
                for (uint32_t i = 0; i < size; ++i) {
                    std::array<std::string, 4> bones;
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
                auto tk = tokens_.next();
                if (!icmp(tk, "endlist")) { tokens_.put_back(tk); }
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
        cleanup_geometry<SkinNode, SkinVertex>(&mdl_->model, n, geomctx);
    } else if (node->type & NodeFlags::mesh) {
        TrimeshNode* n = static_cast<TrimeshNode*>(node.get());
        cleanup_geometry<TrimeshNode, Vertex>(&mdl_->model, n, geomctx);
    }

    geometry->nodes.push_back(std::move(node));

    return tk == "endnode";
}

bool TextParser::parse_geometry()
{
    std::string_view tk;
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
    std::string_view tk = tokens_.next();
    auto anim = std::make_unique<Animation>(std::string(tk));
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
                LOG_F(INFO, "event parsing failed");
                return false;
            }
        } else if (icmp(tk, "length")) {
            if (!parse_tokens(tokens_, "length", anim->length))
                return false;
        } else if (icmp(tk, "node")) {
            if (!parse_node(anim.get())) {
                LOG_F(INFO, "node parsing failed");
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
    std::string_view tk = tokens_.next();
    mdl_->model.name = std::string(tk);
    LOG_F(INFO, "parsing: {}", mdl_->model.name);

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
            std::string name;
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
    for (std::string_view tk = tokens_.next(); !tk.empty() && result; tk = tokens_.next()) {
        if (is_newline(tk)) continue;
        // both spellings of this appear to be present in vanilla models and community tools
        // consume whatever is there till end of line.
        if (tk == "filedependancy" || tk == "filedependency") {
            auto t = tokens_.next();
            if (is_newline(t)) { continue; } // one model it's empty..
            mdl_->model.file_dependency = std::string(t);
            for (tk = tokens_.next(); !tk.empty(); tk = tokens_.next()) {
                if (is_newline(tk)) { break; }
                mdl_->model.file_dependency += " " + std::string(tk);
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
