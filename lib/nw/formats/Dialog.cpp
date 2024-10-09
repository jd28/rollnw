#include "Dialog.hpp"

#include "../kernel/Kernel.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

// -- DialogPtr ---------------------------------------------------------------

DialogPtr* DialogPtr::add()
{
    auto ptr = parent->create_ptr();
    ptr->type = (type == DialogNodeType::entry) ? DialogNodeType::reply : DialogNodeType::entry;
    ptr->node = parent->create_node(ptr->type);
    return add_ptr(ptr);
}

DialogPtr* DialogPtr::add_ptr(DialogPtr* ptr, bool is_link)
{
    if (is_link) {
        auto new_ptr = parent->create_ptr();
        *new_ptr = *ptr;
        new_ptr->is_link = true;
        node->pointers.push_back(new_ptr);
        return new_ptr;
    } else {
        node->pointers.push_back(ptr);
        Vector<DialogNode*> subnodes;
        ptr->get_all_subnodes(subnodes);
        for (auto n : subnodes) {
            parent->add_node_internal(n, n->type);
        }
        return ptr;
    }
}

DialogPtr* DialogPtr::add_string(String value, nw::LanguageID lang, bool feminine)
{
    auto ptr = parent->create_ptr();
    ptr->type = (type == DialogNodeType::entry) ? DialogNodeType::reply : DialogNodeType::entry;
    ptr->node = parent->create_node(ptr->type);
    ptr->node->text.add(lang, value, feminine);
    return add_ptr(ptr);
}

DialogPtr* DialogPtr::copy() const
{
    auto result = parent->create_ptr();
    *result = *this;
    if (!is_link) {
        result->node = node->copy();
    }
    return result;
}

void DialogPtr::remove_ptr(DialogPtr* ptr)
{
    auto it = std::remove(std::begin(node->pointers), std::end(node->pointers), ptr);
    node->pointers.erase(it, std::end(node->pointers));
    Vector<DialogNode*> subnodes;
    ptr->get_all_subnodes(subnodes);
    for (auto n : subnodes) {
        parent->remove_node_internal(n, n->type);
    }
}

DialogNode* DialogNode::copy() const
{
    auto result = parent->create_node(type);
    *result = *this;
    result->pointers.clear();

    for (auto p : pointers) {
        result->pointers.push_back(p->copy());
    }
    return result;
}

/// Gets a condition parameter if it exists
std::optional<String> DialogPtr::get_condition_param(const String& key)
{
    for (auto& it : condition_params) {
        if (it.first == key) {
            return it.second;
        }
    }
    return {};
}

void DialogPtr::get_all_subnodes(Vector<DialogNode*>& subnodes)
{
    if (!is_link) {
        subnodes.push_back(node);
        for (auto p : node->pointers) {
            p->get_all_subnodes(subnodes);
        }
    }
}

/// Removes condition parameter by key
void DialogPtr::remove_condition_param(const String& key)
{
    condition_params.erase(std::remove_if(std::begin(condition_params), std::end(condition_params),
                               [&key](const std::pair<String, String>& p) {
                                   return p.first == key;
                               }),
        std::end(condition_params));
}

/// Removes condition parameter by index
void DialogPtr::remove_condition_param(size_t index)
{
    condition_params.erase(std::begin(condition_params) + index);
}

/// Sets condition parameter, if key does not exist key and value are appended
void DialogPtr::set_condition_param(const String& key, const String& value)
{
    for (auto& it : condition_params) {
        if (it.first == key) {
            it.second = value;
            return;
        }
    }
    condition_params.emplace_back(key, value);
}

std::optional<String> DialogNode::get_action_param(const String& key)
{
    for (auto& it : action_params) {
        if (it.first == key) {
            return it.second;
        }
    }
    return {};
}

/// Removes action parameter by key
void DialogNode::remove_action_param(const String& key)
{
    action_params.erase(std::remove_if(std::begin(action_params), std::end(action_params),
                            [&key](const std::pair<String, String>& p) {
                                return p.first == key;
                            }),
        std::end(action_params));
}

/// Removes action parameter by index
void DialogNode::remove_action_param(size_t index)
{
    action_params.erase(std::begin(action_params) + index);
}

/// Sets action parameter, if key does not exist key and value are appended
void DialogNode::set_action_param(const String& key, const String& value)
{
    for (auto& it : action_params) {
        if (it.first == key) {
            it.second = value;
            return;
        }
    }
    action_params.emplace_back(key, value);
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
        deserialize(archive, *this);
        is_valid_ = true;
    } catch (nlohmann::json::parse_error& e) {
        LOG_F(ERROR, "[formats] failed deserializing dialog from json: {}", e.what());
        is_valid_ = false;
    }
}

DialogPtr* Dialog::add()
{
    auto ptr = create_ptr();
    ptr->type = DialogNodeType::entry;
    ptr->is_start = true;
    ptr->node = create_node(ptr->type);
    return add_ptr(ptr);
}

DialogPtr* Dialog::add_ptr(DialogPtr* ptr, bool is_link)
{
    if (is_link) {
        auto new_ptr = create_ptr();
        *new_ptr = *ptr;
        new_ptr->is_link = true;
        ptr->is_start = true;
        starts.push_back(new_ptr);
        return new_ptr;
    } else {
        ptr->is_start = true;
        starts.push_back(ptr);
        Vector<DialogNode*> subnodes;
        ptr->get_all_subnodes(subnodes);

        for (auto node : subnodes) {
            add_node_internal(node, node->type);
        }
        return ptr;
    }
}

DialogPtr* Dialog::add_string(String value, nw::LanguageID lang, bool feminine)
{
    auto ptr = create_ptr();
    ptr->type = DialogNodeType::entry;
    ptr->node = create_node(ptr->type);
    ptr->is_start = true;
    ptr->node->text.add(lang, value, feminine);
    return add_ptr(ptr);
}

DialogNode* Dialog::create_node(DialogNodeType type)
{
    auto result = node_pool_.allocate();
    result->type = type;
    result->parent = this;
    return result;
}

DialogPtr* Dialog::create_ptr()
{
    auto ptr = ptr_pool_.allocate();
    ptr->parent = this;
    return ptr;
}

void Dialog::delete_node(DialogNode* node)
{
    if (!node) { return; }
    for (auto ptr : node->pointers) {
        delete_ptr(ptr);
    }
    node->pointers.clear();
    node_pool_.free(node);
}

void Dialog::delete_ptr(DialogPtr* ptr)
{
    if (!ptr || ptr->is_link) { return; }
    delete_node(ptr->node);
    ptr_pool_.free(ptr);
}

size_t Dialog::node_index(DialogNode* node, DialogNodeType type) const
{
    if (type == DialogNodeType::entry) {
        auto it = std::find(std::begin(entries), std::end(entries), node);
        if (it == std::end(entries)) {
            throw std::runtime_error("[format] invalid dialog node");
        }
        return it - std::begin(entries);
    } else {
        auto it = std::find(std::begin(replies), std::end(replies), node);
        if (it == std::end(replies)) {
            throw std::runtime_error("[format] invalid dialog node");
        }
        return it - std::begin(replies);
    }
}

void Dialog::remove_ptr(DialogPtr* ptr)
{
    auto it = std::remove(std::begin(starts), std::end(starts), ptr);
    starts.erase(it, std::end(starts));
    if (ptr->is_link) { return; }

    Vector<DialogNode*> subnodes;
    ptr->get_all_subnodes(subnodes);
    for (auto node : subnodes) {
        remove_node_internal(node, node->type);
    }
}

void Dialog::add_node_internal(DialogNode* node, DialogNodeType type)
{
    if (type == DialogNodeType::entry) {
        auto it = std::find(std::begin(entries), std::end(entries), node);
        if (it != std::end(entries)) {
            throw std::runtime_error(fmt::format("[dialog] attempt to add entry that already exists: {}",
                static_cast<void*>(node)));
        } else {
            entries.push_back(node);
        }
    } else {
        auto it = std::find(std::begin(replies), std::end(replies), node);
        if (it != std::end(replies)) {
            throw std::runtime_error(fmt::format("[dialog] attempt to add entry that already exists: {}",
                static_cast<void*>(node)));
        } else {
            replies.push_back(node);
        }
    }
}

void Dialog::remove_node_internal(DialogNode* node, DialogNodeType type)
{
    if (type == DialogNodeType::entry) {
        auto it = std::find(std::begin(entries), std::end(entries), node);
        if (it == std::end(entries)) {
            throw std::runtime_error(fmt::format("[dialog] attempt to remove entry that doesn't exist: {}",
                static_cast<void*>(node)));
            return;
        } else {
            entries.erase(it);
        }
    } else {
        auto it = std::find(std::begin(replies), std::end(replies), node);
        if (it == std::end(replies)) {
            throw std::runtime_error(fmt::format("[dialog] attempt to remove reply that doesn't exist: {}",
                static_cast<void*>(node)));
            return;
        } else {
            replies.erase(it);
        }
    }

    for (auto p : starts) {
        if (p->is_link && p->node == node) {
            remove_ptr(p);
        }
    }

    for (auto e : entries) {
        auto it = std::remove_if(std::begin(e->pointers), std::end(e->pointers),
            [node](DialogPtr* ptr) {
                return ptr->is_link && ptr->node == node;
            });
        e->pointers.erase(it, std::end(e->pointers));
    }

    for (auto e : replies) {
        auto it = std::remove_if(std::begin(e->pointers), std::end(e->pointers),
            [node](DialogPtr* ptr) {
                return ptr->is_link && ptr->node == node;
            });
        e->pointers.erase(it, std::end(e->pointers));
    }
}

bool Dialog::read_nodes(const GffStruct gff, DialogNodeType node_type)
{
    StringView node_list;
    StringView ptr_list;
    Vector<DialogNode*>* holder;

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
        s.get_to("QuestEntry", node->quest_entry, false);
        s.get_to("Comment", node->comment, false);
        s.get_to("Quest", node->quest);
        s.get_to("Script", node->script_action);
        s.get_to("Sound", node->sound);
        s.get_to("Speaker", node->speaker, false);
        s.get_to("Animation", node->animation);
        s.get_to("AnimLoop", node->animation_loop);
        s.get_to("Delay", node->delay);
        valid = valid && s.get_to("Text", node->text);

        if (nw::kernel::config().version() == nw::GameVersion::vEE) {
            size_t num_action_params = s["ActionParams"].size();
            for (size_t j = 0; j < num_action_params; ++j) {
                GffStruct p = s["ActionParams"][j];
                String key, value;
                p.get_to("Key", key);
                p.get_to("Value", value);
                node->action_params.emplace_back(key, value);
            }
        }

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

            if (nw::kernel::config().version() == nw::GameVersion::vEE) {
                size_t num_params = p["ConditionParams"].size();
                for (size_t h = 0; h < num_params; ++h) {
                    GffStruct cp = p["ConditionParams"][h];
                    String key, value;
                    cp.get_to("Key", key);
                    cp.get_to("Value", value);
                    ptr->condition_params.emplace_back(key, value);
                }
            }
            node->pointers.push_back(ptr);
        }

        add_node_internal(node, node_type);
    }

    return valid;
}

bool Dialog::load(const GffStruct gff)
{
    bool valid = gff.get_to("DelayEntry", delay_entry)
        && gff.get_to("DelayReply", delay_reply)
        && gff.get_to("EndConverAbort", script_abort)
        && gff.get_to("EndConversation", script_end)
        && gff.get_to("NumWords", word_count);

    gff.get_to("PreventZoomIn", prevent_zoom, false);

    if (!valid) { return false; }

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

        if (nw::kernel::config().version() == nw::GameVersion::vEE) {
            size_t num_params = s["ConditionParams"].size();
            for (size_t h = 0; h < num_params; ++h) {
                GffStruct cp = s["ConditionParams"][h];
                String key, value;
                cp.get_to("Key", key);
                cp.get_to("Value", value);
                ptr->condition_params.emplace_back(key, value);
            }
        }

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

// NWN:EE Toolset will add empty action/condition parameter lists into the GFF, but
// obv can read DLGs without the list present, so I'm declining to put it in there
// so NWN 1.69 DLGs roundtrip as one would expect.
GffBuilder serialize(const Dialog* obj)
{
    GffBuilder gff{"DLG"};

    gff.top.add_field("PreventZoomIn", obj->prevent_zoom);
    gff.top.add_field("DelayEntry", obj->delay_entry);
    gff.top.add_field("DelayReply", obj->delay_reply);
    gff.top.add_field("EndConverAbort", obj->script_abort);
    gff.top.add_field("EndConversation", obj->script_end);
    gff.top.add_field("NumWords", obj->word_count);

    DialogNodeType node_type = DialogNodeType::reply;
    const Vector<DialogNode*>* holder = &obj->entries;
    StringView node_list = "EntryList";
    StringView ptr_list = "RepliesList";

    auto& entry_list = gff.top.add_list(node_list);
    for (size_t i = 0; i < holder->size(); ++i) {
        auto& s = entry_list.push_back(uint32_t(i));
        if (!(*holder)[i]->quest.empty()) {
            s.add_field("QuestEntry", (*holder)[i]->quest_entry);
        }
        s.add_field("Comment", (*holder)[i]->comment);
        s.add_field("Quest", (*holder)[i]->quest);
        s.add_field("Script", (*holder)[i]->script_action);
        s.add_field("Speaker", (*holder)[i]->speaker);
        s.add_field("Sound", (*holder)[i]->sound);
        s.add_field("Text", (*holder)[i]->text);
        s.add_field("Animation", (*holder)[i]->animation);
        s.add_field("AnimLoop", (*holder)[i]->animation_loop);
        s.add_field("Delay", (*holder)[i]->delay);

        if (nw::kernel::config().version() == nw::GameVersion::vEE
            && (*holder)[i]->action_params.size()) {
            auto& list = s.add_list("ActionParams");
            for (size_t j = 0; j < (*holder)[i]->action_params.size(); ++j) {
                list.push_back(uint32_t(j))
                    .add_field("Key", (*holder)[i]->action_params[j].first)
                    .add_field("Value", (*holder)[i]->action_params[j].second);
            }
        }

        auto& ptrs = s.add_list(ptr_list);
        for (size_t j = 0; j < (*holder)[i]->pointers.size(); ++j) {
            auto& ps = ptrs.push_back(uint32_t(j));
            uint32_t index = uint32_t(obj->node_index((*holder)[i]->pointers[j]->node, node_type));
            ps.add_field("Active", (*holder)[i]->pointers[j]->script_appears);
            ps.add_field("Index", index);
            ps.add_field("IsChild", (*holder)[i]->pointers[j]->is_link);
            if ((*holder)[i]->pointers[j]->is_link) {
                ps.add_field("LinkComment", (*holder)[i]->pointers[j]->comment);
            }

            if (nw::kernel::config().version() == nw::GameVersion::vEE
                && (*holder)[i]->pointers[j]->condition_params.size()) {
                auto& list = ps.add_list("ConditionParams");
                for (size_t h = 0; h < (*holder)[i]->pointers[j]->condition_params.size(); ++h) {
                    list.push_back(uint32_t(h))
                        .add_field("Key", (*holder)[i]->pointers[j]->condition_params[h].first)
                        .add_field("Value", (*holder)[i]->pointers[j]->condition_params[h].second);
                }
            }
        }
    }

    node_type = DialogNodeType::entry;
    holder = &obj->replies;
    node_list = "ReplyList";
    ptr_list = "EntriesList";

    auto& reply_list = gff.top.add_list(node_list);
    for (size_t i = 0; i < holder->size(); ++i) {
        auto& s = reply_list.push_back(uint32_t(i));
        if (!(*holder)[i]->quest.empty()) {
            s.add_field("QuestEntry", (*holder)[i]->quest_entry);
        }
        s.add_field("Comment", (*holder)[i]->comment);
        s.add_field("Quest", (*holder)[i]->quest);
        s.add_field("Script", (*holder)[i]->script_action);
        s.add_field("Sound", (*holder)[i]->sound);
        s.add_field("Text", (*holder)[i]->text);
        s.add_field("Animation", (*holder)[i]->animation);
        s.add_field("AnimLoop", (*holder)[i]->animation_loop);
        s.add_field("Delay", (*holder)[i]->delay);

        if (nw::kernel::config().version() == nw::GameVersion::vEE
            && (*holder)[i]->action_params.size()) {
            auto& list = s.add_list("ActionParams");
            for (size_t j = 0; j < (*holder)[i]->action_params.size(); ++j) {
                list.push_back(uint32_t(j))
                    .add_field("Key", (*holder)[i]->action_params[j].first)
                    .add_field("Value", (*holder)[i]->action_params[j].second);
            }
        }

        auto& ptrs = s.add_list(ptr_list);
        for (size_t j = 0; j < (*holder)[i]->pointers.size(); ++j) {
            auto& ps = ptrs.push_back(uint32_t(j));
            uint32_t index = uint32_t(obj->node_index((*holder)[i]->pointers[j]->node, node_type));
            ps.add_field("Active", (*holder)[i]->pointers[j]->script_appears);
            ps.add_field("Index", index);
            ps.add_field("IsChild", (*holder)[i]->pointers[j]->is_link);
            if ((*holder)[i]->pointers[j]->is_link) {
                ps.add_field("LinkComment", (*holder)[i]->pointers[j]->comment);
            }
            if (nw::kernel::config().version() == nw::GameVersion::vEE
                && (*holder)[i]->pointers[j]->condition_params.size()) {
                auto& list = ps.add_list("ConditionParams");
                for (size_t h = 0; h < (*holder)[i]->pointers[j]->condition_params.size(); ++h) {
                    list.push_back(uint32_t(h))
                        .add_field("Key", (*holder)[i]->pointers[j]->condition_params[h].first)
                        .add_field("Value", (*holder)[i]->pointers[j]->condition_params[h].second);
                }
            }
        }
    }

    auto& starts = gff.top.add_list("StartingList");
    for (size_t i = 0; i < obj->starts.size(); ++i) {
        uint32_t index = uint32_t(obj->node_index(obj->starts[i]->node, DialogNodeType::entry));
        auto& ps = starts.push_back(uint32_t(i))
                       .add_field("Active", obj->starts[i]->script_appears)
                       .add_field("Index", index);
        if (nw::kernel::config().version() == nw::GameVersion::vEE
            && obj->starts[i]->condition_params.size()) {
            auto& list = ps.add_list("ConditionParams");
            for (size_t h = 0; h < obj->starts[i]->condition_params.size(); ++h) {
                list.push_back(uint32_t(h))
                    .add_field("Key", obj->starts[i]->condition_params[h].first)
                    .add_field("Value", obj->starts[i]->condition_params[h].second);
            }
        }
    }

    gff.build();
    return gff;
}

// JSON De/Serialization

void deserialize(const nlohmann::json& archive, DialogPtr& ptr)
{
    archive["index"].get_to(ptr.index);
    archive["script_appears"].get_to(ptr.script_appears);
    archive["is_start"].get_to(ptr.is_start);
    archive["is_link"].get_to(ptr.is_link);
    archive["comment"].get_to(ptr.comment);
    archive["condition_params"].get_to(ptr.condition_params);
}

void serialize(nlohmann::json& archive, const DialogPtr& ptr)
{
    uint32_t index = uint32_t(ptr.parent->node_index(ptr.node, ptr.type));
    archive["index"] = index;
    archive["script_appears"] = ptr.script_appears;
    archive["is_start"] = ptr.is_start;
    archive["is_link"] = ptr.is_link;
    archive["comment"] = ptr.comment;
    archive["condition_params"] = ptr.condition_params;
}

void deserialize(const nlohmann::json& archive, DialogNode& node)
{
    archive["comment"].get_to(node.comment);
    archive["quest"].get_to(node.quest);
    archive["quest_entry"].get_to(node.quest_entry);
    archive["script_action"].get_to(node.script_action);
    archive["sound"].get_to(node.sound);
    archive["text"].get_to(node.text);
    if (node.type == DialogNodeType::entry) {
        archive["speaker"].get_to(node.speaker);
    }
    archive["animation"].get_to(node.animation);
    archive["animation_loop"].get_to(node.animation_loop);
    archive["delay"].get_to(node.delay);
    archive["action_params"].get_to(node.action_params);

    auto& list = archive["pointers"];
    for (size_t i = 0; i < list.size(); ++i) {
        auto ptr = node.parent->create_ptr();
        ptr->type = node.type == DialogNodeType::entry ? DialogNodeType::reply : DialogNodeType::entry;
        ptr->parent = node.parent;
        deserialize(list[i], *ptr);
        node.pointers.push_back(ptr);
    }
}

void serialize(nlohmann::json& archive, const DialogNode& node)
{
    archive["comment"] = node.comment;
    archive["quest"] = node.quest;
    archive["quest_entry"] = node.quest_entry;
    archive["script_action"] = node.script_action;
    archive["sound"] = node.sound;
    archive["text"] = node.text;
    if (node.type == DialogNodeType::entry) {
        archive["speaker"] = node.speaker;
    }
    archive["animation"] = node.animation;
    archive["animation_loop"] = node.animation_loop;
    archive["delay"] = node.delay;
    archive["action_params"] = node.action_params;

    auto& list = archive["pointers"] = nlohmann::json::array();
    for (size_t i = 0; i < node.pointers.size(); ++i) {
        nlohmann::json j;
        serialize(j, *node.pointers[i]);
        list.push_back(j);
    }
}

void deserialize(const nlohmann::json& archive, Dialog& node)
{
    if (archive["$type"].get<String>() != "DLG") {
        LOG_F(ERROR, "invalid dlg json");
        return;
    }

    auto& json_entries = archive["entries"];
    node.entries.reserve(json_entries.size());
    for (auto& je : json_entries) {
        DialogNode* n = node.create_node(DialogNodeType::entry);
        deserialize(je, *n);
        node.add_node_internal(n, DialogNodeType::entry);
    }

    auto& json_replies = archive["replies"];
    node.replies.reserve(json_replies.size());
    for (auto& je : json_replies) {
        DialogNode* n = node.create_node(DialogNodeType::reply);
        deserialize(je, *n);
        node.add_node_internal(n, DialogNodeType::reply);
    }

    auto& json_starts = archive["starts"];
    node.starts.reserve(json_starts.size());
    for (auto& je : json_starts) {
        auto ptr = node.create_ptr();
        ptr->type = DialogNodeType::entry;
        ptr->parent = &node;
        deserialize(je, *ptr);
        node.starts.push_back(ptr);
    }

    archive["script_abort"].get_to(node.script_abort);
    archive["script_end"].get_to(node.script_end);
    archive["delay_entry"].get_to(node.delay_entry);
    archive["delay_reply"].get_to(node.delay_reply);
    archive["word_count"].get_to(node.word_count);
    archive["prevent_zoom"].get_to(node.prevent_zoom);

    for (auto entry : node.entries) {
        for (auto it : entry->pointers) {
            it->node = node.replies[it->index];
        }
    }

    for (auto reply : node.replies) {
        for (auto it : reply->pointers) {
            it->node = node.entries[it->index];
        }
    }

    for (auto start : node.starts) {
        start->node = node.entries[start->index];
    }
}

void serialize(nlohmann::json& archive, const Dialog& node)
{
    archive["$type"] = "DLG";
    archive["$version"] = Dialog::json_archive_version;

    for (auto it : node.entries) {
        nlohmann::json j;
        serialize(j, *it);
        archive["entries"].push_back(j);
    }

    for (auto it : node.replies) {
        nlohmann::json j;
        serialize(j, *it);
        archive["replies"].push_back(j);
    }

    for (auto it : node.starts) {
        nlohmann::json j;
        serialize(j, *it);
        archive["starts"].push_back(j);
    }

    archive["script_abort"] = node.script_abort;
    archive["script_end"] = node.script_end;
    archive["delay_entry"] = node.delay_entry;
    archive["delay_reply"] = node.delay_reply;
    archive["word_count"] = node.word_count;
    archive["prevent_zoom"] = node.prevent_zoom;
}

} // namespace nw
