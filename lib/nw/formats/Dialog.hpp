#pragma once

#include "../serialization/Archives.hpp"
#include "../util/memory.hpp"

#include <nlohmann/json_fwd.hpp>

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
    Dialog* parent = nullptr;
    DialogNodeType type = DialogNodeType::entry;
    uint32_t index = std::numeric_limits<uint32_t>::max();
    DialogNode* node = nullptr;

    Resref script_appears;
    bool is_start = false;
    bool is_link = false;
    std::string comment;

    /// Adds Dialog Pointer, if `is_link` is false no new pointer or node is created.
    /// if `is_link` is true a new pointer will created with the node copied from input pointer.
    DialogPtr* add_ptr(DialogPtr* ptr, bool is_link = false);

    /// Adds Dialog Pointer and Node with string value set
    DialogPtr* add_string(std::string value, nw::LanguageID lang = nw::LanguageID::english, bool feminine = false);

    /// Adds empty Dialog Pointer and Node
    DialogPtr* add();

    /// Removes Dialog Ptr from underlying node
    void remove_ptr(DialogPtr* ptr);
};

void from_json(const nlohmann::json& archive, DialogPtr& ptr);
void to_json(nlohmann::json& archive, const DialogPtr& ptr);

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

    std::vector<DialogPtr*> pointers;
};

void from_json(const nlohmann::json& archive, DialogNode& node);
void to_json(nlohmann::json& archive, const DialogNode& node);

struct Dialog {
public:
    Dialog();
    explicit Dialog(const GffStruct archive);
    explicit Dialog(const nlohmann::json& archive);

    static constexpr int json_archive_version = 1;
    static constexpr ResourceType::type restype = ResourceType::dlg;

    /// Adds Dialog Pointer, if `is_link` is false no new pointer or node is created.
    /// if `is_link` is true a new pointer will created with the node copied from input pointer.
    DialogPtr* add_ptr(DialogPtr* ptr, bool is_link = false);

    /// Adds Dialog Pointer and Node with string value set
    DialogPtr* add_string(std::string value, nw::LanguageID lang = nw::LanguageID::english, bool feminine = false);

    /// Adds empty Dialog Pointer and Node
    DialogPtr* add();

    /// Creates a new Dialog Node.
    DialogNode* create_node(DialogNodeType type);

    /// Creates a new Dialog Pointer
    DialogPtr* create_ptr();

    /// Get Node index.
    size_t node_index(DialogNode* node, DialogNodeType type) const;

    /// Removes Dialog Ptr from underlying node
    void remove_ptr(DialogPtr* ptr);

    /// Checks id dialog was successfully parsed
    bool valid() const noexcept { return is_valid_; }

    std::vector<DialogNode*> entries;
    std::vector<DialogNode*> replies;
    Resref script_abort;
    Resref script_end;
    std::vector<DialogPtr*> starts;

    uint32_t delay_entry = 0;
    uint32_t delay_reply = 0;
    uint32_t word_count = 0;

    bool prevent_zoom = false;

private:
    bool is_valid_ = false;
    ObjectPool<DialogNode, 64> node_pool_;
    ObjectPool<DialogPtr, 64> ptr_pool_;

    bool load(const GffStruct gff);
    bool read_nodes(const GffStruct gff, DialogNodeType node_type);
};

void from_json(const nlohmann::json& archive, Dialog& node);
void to_json(nlohmann::json& archive, const Dialog& node);

} // namespace nw
