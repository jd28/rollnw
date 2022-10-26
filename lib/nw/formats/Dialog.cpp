#include "Dialog.hpp"

#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

namespace nw {

// -- DialogPtr ---------------------------------------------------------------

DialogNode* DialogPtr::node()
{
    switch (type) {
    default:
        LOG_F(ERROR, "Unknown dialog node types: {}", to_underlying(type));
        return nullptr;
    case DialogNodeType::entry:
        if (index < parent->entries.size()) {
            return &parent->entries[index];
        }
        break;
    case DialogNodeType::reply:
        if (index < parent->replies.size()) {
            return &parent->replies[index];
        }
        break;
    }

    return nullptr;
}

const DialogNode* DialogPtr::node() const
{
    return const_cast<DialogPtr*>(this)->node();
}

void from_json(const nlohmann::json& archive, DialogPtr& ptr)
{
    archive["type"].get_to(ptr.type);
    archive["index"].get_to(ptr.index);
    archive["script_appears"].get_to(ptr.script_appears);
    archive["is_start"].get_to(ptr.is_start);
    archive["is_link"].get_to(ptr.is_link);
    archive["comment"].get_to(ptr.comment);
}

void to_json(nlohmann::json& archive, const DialogPtr& ptr)
{
    archive["type"] = ptr.type;
    archive["index"] = ptr.index;
    archive["script_appears"] = ptr.script_appears;
    archive["is_start"] = ptr.is_start;
    archive["is_link"] = ptr.is_link;
    archive["comment"] = ptr.comment;
}

void from_json(const nlohmann::json& archive, DialogNode& node)
{
    archive["type"].get_to(node.type);
    archive["comment"].get_to(node.comment);
    archive["quest"].get_to(node.quest);
    archive["quest_entry"].get_to(node.quest_entry);
    archive["script_action"].get_to(node.script_action);
    archive["sound"].get_to(node.sound);
    archive["text"].get_to(node.text);
}

void to_json(nlohmann::json& archive, const DialogNode& node)
{
    archive["type"] = node.type;
    archive["comment"] = node.comment;
    archive["quest"] = node.quest;
    archive["quest_entry"] = node.quest_entry;
    archive["script_action"] = node.script_action;
    archive["sound"] = node.sound;
    archive["text"] = node.text;
}

Dialog::Dialog()
    : is_valid_{true}
{
}

Dialog::Dialog(const GffStruct archive)
    : prevent_zoom(0)
{
    is_valid_ = load(archive);
}

Dialog::Dialog(const nlohmann::json& archive)
{
    try {
        from_json(archive, *this);
    } catch (nlohmann::json::exception& e) {
        is_valid_ = false;
    }
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

    if (!valid) {
        return false;
    }

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

void from_json(const nlohmann::json& archive, Dialog& node)
{
    if (archive["$type"].get<std::string>() != "DLG") {
        LOG_F(ERROR, "invalid dlg json");
        return;
    }

    archive["entries"].get_to(node.entries);
    archive["replies"].get_to(node.replies);
    archive["script_abort"].get_to(node.script_abort);
    archive["script_end"].get_to(node.script_end);
    archive["starts"].get_to(node.starts);
    archive["delay_entry"].get_to(node.delay_entry);
    archive["delay_reply"].get_to(node.delay_reply);
    archive["word_count"].get_to(node.word_count);
    archive["prevent_zoom"].get_to(node.prevent_zoom);

    for (auto& entry : node.entries) {
        entry.parent = &node;
    }

    for (auto& reply : node.replies) {
        reply.parent = &node;
    }

    for (auto& start : node.starts) {
        start.parent = &node;
    }
}

void to_json(nlohmann::json& archive, const Dialog& node)
{
    archive["$type"] = "DLG";
    archive["$type"] = Dialog::json_archive_version;

    archive["entries"] = node.entries;
    archive["replies"] = node.replies;
    archive["script_abort"] = node.script_abort;
    archive["script_end"] = node.script_end;
    archive["starts"] = node.starts;
    archive["delay_entry"] = node.delay_entry;
    archive["delay_reply"] = node.delay_reply;
    archive["word_count"] = node.word_count;
    archive["prevent_zoom"] = node.prevent_zoom;
}

} // namespace nw
