#pragma once

#include "../util/Tokenizer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nw {

class Mdl;
struct MdlAABBEntry;
struct MdlAABBNode;
struct MdlFace;
struct MdlNode;

class MdlTextParser {
    Tokenizer tokens_;
    Mdl* mdl_;

    bool parse_anim();
    bool parse_controller(MdlNode* node, std::string_view name, uint32_t type);
    bool parse_geometry();
    bool parse_model();
    bool parse_node(bool is_anim = false);

public:
    MdlTextParser(std::string_view buffer, Mdl* mdl);
    bool parse();
};

} // namespace nw
