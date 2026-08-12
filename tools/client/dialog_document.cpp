#include "dialog_document.hpp"

#include <nw/kernel/Strings.hpp>
#include <nw/serialization/Gff.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>

namespace nw::toolset {
namespace {

constexpr size_t kMaxDialogRows = 65536;

DialogTextSlice append_text(std::string_view value, std::string& output)
{
    // Text slices are a 32-bit file-to-UI protocol; clamp at its representable limit.
    if (output.size() >= std::numeric_limits<uint32_t>::max()) {
        return {};
    }
    const size_t available = std::numeric_limits<uint32_t>::max() - output.size();
    const size_t length = std::min(value.size(), available);
    const auto offset = static_cast<uint32_t>(output.size());
    output.append(value.data(), length);
    return {offset, static_cast<uint32_t>(length)};
}

bool has_suffix(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size()
        && value.substr(value.size() - suffix.size()) == suffix;
}

std::unique_ptr<Dialog> parse_dialog(const std::filesystem::path& path,
    std::string& diagnostic)
{
    const std::string filename = path.filename().string();
    if (has_suffix(filename, ".dlg.json")) {
        std::ifstream input{path};
        if (!input) {
            diagnostic = "Unable to read dialog JSON.";
            return {};
        }

        try {
            nlohmann::json archive;
            input >> archive;
            auto dialog = std::make_unique<Dialog>(archive);
            if (!dialog->valid()) {
                diagnostic = "Dialog JSON is invalid.";
                return {};
            }
            return dialog;
        } catch (const std::exception& error) {
            diagnostic = std::string{"Unable to parse dialog JSON: "} + error.what();
            return {};
        }
    }

    if (path.extension() == ".dlg") {
        Gff archive{path};
        if (!archive.valid()) {
            diagnostic = archive.error().empty()
                ? std::string{"Unable to parse dialog GFF."}
                : archive.error();
            return {};
        }
        auto dialog = std::make_unique<Dialog>(archive.toplevel());
        if (!dialog->valid()) {
            diagnostic = "Dialog GFF is invalid.";
            return {};
        }
        return dialog;
    }

    diagnostic = "Expected a .dlg or .dlg.json resource.";
    return {};
}

DialogTextSlice append_localized_text(const LocString& value,
    std::string& output)
{
    return append_text(kernel::strings().get(value), output);
}

bool append_parameters(const Vector<std::pair<String, String>>& source,
    uint32_t& offset,
    uint16_t& count,
    DialogDocumentSnapshot& output)
{
    if (source.size() > std::numeric_limits<uint16_t>::max()
        || output.parameters.size() > std::numeric_limits<uint32_t>::max() - source.size()) {
        output.diagnostic = "Dialog parameter count exceeds the supported range.";
        return false;
    }

    offset = static_cast<uint32_t>(output.parameters.size());
    count = static_cast<uint16_t>(source.size());
    output.parameters.reserve(output.parameters.size() + source.size());
    for (const auto& [name, value] : source) {
        output.parameters.push_back({
            append_text(name, output.text),
            append_text(value, output.text),
        });
    }
    return true;
}

struct DialogTraversalFrame {
    const DialogPtr* pointer = nullptr;
    uint32_t parent = dialog_no_row;
    uint16_t depth = 0;
    bool leave_node = false;
};

bool build_dialog_rows(const Dialog& dialog, DialogDocumentSnapshot& output)
{
    size_t source_pointer_count = dialog.starts.size();
    for (const DialogNode* entry : dialog.entries) {
        source_pointer_count += entry->pointers.size();
    }
    for (const DialogNode* reply : dialog.replies) {
        source_pointer_count += reply->pointers.size();
    }
    if (source_pointer_count > std::numeric_limits<uint32_t>::max()) {
        output.diagnostic = "Dialog pointer count exceeds the supported range.";
        return false;
    }
    output.rows.reserve(std::min(source_pointer_count, kMaxDialogRows));

    std::vector<uint8_t> active_entries(dialog.entries.size());
    std::vector<uint8_t> active_replies(dialog.replies.size());
    std::vector<DialogTraversalFrame> stack;
    stack.reserve(std::min(source_pointer_count, kMaxDialogRows) * 2);
    for (auto it = dialog.starts.rbegin(); it != dialog.starts.rend(); ++it) {
        stack.push_back({*it, dialog_no_row, 0, false});
    }

    while (!stack.empty()) {
        const DialogTraversalFrame frame = stack.back();
        stack.pop_back();
        if (!frame.pointer) {
            output.diagnostic = "Dialog contains a null pointer.";
            return false;
        }

        const size_t node_count = frame.pointer->type == DialogNodeType::entry
            ? dialog.entries.size()
            : dialog.replies.size();
        if (frame.pointer->index >= node_count || !frame.pointer->node) {
            output.diagnostic = "Dialog pointer target is out of range.";
            return false;
        }
        auto& active = frame.pointer->type == DialogNodeType::entry
            ? active_entries
            : active_replies;
        if (frame.leave_node) {
            active[frame.pointer->index] = 0;
            continue;
        }

        if (output.rows.size() >= kMaxDialogRows) {
            output.diagnostic = "Dialog expands beyond the 65,536 row safety limit.";
            return false;
        }
        if (!frame.pointer->is_link && active[frame.pointer->index]) {
            output.diagnostic = "Dialog contains a non-link cycle.";
            return false;
        }

        const DialogNode& node = *frame.pointer->node;
        DialogDocumentRow row;
        row.parent = frame.parent;
        row.node_index = frame.pointer->index;
        row.depth = frame.depth;
        row.type = frame.pointer->type;
        row.animation = node.animation;
        row.delay = node.delay;
        row.text = append_localized_text(node.text, output.text);
        row.speaker = append_text(node.speaker, output.text);
        row.condition_script = append_text(frame.pointer->script_appears.view(), output.text);
        row.action_script = append_text(node.script_action.view(), output.text);
        row.sound = append_text(node.sound.view(), output.text);
        row.comment = append_text(node.comment, output.text);
        row.link_comment = append_text(frame.pointer->comment, output.text);
        row.quest = append_text(node.quest, output.text);
        row.quest_entry = node.quest_entry;
        row.child_count = frame.pointer->is_link
            ? 0
            : static_cast<uint32_t>(node.pointers.size());
        row.is_start = frame.pointer->is_start;
        row.is_link = frame.pointer->is_link;
        row.animation_loop = node.animation_loop;
        if (!append_parameters(frame.pointer->condition_params,
                row.condition_parameter_offset,
                row.condition_parameter_count,
                output)
            || !append_parameters(node.action_params,
                row.action_parameter_offset,
                row.action_parameter_count,
                output)) {
            return false;
        }

        const uint32_t row_index = static_cast<uint32_t>(output.rows.size());
        output.rows.push_back(row);
        if (frame.pointer->is_link || node.pointers.empty()) {
            continue;
        }
        if (frame.depth == std::numeric_limits<uint16_t>::max()) {
            output.diagnostic = "Dialog depth exceeds the supported range.";
            return false;
        }

        active[frame.pointer->index] = 1;
        stack.push_back({frame.pointer, frame.parent, frame.depth, true});
        for (auto it = node.pointers.rbegin(); it != node.pointers.rend(); ++it) {
            stack.push_back({*it, row_index, static_cast<uint16_t>(frame.depth + 1), false});
        }
    }

    return true;
}

} // namespace

std::string_view DialogDocumentSnapshot::text_view(DialogTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

void load_dialog_document(const std::filesystem::path& path,
    DialogDocumentSnapshot& output)
{
    output = {};
    output.source_path = path;

    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        output.status = DialogDocumentStatus::invalid_path;
        output.diagnostic = "Dialog resource does not exist or is not a file.";
        return;
    }

    auto dialog = parse_dialog(path, output.diagnostic);
    if (!dialog) {
        output.status = has_suffix(path.filename().string(), ".dlg.json")
                || path.extension() == ".dlg"
            ? DialogDocumentStatus::invalid_data
            : DialogDocumentStatus::invalid_path;
        return;
    }

    if (dialog->entries.size() > std::numeric_limits<uint32_t>::max()
        || dialog->replies.size() > std::numeric_limits<uint32_t>::max()
        || dialog->starts.size() > std::numeric_limits<uint32_t>::max()) {
        output.status = DialogDocumentStatus::invalid_data;
        output.diagnostic = "Dialog node count exceeds the supported range.";
        return;
    }

    output.entry_count = static_cast<uint32_t>(dialog->entries.size());
    output.reply_count = static_cast<uint32_t>(dialog->replies.size());
    output.start_count = static_cast<uint32_t>(dialog->starts.size());
    output.script_abort = append_text(dialog->script_abort.view(), output.text);
    output.script_end = append_text(dialog->script_end.view(), output.text);
    output.word_count = dialog->word_count;
    output.delay_entry = dialog->delay_entry;
    output.delay_reply = dialog->delay_reply;
    output.prevent_zoom = dialog->prevent_zoom;

    size_t link_count = 0;
    for (const DialogPtr* pointer : dialog->starts) {
        link_count += pointer && pointer->is_link ? 1u : 0u;
    }
    for (const DialogNode* entry : dialog->entries) {
        for (const DialogPtr* pointer : entry->pointers) {
            link_count += pointer && pointer->is_link ? 1u : 0u;
        }
    }
    for (const DialogNode* reply : dialog->replies) {
        for (const DialogPtr* pointer : reply->pointers) {
            link_count += pointer && pointer->is_link ? 1u : 0u;
        }
    }
    if (link_count > std::numeric_limits<uint32_t>::max()) {
        output.status = DialogDocumentStatus::invalid_data;
        output.diagnostic = "Dialog link count exceeds the supported range.";
        return;
    }
    output.link_count = static_cast<uint32_t>(link_count);

    if (!build_dialog_rows(*dialog, output)) {
        output.status = DialogDocumentStatus::invalid_data;
        output.rows.clear();
        output.parameters.clear();
        return;
    }

    output.status = DialogDocumentStatus::ready;
}

} // namespace nw::toolset
