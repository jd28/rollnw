#include "dialog_view.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace nw::toolset {
namespace {

constexpr int kDialogRowHeightPx = 34;
constexpr int kDialogOverscanRows = 10;

std::string escape_html(std::string_view text)
{
    std::string output;
    output.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

std::string_view node_type_label(DialogNodeType type)
{
    return type == DialogNodeType::entry ? "Entry" : "Reply";
}

std::string_view animation_label(DialogAnimation animation)
{
    switch (animation) {
    case DialogAnimation::default_:
        return "Default";
    case DialogAnimation::taunt:
        return "Taunt";
    case DialogAnimation::greeting:
        return "Greeting";
    case DialogAnimation::listen:
        return "Listen";
    case DialogAnimation::worship:
        return "Worship";
    case DialogAnimation::salute:
        return "Salute";
    case DialogAnimation::bow:
        return "Bow";
    case DialogAnimation::steal:
        return "Steal";
    case DialogAnimation::talk_normal:
        return "Talk, normal";
    case DialogAnimation::talk_pleading:
        return "Talk, pleading";
    case DialogAnimation::talk_forceful:
        return "Talk, forceful";
    case DialogAnimation::talk_laugh:
        return "Talk, laugh";
    case DialogAnimation::victory_1:
        return "Victory 1";
    case DialogAnimation::victory_2:
        return "Victory 2";
    case DialogAnimation::victory_3:
        return "Victory 3";
    case DialogAnimation::look_far:
        return "Look far";
    case DialogAnimation::drink:
        return "Drink";
    case DialogAnimation::read:
        return "Read";
    case DialogAnimation::none:
        return "None";
    }
    return "Unknown";
}

class DialogListAdapter final : public VirtualListAdapter {
public:
    explicit DialogListAdapter(const DialogDocumentSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "dialog_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[static_cast<size_t>(index)];
        const auto node_text = snapshot_.text_view(row.text);
        const auto speaker = snapshot_.text_view(row.speaker);
        const int indent = 10 + std::min<int>(row.depth, 24) * 16;

        std::string markup;
        markup.reserve(320);
        markup += "<div class=\"dialog_row_cells dialog_row_";
        markup += row.type == DialogNodeType::entry ? "entry" : "reply";
        if (row.is_link) {
            markup += " link";
        }
        markup += "\" style=\"padding-left:";
        markup += std::to_string(indent);
        markup += "px\"><span class=\"tree_twisty";
        markup += !row.is_link && row.child_count > 0 ? " expanded" : " leaf";
        markup += "\"></span><span class=\"dialog_row_text\">";
        if (!speaker.empty()) {
            markup += "<span class=\"dialog_row_speaker\">";
            markup += escape_html(speaker);
            markup += ": </span>";
        }
        markup += escape_html(node_text.empty()
                ? (row.type == DialogNodeType::entry
                          ? std::string_view{"Untitled entry"}
                          : std::string_view{"Untitled reply"})
                : node_text);
        markup += "</span>";
        if (row.is_start) {
            markup += "<span class=\"dialog_row_badge\">start</span>";
        }
        if (row.is_link) {
            markup += "<span class=\"dialog_row_badge link\">link</span>";
        }
        markup += "</div>";
        return markup;
    }

private:
    const DialogDocumentSnapshot& snapshot_;
};

void configure_list(DialogViewState& state)
{
    if (state.list_configured) {
        return;
    }
    state.list.set_row_height(kDialogRowHeightPx);
    state.list.set_overscan(kDialogOverscanRows);
    state.list_configured = true;
}

void append_inspector_field(std::string& markup,
    std::string_view label,
    std::string_view value)
{
    if (value.empty()) {
        return;
    }
    markup += "<div class=\"dialog_inspector_field\"><span class=\"dialog_inspector_label\">";
    markup += escape_html(label);
    markup += "</span><span class=\"dialog_inspector_value\">";
    markup += escape_html(value);
    markup += "</span></div>";
}

void append_parameters(std::string& markup,
    const DialogDocumentSnapshot& document,
    uint32_t offset,
    uint16_t count)
{
    if (offset > document.parameters.size()
        || count > document.parameters.size() - offset) {
        return;
    }
    for (uint32_t index = offset; index < offset + count; ++index) {
        const auto& parameter = document.parameters[index];
        append_inspector_field(markup,
            document.text_view(parameter.name),
            document.text_view(parameter.value));
    }
}

void append_script_group(std::string& markup,
    std::string_view title,
    std::string_view script,
    const DialogDocumentSnapshot& document,
    uint32_t parameter_offset,
    uint16_t parameter_count)
{
    if (script.empty() && parameter_count == 0) {
        return;
    }
    markup += "<div class=\"dialog_inspector_group\"><div class=\"dialog_inspector_group_title\">";
    markup += escape_html(title);
    markup += "</div>";
    append_inspector_field(markup, "Script", script);
    append_parameters(markup, document, parameter_offset, parameter_count);
    markup += "</div>";
}

void sync_inspector(Rml::ElementDocument* document, const DialogViewState& state)
{
    auto* inspector = document ? document->GetElementById("dialog_inspector") : nullptr;
    if (!inspector) {
        return;
    }

    const int selected = state.list.selected();
    if (state.document.status != DialogDocumentStatus::ready
        || selected < 0
        || static_cast<size_t>(selected) >= state.document.rows.size()) {
        inspector->SetInnerRML("<div class=\"dialog_inspector_empty\">Select a conversation node.</div>");
        return;
    }

    const auto& row = state.document.rows[static_cast<size_t>(selected)];
    std::string markup;
    markup.reserve(1600);
    markup += "<div class=\"dialog_inspector_header\"><span>";
    markup += node_type_label(row.type);
    markup += " ";
    markup += std::to_string(row.node_index);
    markup += "</span><span class=\"dialog_inspector_header_badges\">";
    if (row.is_start) {
        markup += "<span class=\"dialog_row_badge\">start</span>";
    }
    if (row.is_link) {
        markup += "<span class=\"dialog_row_badge link\">link</span>";
    }
    markup += "</span></div><div class=\"dialog_inspector_scroll\">";

    markup += "<div class=\"dialog_inspector_text\">";
    const auto text = state.document.text_view(row.text);
    markup += escape_html(text.empty() ? std::string_view{"No text"} : text);
    markup += "</div>";
    append_inspector_field(markup, "Speaker", state.document.text_view(row.speaker));
    append_inspector_field(markup, "Comment", state.document.text_view(row.comment));
    append_inspector_field(markup, "Link comment", state.document.text_view(row.link_comment));

    append_script_group(markup,
        "Condition",
        state.document.text_view(row.condition_script),
        state.document,
        row.condition_parameter_offset,
        row.condition_parameter_count);
    append_script_group(markup,
        "Action",
        state.document.text_view(row.action_script),
        state.document,
        row.action_parameter_offset,
        row.action_parameter_count);

    markup += "<div class=\"dialog_inspector_group\"><div class=\"dialog_inspector_group_title\">Node</div>";
    append_inspector_field(markup, "Sound", state.document.text_view(row.sound));
    append_inspector_field(markup, "Quest", state.document.text_view(row.quest));
    if (row.quest_entry != UINT32_MAX) {
        append_inspector_field(markup, "Quest entry", std::to_string(row.quest_entry));
    }
    std::string animation{animation_label(row.animation)};
    animation += " (";
    animation += std::to_string(static_cast<uint32_t>(row.animation));
    animation += ")";
    append_inspector_field(markup, "Animation", animation);
    if (row.animation_loop) {
        append_inspector_field(markup, "Animation loop", "Yes");
    }
    if (row.delay != UINT32_MAX) {
        append_inspector_field(markup, "Delay", std::to_string(row.delay));
    }
    append_inspector_field(markup, "Children", std::to_string(row.child_count));
    markup += "</div>";

    markup += "<div class=\"dialog_inspector_group\"><div class=\"dialog_inspector_group_title\">Conversation</div>";
    append_inspector_field(markup, "Abort script", state.document.text_view(state.document.script_abort));
    append_inspector_field(markup, "End script", state.document.text_view(state.document.script_end));
    append_inspector_field(markup, "Entry delay", std::to_string(state.document.delay_entry));
    append_inspector_field(markup, "Reply delay", std::to_string(state.document.delay_reply));
    append_inspector_field(markup, "Word count", std::to_string(state.document.word_count));
    append_inspector_field(markup, "Prevent zoom", state.document.prevent_zoom ? "Yes" : "No");
    markup += "</div></div>";
    inspector->SetInnerRML(markup);
}

} // namespace

void clear_dialog_view(DialogViewState& state)
{
    state = {};
    configure_list(state);
}

void load_dialog_view(DialogViewState& state,
    const std::filesystem::path& path,
    std::string tab_id)
{
    load_dialog_document(path, state.document);
    state.tab_id = std::move(tab_id);
    configure_list(state);
    state.list.set_total_rows(static_cast<int>(state.document.rows.size()));
    state.list.set_scroll_top(0);
    state.list.set_selected(state.document.rows.empty() ? -1 : 0);
    state.rendered = false;
}

std::string dialog_view_markup(const DialogViewState& state)
{
    std::string markup;
    markup.reserve(640);
    markup += "<div class=\"dialog_surface\"><div class=\"dialog_summary\">";
    markup += "<span class=\"dialog_summary_words\">";
    markup += std::to_string(state.document.word_count);
    markup += " words</span>";
    markup += "<span>" + std::to_string(state.document.start_count) + " starts</span>";
    markup += "<span>" + std::to_string(state.document.entry_count) + " entries</span>";
    markup += "<span>" + std::to_string(state.document.reply_count) + " replies</span>";
    markup += "<span>" + std::to_string(state.document.link_count) + " links</span>";
    markup += "</div><div class=\"dialog_body\"><div class=\"dialog_outline\">";
    markup += "<div class=\"dialog_pane_header\"><span>Conversation</span><span>";
    markup += std::to_string(state.document.rows.size());
    markup += " rows</span></div><div id=\"dialog_rows\" class=\"dialog_rows\">";
    markup += "<div class=\"dialog_empty\">Loading conversation...</div></div></div>";
    markup += "<div id=\"dialog_inspector\" class=\"dialog_inspector\">";
    markup += "<div class=\"dialog_inspector_empty\">Select a conversation node.</div>";
    markup += "</div></div></div>";
    return markup;
}

bool sync_dialog_view(Rml::ElementDocument* document,
    DialogViewState& state,
    bool force)
{
    auto* list = document ? document->GetElementById("dialog_rows") : nullptr;
    if (!list) {
        return false;
    }

    configure_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.list.set_viewport_height(viewport_height);
    state.list.set_scroll_top(scroll_top);
    const auto range = state.list.compute_range();
    const int row_count = static_cast<int>(state.document.rows.size());

    if (!force && state.rendered
        && row_count == state.rendered_row_count
        && range.start == state.rendered_range.start
        && range.end == state.rendered_range.end) {
        return false;
    }

    std::string markup;
    if (state.document.status != DialogDocumentStatus::ready) {
        markup = "<div class=\"dialog_empty error\">";
        markup += escape_html(state.document.diagnostic.empty()
                ? std::string_view{"Unable to load dialog."}
                : std::string_view{state.document.diagnostic});
        markup += "</div>";
    } else if (state.document.rows.empty()) {
        markup = "<div class=\"dialog_empty\">This conversation has no starting nodes.</div>";
    } else {
        markup = render_virtual_list(state.list, DialogListAdapter{state.document});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    state.rendered_range = range;
    state.rendered_row_count = row_count;
    state.rendered = true;
    sync_inspector(document, state);
    return true;
}

bool select_dialog_view_row(DialogViewState& state, int row)
{
    if (row < 0 || static_cast<size_t>(row) >= state.document.rows.size()) {
        return false;
    }
    state.list.set_selected(row);
    state.rendered = false;
    return true;
}

} // namespace nw::toolset
