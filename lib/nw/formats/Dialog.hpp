#pragma once

#include "../i18n/LocString.hpp"
#include "../resources/ResourceType.hpp"
#include "../resources/Resref.hpp"
#include "../serialization/Serialization.hpp"
#include "../util/memory.hpp"

#include <nlohmann/json_fwd.hpp>

#include <limits>
#include <string>

namespace nw {

enum struct DialogNodeType {
    entry,
    reply
};

enum struct DialogAnimation : uint32_t {
    default_ = 0,
    taunt = 28,
    greeting = 29,
    listen = 30,
    worship = 33,
    salute = 34,
    bow = 35,
    steal = 37,
    talk_normal = 38,
    talk_pleading = 39,
    talk_forceful = 40,
    talk_laugh = 41,
    victory_1 = 44,
    victory_2 = 45,
    victory_3 = 46,
    look_far = 48,
    drink = 70,
    read = 71,
    none = 88,
};

struct Dialog;
struct DialogNode;

struct DialogPtr {
    Dialog* parent = nullptr;
    DialogNodeType type = DialogNodeType::entry;
    uint32_t index = std::numeric_limits<uint32_t>::max();
    DialogNode* node = nullptr;

    Resref script_appears;
    Vector<std::pair<String, String>> condition_params;

    bool is_start = false;
    bool is_link = false;
    String comment;

    /// Adds Dialog Pointer, if `is_link` is false no new pointer or node is created.
    /// if `is_link` is true a new pointer will created with the node copied from input pointer.
    DialogPtr* add_ptr(DialogPtr* ptr, bool is_link = false);

    /// Adds Dialog Pointer and Node with string value set
    DialogPtr* add_string(String value, nw::LanguageID lang = nw::LanguageID::english, bool feminine = false);

    /// Adds empty Dialog Pointer and Node
    DialogPtr* add();

    /// Copies dialog pointer and all sub-nodes
    DialogPtr* copy() const;

    /// Gets all sub-nodes that are not links
    /// When a pointer is removed from the dialog tree all of its sub-nodes must be removed from
    /// the main node list, unless they are links.
    void get_all_subnodes(Vector<DialogNode*>& subnodes);

    /// Gets a condition parameter if it exists
    std::optional<String> get_condition_param(const String& key);

    /// Removes condition parameter by key
    void remove_condition_param(const String& key);

    /// Removes condition parameter by index
    void remove_condition_param(size_t index);

    /// Removes Dialog Ptr from underlying node
    void remove_ptr(DialogPtr* ptr);

    /// Sets condition parameter, if key does not exist key and value are appended
    void set_condition_param(const String& key, const String& value);
};

struct DialogNode {
public:
    DialogNode() { }

    Dialog* parent = nullptr;
    DialogNodeType type;

    String comment;
    String quest;
    String speaker;
    uint32_t quest_entry = std::numeric_limits<uint32_t>::max();
    Resref script_action;
    Resref sound;
    LocString text;
    DialogAnimation animation = DialogAnimation::default_;
    bool animation_loop = false; // This is obsolete.. but still is added to every conversation by the toolset..
    uint32_t delay = std::numeric_limits<uint32_t>::max();

    Vector<DialogPtr*> pointers;
    Vector<std::pair<String, String>> action_params;

    /// Copies a Node
    DialogNode* copy() const;

    /// Gets action parameter if it exists
    std::optional<String> get_action_param(const String& key);

    /// Removes action parameter by key
    void remove_action_param(const String& key);

    /// Removes action parameter by index
    void remove_action_param(size_t index);

    /// Sets action parameter, if key does not exist key and value are appended
    void set_action_param(const String& key, const String& value);
};

struct Dialog {
public:
    Dialog();
    explicit Dialog(const GffStruct archive);
    explicit Dialog(const nlohmann::json& archive);
    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;

    static constexpr int json_archive_version = 1;
    static constexpr ResourceType::type restype = ResourceType::dlg;

    /// Adds empty Dialog Pointer and Node
    DialogPtr* add();

    /// Adds a node to the iternal node lists
    /// @warning This should be considered for internal use and not client code
    void add_node_internal(DialogNode* node, DialogNodeType type);

    /// Adds Dialog Pointer, if `is_link` is false no new pointer or node is created.
    /// if `is_link` is true a new pointer will created with the node copied from input pointer.
    DialogPtr* add_ptr(DialogPtr* ptr, bool is_link = false);

    /// Adds Dialog Pointer and Node with string value set
    DialogPtr* add_string(String value, nw::LanguageID lang = nw::LanguageID::english, bool feminine = false);

    /// Creates a new Dialog Node.
    DialogNode* create_node(DialogNodeType type);

    /// Creates a new Dialog Pointer
    DialogPtr* create_ptr();

    /// Deletes a dialog node
    /// @warning This should be considered for internal use and not client code
    void delete_node(DialogNode* node);

    /// Deletes a dialog pointer
    /// @warning ``ptr`` should be removed from / not added to a dialog prior to deletion
    void delete_ptr(DialogPtr* ptr);

    /// Get Node index.
    size_t node_index(DialogNode* node, DialogNodeType type) const;

    /// Removes a node to the iternal node lists
    /// @warning This should be considered for internal use and not client code
    void remove_node_internal(DialogNode* node, DialogNodeType type);

    /// Removes Dialog Ptr from underlying node
    void remove_ptr(DialogPtr* ptr);

    /// Checks id dialog was successfully parsed
    bool valid() const noexcept { return is_valid_; }

    Vector<DialogNode*> entries;
    Vector<DialogNode*> replies;

    Resref script_abort;
    Resref script_end;
    Vector<DialogPtr*> starts;

    uint32_t delay_entry = 0;
    uint32_t delay_reply = 0;
    uint32_t word_count = 0;

    bool prevent_zoom = false;

private:
    bool is_valid_ = false;
    MemoryArena arena_{sizeof(DialogNode) * 64 + sizeof(DialogPtr) * 64};
    ObjectPool<DialogNode, 64> node_pool_{&arena_};
    ObjectPool<DialogPtr, 64> ptr_pool_{&arena_};

    bool load(const GffStruct gff);
    bool read_nodes(const GffStruct gff, DialogNodeType node_type);
};

GffBuilder serialize(const Dialog* obj);

void serialize(nlohmann::json& archive, const Dialog& node);
void deserialize(const nlohmann::json& archive, Dialog& node);

} // namespace nw
