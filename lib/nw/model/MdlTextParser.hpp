#pragma once

#include "../util/Tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::model {

class Mdl;
struct Geometry;
struct Node;

class TextParser {
    Tokenizer tokens_;
    Mdl* mdl_;

    bool parse_anim();
    bool parse_controller(Node* node, std::string_view name, uint32_t type);
    bool parse_geometry();
    bool parse_model();
    bool parse_node(Geometry* geometry);

public:
    TextParser(std::string_view buffer, Mdl* mdl);
    bool parse();
};

} // namespace nw::model
