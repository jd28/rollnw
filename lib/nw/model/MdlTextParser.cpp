#include "MdlTextParser.hpp"

#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/templates.hpp"
#include "Mdl.hpp"

#include <string_view>
#include <unordered_map>

using namespace std::literals;

namespace nw::model {

using string::icmp;

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

bool parse_tokens(Tokenizer& tokens, std::string_view name, SkinWeight& out)
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

bool TextParser::parse_controller(Node* node, std::string_view name, uint32_t type)
{
    size_t start_line = tokens_.line();
    std::string_view tk = tokens_.next();
    while (is_newline(tk))
        tk = tokens_.next();

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
        node->add_controller_data(name, type, data, 1, -1);
        return true;
    } else if (name == "detonatekey") {
        auto opt = string::from<float>(tk);
        if (!opt) {
            LOG_F(ERROR, "Failed to parse float: {}, {}", name, tokens_.line());
            return false;
        }
        data.push_back(*opt);

        node->add_controller_data(name, type, data, 1, 1);
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
        node->add_controller_data(name, type, data, 1, int(data.size()));
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
        data.push_back(*opt);
        tk = tokens_.next();
    }
    node->add_controller_data(name, type, data, rows, colsize);
    // Some controller lists have both row count and 'endlist'..
    if (max_rows == 0 && tk != "endlist") { tokens_.put_back(tk); }
    return tk == "endlist" || max_rows == 0;
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
            PARSE_DATA(gizmo, n)
            PARSE_DATA(danglymesh, n)
            PARSE_DATA(period, n)
            PARSE_DATA(tightness, n)
            PARSE_DATA(displacement, n)
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
            PARSE_DATA_TO(tverts, n, tverts[0])
            PARSE_DATA_TO(tverts1, n, tverts[1])
            PARSE_DATA_TO(tverts2, n, tverts[2])
            PARSE_DATA_TO(tverts3, n, tverts[3])
            PARSE_DATA(verts, n)
            PARSE_DATA(normals, n)
            PARSE_DATA(tangents, n)

            if (tk == "multimaterial") {
                tk = tokens_.next();
                auto rows = string::from<uint32_t>(tk);
                if (!rows) {
                    LOG_F(ERROR, "expected row count, got {}, line: {}", tk, tokens_.line());
                    return false;
                }

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
            }
            continue;
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
            PARSE_DATA(weights, n)
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
