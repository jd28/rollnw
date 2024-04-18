#include "Dialog.hpp"

#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

// -- DialogPtr ---------------------------------------------------------------

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

    uint32_t index = uint32_t(ptr.parent->node_index(ptr.node, ptr.type));
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

DialogNode* Dialog::create_node(DialogNodeType type)
{
    auto result = node_pool_.allocate();
    result->type = type;
    return result;
}

DialogPtr* Dialog::create_ptr()
{
    return ptr_pool_.allocate();
}

size_t Dialog::node_index(DialogNode* node, DialogNodeType type) const
{
    if (type == DialogNodeType::entry) {
        auto it = std::find(entries.begin(), entries.end(), node);
        return it - entries.begin();
    } else {
        auto it = std::find(replies.begin(), replies.end(), node);
        return it - replies.begin();
    }
}

bool Dialog::read_nodes(const GffStruct gff, DialogNodeType node_type)
{
    std::string_view node_list;
    std::string_view ptr_list;
    std::vector<DialogNode*>* holder;

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
        GffStruct s = gff[node_list][i];

        DialogNode* node = create_node(node_type);
        node->parent = this;
        node->type = DialogNodeType::entry;

        s.get_to("QuestEntry", node->quest_entry, false);
        s.get_to("Comment", node->comment, false);
        s.get_to("Quest", node->quest);
        s.get_to("Script", node->script_action);
        s.get_to("Sound", node->sound);

        valid = valid && s.get_to("Text", node->text);

        size_t num_ptrs = s[ptr_list].size();
        node->pointers.reserve(num_ptrs);
        for (size_t j = 0; j < num_ptrs && valid; ++j) {
            GffStruct p = s[ptr_list][j];
            if (!p.valid()) {
                LOG_F(ERROR, "Unable to get list element: {}", j);
                valid = false;
                break;
            }

            DialogPtr* ptr = create_ptr();
            ptr->parent = this;
            ptr->is_start = false;
            ptr->type = (node_type == DialogNodeType::entry) ? DialogNodeType::reply : DialogNodeType::entry;

            p.get_to("Active", ptr->script_appears);
            p.get_to("Index", ptr->index);
            p.get_to("IsChild", ptr->is_link);
            p.get_to("LinkComment", ptr->comment, false);

            node->pointers.push_back(ptr);
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
        DialogPtr* ptr = create_ptr();

        ptr->parent = this;
        ptr->is_start = true;
        ptr->is_link = false;
        ptr->type = DialogNodeType::entry;

        GffStruct s = gff["StartingList"][i];
        valid = valid && s.get_to("Active", ptr->script_appears);
        valid = valid && s.get_to("Index", ptr->index);
        starts.push_back(ptr);
    }

    for (auto entry : entries) {
        for (auto it : entry->pointers) {
            it->node = replies[it->index];
        }
    }

    for (auto reply : replies) {
        for (auto it : reply->pointers) {
            it->node = entries[it->index];
        }
    }

    for (auto start : starts) {
        start->node = entries[start->index];
    }

    return valid;
}

void from_json(const nlohmann::json& archive, Dialog& node)
{
    if (archive["$type"].get<std::string>() != "DLG") {
        LOG_F(ERROR, "invalid dlg json");
        return;
    }

    auto& json_entries = archive["entries"];
    node.entries.reserve(json_entries.size());
    for (auto& je : json_entries) {
        DialogNode* n = node.create_node(DialogNodeType::entry);
        *n = je;
    }

    auto& json_replies = archive["replies"];
    node.replies.reserve(json_replies.size());
    for (auto& je : json_replies) {
        DialogNode* n = node.create_node(DialogNodeType::reply);
        *n = je;
    }

    auto& json_starts = archive["starts"];
    node.starts.reserve(json_starts.size());
    for (auto& je : json_starts) {
        DialogNode* n = node.create_node(DialogNodeType::entry);
        *n = je;
    }

    archive["script_abort"].get_to(node.script_abort);
    archive["script_end"].get_to(node.script_end);
    archive["delay_entry"].get_to(node.delay_entry);
    archive["delay_reply"].get_to(node.delay_reply);
    archive["word_count"].get_to(node.word_count);
    archive["prevent_zoom"].get_to(node.prevent_zoom);

    for (auto entry : node.entries) {
        entry->parent = &node;
        for (auto it : entry->pointers) {
            it->node = node.replies[it->index];
        }
    }

    for (auto reply : node.replies) {
        reply->parent = &node;
        for (auto it : reply->pointers) {
            it->node = node.entries[it->index];
        }
    }

    for (auto start : node.starts) {
        start->parent = &node;
        start->node = node.entries[start->index];
    }
}

void to_json(nlohmann::json& archive, const Dialog& node)
{
    archive["$type"] = "DLG";
    archive["$type"] = Dialog::json_archive_version;

    for (auto it : node.entries) {
        archive["entries"].push_back(*it);
    }

    for (auto it : node.replies) {
        archive["replies"].push_back(*it);
    }

    for (auto it : node.starts) {
        archive["starts"].push_back(*it);
    }

    archive["script_abort"] = node.script_abort;
    archive["script_end"] = node.script_end;
    archive["delay_entry"] = node.delay_entry;
    archive["delay_reply"] = node.delay_reply;
    archive["word_count"] = node.word_count;
    archive["prevent_zoom"] = node.prevent_zoom;
}

} // namespace nw
