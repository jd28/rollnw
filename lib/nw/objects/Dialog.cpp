#include "Dialog.hpp"

#include <iostream>

namespace nw {

DialogNode* DialogPtr::node()
{
    switch (type) {
    default:
        LOG_F(ERROR, "Unknown dialog node types: {}", type);
        return nullptr;
    case DialogNodeType::entry:
        if (index < parent->entries.size()) {
            return &parent->entries[index];
        }
    case DialogNodeType::reply:
        if (index < parent->replies.size()) {
            return &parent->replies[index];
        }
    }

    return nullptr;
}

const DialogNode* DialogPtr::node() const
{
    return const_cast<DialogPtr*>(this)->node();
}

Dialog::Dialog(const GffStruct gff)
    : prevent_zoom(0)
{
    is_valid_ = load(gff);
}

bool Dialog::read_nodes(const GffStruct gff, DialogNodeType node_type)
{
    std::string_view node_list;
    std::string_view ptr_list;
    std::vector<DialogNode>* holder;

    if (node_type == DialogNodeType::entry) {
        holder = &entries;
        node_list = "EntryList";
        ptr_list = "RepliesList";
    } else if (node_type == DialogNodeType::reply) {
        holder = &replies;
        node_list = "ReplyList";
        ptr_list = "EntriesList";
    } else {
        LOG_F(ERROR, "Invalid Node Type.");
        return false;
    }

    bool valid = true;
    size_t size = gff[node_list].size();
    holder->reserve(size);
    for (size_t i = 0; i < size && valid; ++i) {
        DialogNode node;
        GffStruct s = gff[node_list][i];

        node.parent = this;
        node.type = DialogNodeType::entry;

        s.get_to("QuestEntry", node.quest_entry, false);
        s.get_to("Comment", node.comment, false);
        s.get_to("Quest", node.quest);
        s.get_to("Script", node.script_action);
        s.get_to("Sound", node.sound);

        valid = valid && s.get_to("Text", node.text);

        size_t num_ptrs = s[ptr_list].size();
        node.pointers.reserve(num_ptrs);
        for (size_t j = 0; j < num_ptrs && valid; ++j) {
            GffStruct p = s[ptr_list][j];
            if (!p.valid()) {
                LOG_F(ERROR, "Unable to get list element: {}", j);
                valid = false;
                break;
            }

            DialogPtr ptr;
            ptr.parent = this;
            ptr.is_start = false;
            ptr.type = (node_type == DialogNodeType::entry) ? DialogNodeType::reply : DialogNodeType::entry;

            p.get_to("Active", ptr.script_appears);
            p.get_to("Index", ptr.index);
            p.get_to("IsChild", ptr.is_link);
            p.get_to("LinkComment", ptr.comment, false);

            node.pointers.push_back(ptr);
        }

        holder->push_back(node);
    }

    return valid;
}

bool Dialog::load(const GffStruct gff)
{
    bool valid = gff.get_to("PreventZoomIn", prevent_zoom)
        && gff.get_to("DelayEntry", delay_entry)
        && gff.get_to("DelayReply", delay_reply)
        && gff.get_to("EndConverAbort", script_abort)
        && gff.get_to("EndConversation", script_end)
        && gff.get_to("NumWords", word_count);

    if (!valid) { return false; }

    if (!read_nodes(gff, DialogNodeType::entry)
        || !read_nodes(gff, DialogNodeType::reply)) {
        return false;
    }

    size_t size = gff["StartingList"].size();
    starts.reserve(size);

    for (size_t i = 0; i < size && valid; ++i) {
        DialogPtr ptr;

        ptr.parent = this;
        ptr.is_start = true;
        ptr.is_link = false;
        ptr.type = DialogNodeType::entry;

        GffStruct s = gff["StartingList"][i];
        valid = valid && s.get_to("Active", ptr.script_appears);
        valid = valid && s.get_to("Index", ptr.index);
        starts.push_back(ptr);
    }

    return valid;
}

} // namespace nw
