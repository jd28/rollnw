#pragma once

#include "../serialization/Archives.hpp"

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

    Dialog* parent = nullptr;
    DialogNodeType type = DialogNodeType::entry;
    uint32_t index = std::numeric_limits<uint32_t>::max();

    Resref script_appears;
    bool is_start = false;
    uint8_t is_link = 0;
    std::string comment;
};

struct DialogNode {
public:
    DialogNode() { }

    Dialog* parent = nullptr;
    DialogNodeType type;

    std::string comment;
    std::string quest;
    uint32_t quest_entry = std::numeric_limits<uint32_t>::max();
    Resref script_action;
    Resref sound;
    LocString text;

    std::vector<DialogPtr> pointers;
};

struct Dialog {
public:
    explicit Dialog(const GffInputArchiveStruct gff);

    static constexpr int json_archive_version = 1;

    uint8_t prevent_zoom = 0;
    uint32_t delay_entry = 0;
    uint32_t delay_reply = 0;
    Resref script_abort;
    Resref script_end;
    uint32_t word_count = 0;

    std::vector<DialogNode> entries;
    std::vector<DialogNode> replies;
    std::vector<DialogPtr> starts;

    bool valid() const noexcept { return is_valid_; }

private:
    bool is_valid_ = false;

    bool load(const GffInputArchiveStruct gff);
    bool read_nodes(const GffInputArchiveStruct gff, DialogNodeType node_type);
};

} // namespace nw
