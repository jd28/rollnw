#pragma once

#include "../log.hpp"
#include "../resources/assets.hpp"
#include "../util/Tokenizer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nw::model {

class Mdl;
struct Geometry;
struct Node;

class TextParser {
    struct PendingSkinBones {
        size_t node_index = 0;
        std::array<String, 64> names;
    };

    Tokenizer tokens_;
    Mdl* mdl_;
    ResourceType::type resource_type_;
    String walkmesh_root_;
    Vector<PendingSkinBones> pending_skin_bones_;

    bool parse_anim();
    bool parse_controller(Node* node, StringView name, uint32_t type);
    bool parse_geometry();
    bool parse_model();
    bool parse_node(Geometry* geometry);
    bool parse_walkmesh_geometry();
    bool resolve_model_skin_bones();
    bool accept_walkmesh_root(StringView name);

public:
    TextParser(StringView buffer, Mdl* mdl, ResourceType::type resource_type);
    bool parse();
};

} // namespace nw::model
