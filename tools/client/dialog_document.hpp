#pragma once

#include <nw/formats/Dialog.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

constexpr uint32_t dialog_no_row = UINT32_MAX;

struct DialogTextSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct DialogParameterRow {
    DialogTextSlice name;
    DialogTextSlice value;
};

struct DialogDocumentRow {
    uint32_t parent = dialog_no_row;
    uint32_t node_index = 0;
    uint32_t condition_parameter_offset = 0;
    uint32_t action_parameter_offset = 0;
    uint16_t depth = 0;
    uint16_t condition_parameter_count = 0;
    uint16_t action_parameter_count = 0;
    DialogNodeType type = DialogNodeType::entry;
    DialogAnimation animation = DialogAnimation::default_;
    uint32_t delay = UINT32_MAX;
    DialogTextSlice text;
    DialogTextSlice speaker;
    DialogTextSlice condition_script;
    DialogTextSlice action_script;
    DialogTextSlice sound;
    DialogTextSlice comment;
    DialogTextSlice link_comment;
    DialogTextSlice quest;
    uint32_t quest_entry = UINT32_MAX;
    uint32_t child_count = 0;
    bool is_start = false;
    bool is_link = false;
    bool animation_loop = false;
};

enum class DialogDocumentStatus : uint8_t {
    empty,
    ready,
    invalid_path,
    invalid_data,
};

struct DialogDocumentSnapshot {
    std::filesystem::path source_path;
    DialogDocumentStatus status = DialogDocumentStatus::empty;
    std::vector<DialogDocumentRow> rows;
    std::vector<DialogParameterRow> parameters;
    std::string text;
    std::string diagnostic;
    DialogTextSlice script_abort;
    DialogTextSlice script_end;
    uint32_t entry_count = 0;
    uint32_t reply_count = 0;
    uint32_t start_count = 0;
    uint32_t link_count = 0;
    uint32_t word_count = 0;
    uint32_t delay_entry = 0;
    uint32_t delay_reply = 0;
    bool prevent_zoom = false;

    [[nodiscard]] std::string_view text_view(DialogTextSlice slice) const noexcept;
};

// One dialog document is the active workspace singleton. The parser-owned
// pointer graph is traversed once into indexed display rows; UI access is flat.
void load_dialog_document(const std::filesystem::path& path,
    DialogDocumentSnapshot& output);

} // namespace nw::toolset
