#include "MdlTextParser.hpp"

#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/templates.hpp"
#include "Mdl.hpp"

#include <string_view>
#include <unordered_map>

using namespace std::literals;

namespace nw {

using string::icmp;

inline bool is_newline(std::string_view tk)
{
    if (tk.empty()) return false;
    return tk[0] == '\r' || tk[0] == '\n';
}

MdlTextParser::MdlTextParser(std::string_view buffer, Mdl* mdl)
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
    if (validate_tokens({tk})) {
        out = std::string(tk);
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

bool parse_tokens(Tokenizer& tokens, std::string_view name, MdlFace& out)
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

bool parse_tokens(Tokenizer& tokens, std::string_view name, MdlSkinWeight& out)
{
    std::string bone;
    float value = 0.0f;
    for (size_t i = 0; i < 4; ++i) {
        if (parse_tokens(tokens, "weight: bone", bone)
            && parse_tokens(tokens, "weight: value", value)) {
            out.bones[i] = std::move(bone);
            out.weights[i] = value;
        } else {
            LOG_F(ERROR, "Failed to parse skin weight {}, line: {}", name, tokens.line());
            return false;
        }
        auto tk = tokens.next();
        if (is_newline(tk)) {
            tokens.put_back(tk);
            break;
        }
        tokens.put_back(tk);
    }

    return true;
}

bool parse_tokens(Tokenizer& tokens, std::string_view name, MdlAABBNode* node)
{
    // Will have to create the tree structure later.
    while (true) {
        MdlAABBEntry out;
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
    return true;
}

bool MdlTextParser::parse_controller(MdlNode* node, std::string_view name, uint32_t type)
{
    size_t start_line = tokens_.line();
    std::string_view tk = tokens_.next();
    while (is_newline(tk))
        tk = tokens_.next();

    std::vector<float> data;
    data.reserve(128);

    if (start_line == tokens_.line()) { // All the data is going to be on one line.
        while (!tk.empty() && !is_newline(tk)) {
            auto opt = string::from<float>(tk);
            if (!opt) {
                LOG_F(ERROR, "Failed to parse float: {}, {}", name, tokens_.line());
                return false;
            }
            data.push_back(*opt);
            tk = tokens_.next();
        }
        node->add_controller_data(name, type, data, 1, int(data.size()));
        return true;
    } else {
        int colsize = -1;
        int rows = 0;

        while (!tk.empty()) {
            if (is_newline(tk)) {
                if (colsize == -1) {
                    colsize = int(data.size());
                }
                if (data.size() % colsize) {
                    LOG_F(ERROR, "{}: Mismatched column size, line: {}", name, tokens_.line());
                    return false;
                }
                ++rows;
                while (is_newline(tk))
                    tk = tokens_.next();
            }
            if (tk == "endlist") break;

            auto opt = string::from<float>(tk);
            if (!opt) {
                LOG_F(ERROR, "Failed to parse float: {}", tk);
                return false;
            }
            data.push_back(*opt);
            tk = tokens_.next();
        }
        node->add_controller_data(name, type, data, rows, colsize);
        return tk == "endlist";
    }
}

#define PARSE_DATA(name, node)                      \
    if (icmp(tk, ROLLNW_STRINGIFY(name))) {         \
        if (!parse_tokens(tokens_, tk, node->name)) \
            return false;                           \
        else                                        \
            continue;                               \
    }

#define PARSE_DATA_TO(name, node, target)             \
    if (icmp(tk, ROLLNW_STRINGIFY(name))) {           \
        if (!parse_tokens(tokens_, tk, node->target)) \
            return false;                             \
        else                                          \
            continue;                                 \
    }

bool MdlTextParser::parse_node(MdlGeometry* geometry)
{
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

    uint32_t type = MdlNodeType::from_string(tktype);

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
        if (string::endswith(tk, "key")) {
            controller_tk = std::string_view(tk.data(), tk.size() - 3);
        } else if (string::endswith(tk, "bezierkey")) {
            controller_tk = std::string_view(tk.data(), tk.size() - 9);
        } else if (icmp(tk, "setfillumcolor")) {
            controller_tk = "selfillumcolor";
        }

        // Check if it's a controller.
        auto [ctype, ntype] = MdlControllerType::lookup(controller_tk);
        if (ctype != 0) {
            if (!(node->type & ntype)) {
                LOG_F(ERROR, "Controller set on an incompatible node: {}", tk);
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

        if (node->type & MdlNodeFlags::mesh) {
            MdlTrimeshNode* n = static_cast<MdlTrimeshNode*>(node.get());

            PARSE_DATA(ambient, n)
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

            PARSE_DATA(colors, n)
            PARSE_DATA(diffuse, n)
            PARSE_DATA(faces, n)
            PARSE_DATA(inheritcolor, n)
            PARSE_DATA(materialname, n)
            PARSE_DATA(render, n)
            PARSE_DATA(renderhint, n)
            PARSE_DATA(rotatetexture, n)
            PARSE_DATA(shadow, n)
            PARSE_DATA(shininess, n)
            PARSE_DATA(specular, n)
            PARSE_DATA_TO(texture0, n, textures[0])
            PARSE_DATA_TO(texture1, n, textures[1])
            PARSE_DATA_TO(texture2, n, textures[2])
            PARSE_DATA(tilefade, n)
            PARSE_DATA(transparencyhint, n)
            PARSE_DATA(showdispl, n)
            PARSE_DATA(displtype, n)
            PARSE_DATA(lightmapped, n)
            PARSE_DATA(multimaterial, n)
            PARSE_DATA_TO(tverts, n, tverts[0])
            PARSE_DATA_TO(tverts1, n, tverts[1])
            PARSE_DATA_TO(tverts2, n, tverts[2])
            PARSE_DATA_TO(tverts3, n, tverts[3])
            PARSE_DATA(verts, n)
            PARSE_DATA(normals, n)
            PARSE_DATA(tangents, n)
        }

        if (node->type & MdlNodeFlags::reference) {
            MdlReferenceNode* n = static_cast<MdlReferenceNode*>(node.get());
            PARSE_DATA(reattachable, n)
            PARSE_DATA(refmodel, n)
        }

        if (node->type & MdlNodeFlags::dangly) {
            MdlDanglymeshNode* n = static_cast<MdlDanglymeshNode*>(node.get());
            PARSE_DATA(constraints, n)
            PARSE_DATA(displacement, n)
            PARSE_DATA(period, n)
            PARSE_DATA(tightness, n)
        }

        if (node->type & MdlNodeFlags::emitter) {
            MdlEmitterNode* n = static_cast<MdlEmitterNode*>(node.get());
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

            bool value;
            if (icmp(tk, "affectedByWind")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::AffectedByWind;
                continue;
            } else if (icmp(tk, "bounce")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::Bounce;
                continue;
            } else if (icmp(tk, "inherit")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::Inherit;
                continue;
            } else if (icmp(tk, "inherit_local")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::InheritLocal;
                continue;
            } else if (icmp(tk, "inherit_part")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::InheritPart;
                continue;
            } else if (icmp(tk, "inheritvel")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::InheritVel;
                continue;
            } else if (icmp(tk, "m_isTinted")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::IsTinted;
                continue;
            } else if (icmp(tk, "p2p")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::P2P;
                continue;
            } else if (icmp(tk, "p2p_sel")) {
                uint32_t v;
                if (!parse_tokens(tokens_, tk, v)) return false;
                if (v) n->flags |= MdlEmitterFlag::P2PSel;
                continue;
            } else if (icmp(tk, "random")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::Random;
                continue;
            } else if (icmp(tk, "splat")) {
                if (!parse_tokens(tokens_, tk, value)) return false;
                if (value) n->flags |= MdlEmitterFlag::Splat;
                continue;
            }
        }

        if (node->type & MdlNodeFlags::light) {
            MdlLightNode* n = static_cast<MdlLightNode*>(node.get());
            PARSE_DATA(affectdynamic, n)
            PARSE_DATA(ambientonly, n)
            PARSE_DATA(fadinglight, n)
            PARSE_DATA(flarecolorshifts, n)
            PARSE_DATA(flarepositions, n)
            PARSE_DATA(flareradius, n)
            PARSE_DATA(flaresizes, n)
            PARSE_DATA(generateflare, n)
            if (icmp(tk, "isdynamic")) {
                LOG_F(WARNING, "'isdynamic' is obsolete, use 'nDynamicType'");
                if (!parse_tokens(tokens_, tk, n->dynamic)) return false;
                continue;
            }
            PARSE_DATA(lightpriority, n)
            PARSE_DATA_TO(nDynamicType, n, dynamic)
            PARSE_DATA(shadow, n)
            PARSE_DATA_TO(texturenames, n, textures);
        }

        if (node->type & MdlNodeFlags::aabb) {
            MdlAABBNode* n = static_cast<MdlAABBNode*>(node.get());
            if (icmp(tk, "aabb")) {
                if (!parse_tokens(tokens_, "aabb", n)) {
                    return false;
                } else {
                    continue;
                }
            }
        }

        if (node->type & MdlNodeFlags::skin) {
            MdlSkinNode* n = static_cast<MdlSkinNode*>(node.get());
            PARSE_DATA(weights, n)
        }

        if (node->type & MdlNodeFlags::anim) {
            MdlAnimeshNode* n = static_cast<MdlAnimeshNode*>(node.get());
            PARSE_DATA(animtverts, n);
            PARSE_DATA(animverts, n);
            PARSE_DATA(sampleperiod, n);
        }

        LOG_F(ERROR, "Unknown token: '{}', line: {}", tk, tokens_.line());
        return false;
    }

    geometry->nodes.push_back(std::move(node));

    return tk == "endnode";
}

bool MdlTextParser::parse_geometry()
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

bool MdlTextParser::parse_anim()
{
    std::string_view tk = tokens_.next();
    auto anim = std::make_unique<MdlAnimation>(std::string(tk));
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
            MdlAnimationEvent ev;
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

bool MdlTextParser::parse_model()
{
    std::string_view tk = tokens_.next();
    mdl_->model.name = std::string(tk);

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
                mdl_->model.classification = MdlModelClass::character;
            else if (icmp(name, "door"))
                mdl_->model.classification = MdlModelClass::door;
            else if (icmp(name, "effect") || icmp(name, "effects"))
                mdl_->model.classification = MdlModelClass::effect;
            else if (icmp(name, "tile"))
                mdl_->model.classification = MdlModelClass::tile;
            else if (icmp(name, "item"))
                mdl_->model.classification = MdlModelClass::item;
            else if (icmp(name, "gui"))
                mdl_->model.classification = MdlModelClass::gui;
            else if (icmp(name, "unknown"))
                mdl_->model.classification = MdlModelClass::invalid;
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
            if (!parse_anim()) return false;
        } else {
            LOG_F(ERROR, "unknown token '{}', line: {}", tk, tokens_.line());
            return false;
        }
    }

    return tk == "donemodel";
}

bool MdlTextParser::parse()
{
    bool result = true;
    for (std::string_view tk = tokens_.next(); !tk.empty() && result; tk = tokens_.next()) {
        if (is_newline(tk)) continue;
        // both spellings of this appear to be present in vanilla models and community tools
        if (tk == "filedependancy" || tk == "filedependency") {
            if (!parse_tokens(tokens_, tk, mdl_->model.file_dependency)) return false;
        } else if (tk == "newmodel") {
            if (!parse_model()) return false;
        } else {
            LOG_F(ERROR, "unknown token '{}', line: {}", tk, tokens_.line());
            return false;
        }
    }

    return result;
}

} // namespace nw
