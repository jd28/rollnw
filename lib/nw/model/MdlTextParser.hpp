#pragma once

#include "../log.hpp"
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

    bool parse_anim();
    bool parse_controller(Node* node, StringView name, uint32_t type);
    bool parse_geometry();
    bool parse_model();
    bool parse_node(Geometry* geometry);

public:
    TextParser(StringView buffer, Mdl* mdl);
    bool parse();
};

} // namespace nw::model
