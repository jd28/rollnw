#pragma once

#include "../log.hpp"
#include "../resources/assets.hpp"
#include "../util/Tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nw::model {

class Mdl;
struct Geometry;
struct Node;

class TextParser {
    Tokenizer tokens_;
    Mdl* mdl_;
    ResourceType::type resource_type_;
    String walkmesh_root_;

    bool parse_anim();
    bool parse_controller(Node* node, StringView name, uint32_t type);
    bool parse_geometry();
    bool parse_model();
    bool parse_node(Geometry* geometry);
    bool parse_walkmesh_geometry();
    bool accept_walkmesh_root(StringView name);

public:
    TextParser(StringView buffer, Mdl* mdl, ResourceType::type resource_type);
    bool parse();
};

} // namespace nw::model
