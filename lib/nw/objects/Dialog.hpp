#pragma once

#include "../formats/Gff.hpp"

#include <string>
#include <vector>

namespace nw {

enum struct DialogNodeType {
    entry,
    reply
};

struct Dialog;

struct DialogNode;

struct DialogPtr {
    DialogNode* node();
    const DialogNode* node() const;

    Dialog* parent;
    DialogNodeType type;
    uint32_t index;

    Resref script_appears;
    bool is_start;
    uint8_t is_link;
    std::string comment;
};

struct DialogNode {
public:
    DialogNode() { }

    Dialog* parent;
    DialogNodeType type;

    std::string comment;
    std::string quest;
    uint32_t quest_entry;
    Resref script_action;
    Resref sound;
    LocString text;

    std::vector<DialogPtr> pointers;
};

struct Dialog {
public:
    Dialog(const GffStruct gff);

    uint8_t prevent_zoom;
    uint32_t delay_entry;
    uint32_t delay_reply;
    Resref script_abort;
    Resref script_end;
    uint32_t word_count;

    std::vector<DialogNode> entries;
    std::vector<DialogNode> replies;
    std::vector<DialogPtr> starts;

    void dump(std::ostream& out, const DialogPtr& ptr, int depth, int indent = 4) const;
    bool valid() const noexcept { return is_valid_; }

private:
    bool is_valid_ = false;

    bool load(const GffStruct gff);
    bool read_nodes(const GffStruct gff, DialogNodeType node_type);
};

std::ostream& operator<<(std::ostream& out, const Dialog& dlg);

} // namespace nw
