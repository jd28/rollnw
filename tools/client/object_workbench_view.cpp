#include "object_workbench_view.hpp"
#include "command_view.hpp"
#include "object_document.hpp"
#include "rml_action_line_edit.hpp"
#include "smalls_rmlui.hpp"
#include "smalls_ui_v1.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>
#include <type_traits>
#include <utility>
namespace nw::toolset {
namespace {
constexpr int kObjectDetailsRowHeightPx = 30;
constexpr int kObjectDetailsOverscanRows = 8;
constexpr int kObjectVariableRowHeightPx = 34;
constexpr int kObjectVariableOverscanRows = 8;
std::string escape_html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string locstring_strref_preview_markup(std::optional<uint32_t> strref)
{
    if (!strref || *strref == UINT32_MAX) { return "No TLK text to preview."; }
    const auto resolved = kernel::strings().get(*strref);
    return resolved.empty() ? "No TLK text to preview." : escape_html(resolved);
}

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}

Rml::Element* find_ancestor_with_class(Rml::Element* element, std::string_view class_name)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->IsClassSet(class_name.data())) {
            return cursor;
        }
    }
    return nullptr;
}

std::optional<int32_t> parse_decimal_int32(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    int32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

constexpr std::array kLocStringLanguages{
    LanguageID::english,
    LanguageID::french,
    LanguageID::german,
    LanguageID::italian,
    LanguageID::spanish,
    LanguageID::polish,
    LanguageID::korean,
    LanguageID::chinese_traditional,
    LanguageID::chinese_simplified,
    LanguageID::japanese,
};

int32_t locstring_source_key(std::optional<LanguageID> language)
{
    return language ? static_cast<int32_t>(*language) : -1;
}

bool locstring_source_language(int32_t key, std::optional<LanguageID>& language)
{
    if (key == -1) {
        language.reset();
        return true;
    }
    const auto found = std::ranges::find(kLocStringLanguages, static_cast<LanguageID>(key));
    if (found == kLocStringLanguages.end()) { return false; }
    language = *found;
    return true;
}

std::vector<VirtualComboBoxItem> locstring_source_options()
{
    std::vector<VirtualComboBoxItem> options;
    options.reserve(kLocStringLanguages.size() + 1);
    options.push_back({.key = -1, .label = "String Reference"});
    for (const auto language : kLocStringLanguages) {
        options.push_back({.key = static_cast<int32_t>(language),
            .label = std::string{Language::to_string(language, true)}});
    }
    return options;
}

class ObjectDetailsListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit ObjectDetailsListAdapter(
        const nw::toolset::ObjectDetailsSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "object_details_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[static_cast<size_t>(index)];
        const bool section = row.kind
            == nw::toolset::ObjectDetailsRowKind::section;
        std::string markup;
        markup.reserve(256);
        markup += "<div class=\"property_tree_cells";
        if (section) {
            markup += " section";
        } else if ((index & 1) != 0) {
            markup += " alternate";
        }
        markup += "\"><div class=\"property_tree_name\" style=\"padding-left:";
        markup += section ? "8px;\">" : "22px;\">";
        markup += "<span class=\"tree_twisty leaf\"></span><span class=\"property_tree_label\">";
        markup += escape_html(snapshot_.text_view(row.label));
        markup += "</span></div><div class=\"property_tree_value";
        const auto value = snapshot_.text_view(row.value);
        if (!section && value.empty()) {
            markup += " empty";
        }
        markup += "\">";
        if (!section) {
            if (row.editor == nw::toolset::ObjectDetailsEditorKind::boolean) {
                markup += "<span class=\"object_details_boolean\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\"><span class=\"object_details_boolean_box";
                if (row.edit_value != 0) {
                    markup += " checked";
                }
                markup += "\">";
                if (row.edit_value != 0) {
                    markup += "&#10003;";
                }
                markup += "</span><span class=\"object_details_boolean_text\">";
                markup += escape_html(value);
                markup += "</span></span>";
            } else if (row.editor == nw::toolset::ObjectDetailsEditorKind::integer) {
                markup += "<span class=\"object_details_integer_spinner\"><button type=\"button\" "
                          "class=\"object_details_integer_step\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" data-delta=\"-1\" title=\"Decrease; minimum ";
                markup += std::to_string(row.edit_min);
                markup += "\"";
                if (row.edit_value <= row.edit_min) {
                    markup += " disabled";
                }
                markup += ">-</button><input class=\"object_details_integer\" type=\"text\" maxlength=\"11\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" data-min=\"";
                markup += std::to_string(row.edit_min);
                markup += "\" data-max=\"";
                markup += std::to_string(row.edit_max);
                markup += "\" value=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" title=\"Valid range: ";
                markup += std::to_string(row.edit_min);
                markup += " to ";
                markup += std::to_string(row.edit_max);
                markup += ". Use Up/Down to adjust; press Enter to apply.\"/><button type=\"button\" "
                          "class=\"object_details_integer_step\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" data-delta=\"1\" title=\"Increase; maximum ";
                markup += std::to_string(row.edit_max);
                markup += "\"";
                if (row.edit_value >= row.edit_max) {
                    markup += " disabled";
                }
                markup += ">+</button></span>";
            } else if (row.editor == nw::toolset::ObjectDetailsEditorKind::door_state) {
                markup += "<button type=\"button\" class=\"object_details_cycle_state\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" title=\"Cycle authored state: Closed, Open 1, Open 2\">";
                markup += escape_html(value);
                markup += "</button>";
            } else if (row.editor
                == nw::toolset::ObjectDetailsEditorKind::sound_position) {
                markup += "<button id=\"object_details_sound_position_field_";
                markup += std::to_string(index);
                markup += "\" type=\"button\" class=\"combobox_field "
                          "object_details_sound_position_field\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" title=\"Choose sound placement\">"
                          "<span class=\"combobox_value\">";
                markup += escape_html(value);
                markup += "</span><span class=\"combobox_arrow\">"
                          "<span class=\"combobox_arrow_indicator\"></span>"
                          "</span></button>";
            } else if (row.editor
                == nw::toolset::ObjectDetailsEditorKind::sound_volume) {
                const auto volume = nw::toolset::sound_volume_editor_value(
                    row.edit_value);
                if (!volume) {
                    markup += "Invalid";
                } else {
                    markup += "<span class=\"object_details_sound_volume_control\">"
                              "<input class=\"object_details_sound_volume\" type=\"range\" "
                              "min=\"0\" max=\"10\" step=\"1\" data-row=\"";
                    markup += std::to_string(index);
                    markup += "\" data-current=\"";
                    markup += std::to_string(row.edit_value);
                    markup += "\" value=\"";
                    markup += std::to_string(*volume);
                    markup += "\" title=\"Volume: 0 to 10\"/>"
                              "<span class=\"object_details_sound_volume_value\">";
                    markup += std::to_string(*volume);
                    markup += "</span></span>";
                }
            } else if (row.editor
                == nw::toolset::ObjectDetailsEditorKind::locstring) {
                std::string action
                    = "<button class=\"action_line_edit_action object_workbench_surface_button\" "
                      "data-surface=\"locstring\" data-row=\"";
                action += std::to_string(index);
                action += "\" type=\"button\" "
                          "title=\"Edit localized string\" aria-label=\"Edit localized string\">&#8230;</button>";
                markup += render_rml_action_line_edit(
                    value, "Not set", action);
            } else {
                markup += escape_html(value.empty() ? std::string_view{"Not set"} : value);
            }
        }
        markup += "</div></div>";
        return markup;
    }

private:
    const nw::toolset::ObjectDetailsSnapshot& snapshot_;
};

class ObjectVariableListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit ObjectVariableListAdapter(
        const nw::toolset::ObjectVariableSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "object_variable_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& snapshot_row = snapshot_.rows[static_cast<size_t>(index)];
        const auto& row = snapshot_row.variable;
        const std::string type = std::to_string(static_cast<uint32_t>(row.type));
        const bool duplicate = nw::toolset::has_object_variable_warning(
            snapshot_row.warnings, nw::toolset::ObjectVariableWarning::duplicate_name);
        const bool looks_integer = nw::toolset::has_object_variable_warning(
            snapshot_row.warnings, nw::toolset::ObjectVariableWarning::string_looks_integer);
        const bool looks_floating = nw::toolset::has_object_variable_warning(
            snapshot_row.warnings, nw::toolset::ObjectVariableWarning::string_looks_floating);
        const std::string value = nw::toolset::format_object_variable_value(row);
        std::string markup;
        markup.reserve(512 + row.name.size() + row.string.size()
            + (row.type == nw::toolset::ObjectVariableType::string ? 0 : value.size()));
        markup += "<div class=\"object_variable_cells\"><div class=\"object_variable_field object_variable_name_field";
        if (duplicate) {
            markup += " warning_duplicate";
        }
        markup += "\">";
        if (duplicate) {
            const auto warning = nw::toolset::object_variable_warning_description(
                nw::toolset::ObjectVariableWarning::duplicate_name);
            markup += "<span class=\"object_variable_field_warning\" title=\"";
            markup += escape_html(warning);
            markup += "\" data-tooltip=\"";
            markup += escape_html(warning);
            markup += "\">!</span>";
        }
        markup += "<input class=\"object_variable_name\" type=\"text\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        markup += "\" value=\"";
        markup += escape_html(row.name);
        markup += "\" title=\"Press Enter to rename\"/></div><button type=\"button\" "
                  "class=\"object_variable_type\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        markup += "\" title=\"Click to change type\">";
        markup += nw::toolset::object_variable_type_name(row.type);
        markup += "</button><div class=\"object_variable_field object_variable_value_field";
        if (looks_integer || looks_floating) {
            markup += " warning_type";
        }
        markup += "\">";
        if (looks_integer || looks_floating) {
            const auto warning = looks_integer
                ? nw::toolset::ObjectVariableWarning::string_looks_integer
                : nw::toolset::ObjectVariableWarning::string_looks_floating;
            markup += "<span class=\"object_variable_field_warning\" title=\"";
            const auto description = nw::toolset::object_variable_warning_description(warning);
            markup += escape_html(description);
            markup += "\" data-tooltip=\"";
            markup += escape_html(description);
            markup += "\">!</span>";
        }
        markup += "<input class=\"object_variable_value\" type=\"text\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        if (row.type != nw::toolset::ObjectVariableType::string) {
            markup += "\" data-last-valid=\"";
            markup += escape_html(value);
        }
        markup += "\" value=\"";
        markup += escape_html(value);
        markup += "\" title=\"Press Enter to apply\"/></div><button type=\"button\" "
                  "class=\"object_variable_remove\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        markup += "\" title=\"Remove variable\">&#215;</button></div>";
        return markup;
    }

private:
    const nw::toolset::ObjectVariableSnapshot& snapshot_;
};

// Object variable text inputs commit on Enter or blur. RmlUi emits Sound
// volume changes throughout a drag, so retain only the newest slider value and
// commit it when that input gesture ends.
class ObjectWorkbenchChangeHandler {
public:
    ObjectWorkbenchChangeHandler(ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
        ToolsetBackend& backend, ShellController& shell, const CommandContext& context)
        : state_{state}
        , workspace_{workspace}
        , backend_{backend}
        , shell_{shell}
        , context_{context}
    {
    }

    void process_event(Rml::Event& event)
    {
        auto* target = event.GetTargetElement();
        if (event == Rml::EventId::Click && target) {
            if (find_ancestor_with_class(target, "object_locstring_gender_toggle")) {
                if (!locstring_panel_active()
                    || !state_.locstring_language
                    || !Language::has_feminine(*state_.locstring_language)
                    || !commit_visible_locstring_draft(target)) { return; }
                state_.locstring_feminine = !state_.locstring_feminine;
                state_.locstring_panel_rendered = false;
                return;
            }
        }
        if (event == Rml::EventId::Change
            && stage_sound_volume(event)) {
            return;
        }
        if (event == Rml::EventId::Blur
            && event.GetTargetElement()
            && event.GetTargetElement()->IsClassSet(
                "object_details_sound_volume")) {
            (void)commit_sound_volume();
            return;
        }
        if (event == Rml::EventId::Change) {
            if (target && target->IsClassSet("object_locstring_text_input")) { return; }
            if (!event.GetParameter<bool>("linebreak", false)) {
                reject_invalid_numeric_input(target);
                preview_locstring_strref(target);
                return;
            }

            if (commit_input(target)) {
                state_.suppress_blur_commit = true;
                target->Blur();
                state_.suppress_blur_commit = false;
            }
            return;
        }

        if (event != Rml::EventId::Blur || state_.suppress_blur_commit) {
            return;
        }

        (void)commit_input(target);
    }

    bool commit_sound_volume()
    {
        if (!state_.pending_sound_volume) {
            return false;
        }
        const auto pending = std::move(*state_.pending_sound_volume);
        state_.pending_sound_volume.reset();
        if (!active_object_details_matches_tab(state_, workspace_)
            || state_.object_details.object != pending.object
            || workspace_.active_tab_id() != pending.tab_id
            || context_.active_tab_id != pending.tab_id
            || backend_.module_generation() != pending.module_generation
            || smalls_rmlui_host().active_object() != pending.object
            || pending.row >= state_.object_details.rows.size()) {
            return false;
        }
        const auto& row = state_.object_details.rows[pending.row];
        if (row.kind != ObjectDetailsRowKind::value
            || row.editor != ObjectDetailsEditorKind::sound_volume
            || row.edit_value != pending.current) {
            return false;
        }
        if (pending.current == pending.desired) {
            return true;
        }
        const std::string row_text = std::to_string(pending.row);
        const std::string current = std::to_string(pending.current);
        const std::string desired = std::to_string(pending.desired);
        const auto result = backend_.execute_command(
            "object.details.set_integer",
            {row_text, current, desired},
            context_);
        append_command_results(shell_, {&result, 1});
        return result.ok();
    }

    bool select_locstring_source(Rml::Element* source, int32_t key)
    {
        std::optional<LanguageID> language;
        if (!locstring_panel_active()
            || !locstring_source_language(key, language)
            || !commit_visible_locstring_draft(source)) { return false; }
        state_.locstring_language = language;
        state_.locstring_feminine = false;
        state_.locstring_panel_rendered = false;
        return true;
    }

private:
    void preview_locstring_strref(Rml::Element* target) const
    {
        if (!target || !target->IsClassSet("object_locstring_strref_input")
            || !locstring_panel_active() || state_.locstring_language
            || !state_.locstring_panel_rendered) { return; }
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        auto* preview = find_el(target->GetOwnerDocument(), "object_locstring_resolved");
        if (!input || !preview) { return; }
        preview->SetInnerRML(locstring_strref_preview_markup(
            nw::toolset::parse_locstring_strref(input->GetValue())));
    }

    bool locstring_panel_active() const
    {
        return state_.object_workbench_surface == ObjectWorkbenchSurface::locstring
            && state_.locstring_row
            && *state_.locstring_row < state_.object_details.rows.size()
            && state_.object_details.rows[*state_.locstring_row].editor
            == ObjectDetailsEditorKind::locstring
            && active_object_details_matches_tab(state_, workspace_)
            && context_.active_tab_id == workspace_.active_tab_id()
            && smalls_rmlui_host().active_object() == state_.object_details.object;
    }

    bool commit_visible_locstring_draft(Rml::Element* source)
    {
        auto* doc = source ? source->GetOwnerDocument() : nullptr;
        if (!doc) { return false; }
        if (state_.locstring_language) {
            auto* text = find_el(doc, "object_locstring_text");
            auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(text);
            return input && (input->GetValue() == state_.locstring_text_before || commit_input(text));
        }
        Rml::ElementList fields;
        doc->GetElementsByClassName(fields, "object_locstring_strref_input");
        if (fields.empty()) { return false; }
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(fields.front());
        if (!input) { return false; }
        const std::string current = fields.front()->GetAttribute<Rml::String>(
            "data-current", "");
        const std::string displayed = current == std::to_string(UINT32_MAX)
            ? std::string{}
            : current;
        return input->GetValue() == displayed || commit_input(fields.front());
    }

    bool stage_sound_volume(Rml::Event& event)
    {
        auto* target = event.GetTargetElement();
        if (!target
            || !target->IsClassSet("object_details_sound_volume")) {
            return false;
        }
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        if (!input) {
            state_.pending_sound_volume.reset();
            return true;
        }
        const float event_value = event.GetParameter<float>("value", -1.0f);
        if (!std::isfinite(event_value) || event_value < 0.0f || event_value > 10.0f) {
            state_.pending_sound_volume.reset();
            return true;
        }
        const int32_t editor_value = static_cast<int32_t>(std::lround(event_value));
        const auto stored_value = std::abs(
                                      event_value - static_cast<float>(editor_value))
                < 0.001f
            ? nw::toolset::sound_volume_storage_value(editor_value)
            : std::nullopt;
        const auto row = parse_decimal_int32(input->GetAttribute<Rml::String>(
            "data-row", ""));
        const auto current = parse_decimal_int32(
            input->GetAttribute<Rml::String>("data-current", ""));
        if (!stored_value || !row || *row < 0 || !current
            || !active_object_details_matches_tab(state_, workspace_)
            || context_.active_tab_id != workspace_.active_tab_id()
            || smalls_rmlui_host().active_object() != state_.object_details.object
            || static_cast<size_t>(*row) >= state_.object_details.rows.size()) {
            state_.pending_sound_volume.reset();
            return true;
        }
        const auto& details_row = state_.object_details.rows[static_cast<size_t>(*row)];
        if (details_row.kind != ObjectDetailsRowKind::value
            || details_row.editor
                != nw::toolset::ObjectDetailsEditorKind::sound_volume
            || details_row.edit_value != *current) {
            state_.pending_sound_volume.reset();
            return true;
        }
        state_.pending_sound_volume = PendingSoundVolume{
            .object = state_.object_details.object,
            .tab_id = workspace_.active_tab_id(),
            .module_generation = backend_.module_generation(),
            .row = static_cast<uint32_t>(*row),
            .current = *current,
            .desired = *stored_value,
        };
        if (auto* value = input->GetNextSibling(); value
            && value->IsClassSet("object_details_sound_volume_value")) {
            value->SetInnerRML(std::to_string(editor_value));
        }
        return true;
    }

    static void reject_invalid_numeric_input(Rml::Element* target)
    {
        const bool strref = target && target->IsClassSet("object_locstring_strref_input");
        if (!strref && (!target || !target->IsClassSet("object_variable_value"))) {
            return;
        }

        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        if (!input) {
            return;
        }

        const Rml::String value = input->GetValue();
        bool valid = false;
        if (strref) {
            valid = nw::toolset::valid_locstring_strref_input_prefix(value);
        } else {
            const auto type_value = parse_decimal_int32(
                input->GetAttribute<Rml::String>("data-type", ""));
            if (!type_value) { return; }
            const auto type = static_cast<nw::toolset::ObjectVariableType>(*type_value);
            if (type == nw::toolset::ObjectVariableType::string) { return; }
            valid = nw::toolset::valid_object_variable_input_prefix(type, value);
        }
        if (valid) {
            input->SetAttribute("data-last-valid", value);
            return;
        }

        int selection_start = 0;
        input->GetSelection(&selection_start, nullptr, nullptr);
        const Rml::String previous = input->GetAttribute<Rml::String>(
            "data-last-valid", "");
        const auto rml_character_count = [](const Rml::String& text) {
            return static_cast<int>(std::min(
                Rml::StringUtilities::LengthUTF8(text),
                static_cast<size_t>(std::numeric_limits<int>::max())));
        };
        const int value_characters = rml_character_count(value);
        const int previous_characters = rml_character_count(previous);
        const int inserted_characters = std::max(
            value_characters - previous_characters, 0);
        const int cursor = std::clamp(
            selection_start - inserted_characters, 0, previous_characters);
        input->SetValue(previous);
        input->SetSelectionRange(cursor, cursor);
    }

    bool commit_input(Rml::Element* target)
    {
        const bool panel_text = target && target->IsClassSet("object_locstring_text_input");
        const bool panel_strref = target && target->IsClassSet("object_locstring_strref_input");
        if (panel_text || panel_strref) {
            auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(target);
            const auto row_index = parse_decimal_int32(target->GetAttribute<Rml::String>("data-row", ""));
            if (!input || !row_index || *row_index < 0
                || !locstring_panel_active() || !state_.locstring_panel_rendered
                || !state_.locstring_row
                || static_cast<uint32_t>(*row_index) != *state_.locstring_row
                || static_cast<size_t>(*row_index) >= state_.object_details.rows.size()
                || state_.object_details.rows[static_cast<size_t>(*row_index)].editor
                    != ObjectDetailsEditorKind::locstring) {
                return false;
            }
            const std::string row_text = std::to_string(*row_index);
            std::string desired = input->GetValue();
            CommandResult result;
            if (panel_text) {
                if (!state_.locstring_language) { return false; }
                const auto runtime_id = Language::to_runtime_id(*state_.locstring_language,
                    state_.locstring_feminine);
                if (target->GetAttribute<Rml::String>("data-runtime", "")
                    != std::to_string(runtime_id)) { return false; }
                if (desired == state_.locstring_text_before) { return true; }
                const std::string language_text = std::to_string(runtime_id);
                result = backend_.execute_command("object.details.set_locstring_text",
                    {row_text, language_text, state_.locstring_text_before, desired}, context_);
            } else {
                if (state_.locstring_language) { return false; }
                const std::string expected = target->GetAttribute<Rml::String>("data-current", "");
                const auto parsed = nw::toolset::parse_locstring_strref(desired);
                const std::string displayed = expected == std::to_string(UINT32_MAX)
                    ? std::string{}
                    : expected;
                if (!parsed) {
                    input->SetValue(displayed);
                    target->SetAttribute("data-last-valid", displayed);
                    preview_locstring_strref(target);
                    return false;
                }
                desired = *parsed == UINT32_MAX ? "" : std::to_string(*parsed);
                input->SetValue(desired);
                target->SetAttribute("data-last-valid", desired);
                preview_locstring_strref(target);
                if (desired == displayed) { return true; }
                result = backend_.execute_command("object.details.set_locstring_strref",
                    {row_text, expected, desired}, context_);
            }
            append_command_results(shell_, {&result, 1});
            if (result.ok()) {
                if (panel_text) {
                    state_.locstring_text_before = desired;
                } else {
                    target->SetAttribute("data-current",
                        desired.empty() ? std::to_string(UINT32_MAX) : desired);
                    state_.locstring_panel_rendered = false;
                }
            }
            return result.ok();
        }
        const bool rename = target && target->IsClassSet("object_variable_name");
        const bool set_value = target && target->IsClassSet("object_variable_value");
        if (!rename && !set_value) {
            return false;
        }

        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        if (!input) {
            return false;
        }

        const std::string name = input->GetAttribute<Rml::String>("data-name", "");
        const std::string type = input->GetAttribute<Rml::String>("data-type", "");
        const std::string desired = input->GetValue();
        const auto result = backend_.execute_command(
            rename ? "object.variables.rename" : "object.variables.set_value",
            {name, type, desired},
            context_);
        append_command_results(shell_, {&result, 1});
        return result.ok();
    }

    ObjectWorkbenchViewState& state_;
    const WorkspaceState& workspace_;
    ToolsetBackend& backend_;
    ShellController& shell_;
    const CommandContext& context_;
};

void hydrate_creature_workbench(Rml::ElementDocument* doc, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    if (!find_el(doc, "creature_surface_details")) {
        return;
    }

    struct CreatureSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        CreatureSurfaceElements{"creature_tab_details", "creature_surface_details",
            ObjectWorkbenchSurface::details},
        CreatureSurfaceElements{"", "object_surface_locstring",
            ObjectWorkbenchSurface::locstring},
        CreatureSurfaceElements{"creature_tab_sheet", "creature_surface_sheet",
            ObjectWorkbenchSurface::sheet},
        CreatureSurfaceElements{"creature_tab_variables", "creature_surface_variables",
            ObjectWorkbenchSurface::variables},
        CreatureSurfaceElements{"creature_tab_classes", "creature_surface_classes",
            ObjectWorkbenchSurface::classes},
        CreatureSurfaceElements{"creature_tab_appearance", "creature_surface_appearance",
            ObjectWorkbenchSurface::appearance},
        CreatureSurfaceElements{"creature_tab_feats", "creature_surface_feats",
            ObjectWorkbenchSurface::feats},
        CreatureSurfaceElements{"creature_tab_spells", "creature_surface_spells",
            ObjectWorkbenchSurface::spells},
        CreatureSurfaceElements{"creature_tab_inventory", "creature_surface_inventory",
            ObjectWorkbenchSurface::inventory},
    };
    for (const auto& elements : surfaces) {
        if (elements.tab_id[0] != '\0') {
            if (auto* tab = find_el(doc, elements.tab_id)) {
                tab->SetClass("active", state.object_workbench_surface == elements.surface);
            }
        }
        if (auto* surface = find_el(doc, elements.surface_id)) {
            surface->SetClass("active", state.object_workbench_surface == elements.surface);
        }
    }

    const auto* active_tab = workspace.active_tab();
    const bool show_header = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type == nw::ObjectType::creature;
    if (auto* header = find_el(doc, "creature_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "creature_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }

    std::string markup;
    switch (state.object_workbench_surface) {
    case ObjectWorkbenchSurface::classes:
        append_creature_classes_markup(markup, state.creature_view, object_workbench_target(state, workspace));
        if (auto* target = find_el(doc, "creature_classes_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    case ObjectWorkbenchSurface::appearance:
        markup.clear();
        if (active_color_editor_matches_tab(state.appearance_view, object_workbench_target(state, workspace))) {
            append_creature_color_selector_markup(markup, state.appearance_view, object_workbench_target(state, workspace));
        } else if (state.appearance_view.appearance_selector_open) {
            append_appearance_selector_markup(markup, state.appearance_view);
        } else {
            if (auto* main = find_el(doc, "creature_appearance_main")) {
                main->SetClass("active", true);
            }
            if (auto* modal = find_el(doc, "creature_appearance_modal_dynamic")) {
                modal->SetClass("active", false);
                modal->SetInnerRML("");
            }
            std::string primary_markup;
            append_appearance_catalog_field_markup(
                primary_markup, state.appearance_view, AppearanceEditorField::appearance);
            if (auto* primary = find_el(
                    doc, "creature_appearance_primary_dynamic")) {
                primary->SetInnerRML(primary_markup);
            }
            std::string secondary_markup;
            append_creature_accessories_markup(secondary_markup, state.appearance_view, object_workbench_target(state, workspace));
            append_creature_colors_markup(secondary_markup, object_workbench_target(state, workspace));
            if (auto* secondary = find_el(
                    doc, "creature_appearance_secondary_dynamic")) {
                secondary->SetInnerRML(secondary_markup);
            }
            break;
        }
        if (auto* main = find_el(doc, "creature_appearance_main")) {
            main->SetClass("active", false);
        }
        if (auto* modal = find_el(doc, "creature_appearance_modal_dynamic")) {
            modal->SetClass("active", true);
            modal->SetInnerRML(markup);
        }
        break;
    case ObjectWorkbenchSurface::feats:
        if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(find_el(doc, "creature_feat_search"))) {
            input->SetValue(state.creature_view.creature_feat_query);
        }
        break;
    case ObjectWorkbenchSurface::spells:
        append_creature_spell_markup(markup, state.creature_view, object_workbench_target(state, workspace));
        if (auto* target = find_el(doc, "creature_spells_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    case ObjectWorkbenchSurface::inventory:
        append_creature_inventory_markup(markup, state.inventory_view, object_workbench_target(state, workspace));
        if (auto* target = find_el(doc, "creature_inventory_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    default:
        break;
    }

    markup.clear();
    append_creature_workbench_overlay_markup(markup, state.creature_view, object_workbench_target(state, workspace));
    if (auto* overlays = find_el(doc, "creature_workbench_dynamic_overlays")) {
        overlays->SetInnerRML(markup);
    }
}

void hydrate_item_workbench(Rml::ElementDocument* doc, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    if (!find_el(doc, "item_surface_details")) { return; }
    const auto surface = state.object_workbench_surface;
    struct ItemSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        ItemSurfaceElements{"item_tab_details", "item_surface_details",
            ObjectWorkbenchSurface::details},
        ItemSurfaceElements{"", "object_surface_locstring",
            ObjectWorkbenchSurface::locstring},
        ItemSurfaceElements{"item_tab_variables", "item_surface_variables",
            ObjectWorkbenchSurface::variables},
        ItemSurfaceElements{"item_tab_appearance", "item_surface_appearance",
            ObjectWorkbenchSurface::appearance},
        ItemSurfaceElements{"item_tab_item_properties", "item_surface_item_properties",
            ObjectWorkbenchSurface::item_properties},
        ItemSurfaceElements{"item_tab_inventory", "item_surface_inventory",
            ObjectWorkbenchSurface::inventory},
    };
    for (const auto& elements : surfaces) {
        if (elements.tab_id[0] != '\0') {
            if (auto* tab = find_el(doc, elements.tab_id)) {
                tab->SetClass("active", surface == elements.surface);
            }
        }
        if (auto* element = find_el(doc, elements.surface_id)) {
            element->SetClass("active", surface == elements.surface);
        }
    }

    const auto* active_tab = workspace.active_tab();
    const bool show_header = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type == nw::ObjectType::item;
    if (auto* header = find_el(doc, "item_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "item_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }
}

void hydrate_door_workbench(Rml::ElementDocument* doc, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    if (!find_el(doc, "door_surface_details")) {
        return;
    }

    struct DoorSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        DoorSurfaceElements{"door_tab_details", "door_surface_details",
            ObjectWorkbenchSurface::details},
        DoorSurfaceElements{"", "object_surface_locstring",
            ObjectWorkbenchSurface::locstring},
        DoorSurfaceElements{"door_tab_variables", "door_surface_variables",
            ObjectWorkbenchSurface::variables},
        DoorSurfaceElements{"door_tab_appearance", "door_surface_appearance",
            ObjectWorkbenchSurface::appearance},
    };
    for (const auto& elements : surfaces) {
        if (elements.tab_id[0] != '\0') {
            if (auto* tab = find_el(doc, elements.tab_id)) {
                tab->SetClass("active", state.object_workbench_surface == elements.surface);
            }
        }
        if (auto* surface = find_el(doc, elements.surface_id)) {
            surface->SetClass("active", state.object_workbench_surface == elements.surface);
        }
    }

    const auto* active_tab = workspace.active_tab();
    const bool show_header = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type == nw::ObjectType::door;
    if (auto* header = find_el(doc, "door_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "door_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }

    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance) {
        std::string markup;
        if (state.appearance_view.appearance_selector_open) {
            append_appearance_selector_markup(markup, state.appearance_view);
        } else {
            append_door_appearance_markup(markup, state.appearance_view);
        }
        if (auto* target = find_el(doc, "door_appearance_dynamic")) {
            target->SetInnerRML(markup);
        }
    }
}

void hydrate_placeable_workbench(Rml::ElementDocument* doc, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    if (!find_el(doc, "placeable_surface_details")) {
        return;
    }

    struct PlaceableSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        PlaceableSurfaceElements{"placeable_tab_details", "placeable_surface_details",
            ObjectWorkbenchSurface::details},
        PlaceableSurfaceElements{"", "object_surface_locstring",
            ObjectWorkbenchSurface::locstring},
        PlaceableSurfaceElements{"placeable_tab_variables", "placeable_surface_variables",
            ObjectWorkbenchSurface::variables},
        PlaceableSurfaceElements{"placeable_tab_appearance", "placeable_surface_appearance",
            ObjectWorkbenchSurface::appearance},
        PlaceableSurfaceElements{"placeable_tab_inventory", "placeable_surface_inventory",
            ObjectWorkbenchSurface::inventory},
    };
    for (const auto& elements : surfaces) {
        if (elements.tab_id[0] != '\0') {
            if (auto* tab = find_el(doc, elements.tab_id)) {
                tab->SetClass("active", state.object_workbench_surface == elements.surface);
            }
        }
        if (auto* surface = find_el(doc, elements.surface_id)) {
            surface->SetClass("active", state.object_workbench_surface == elements.surface);
        }
    }

    const auto* active_tab = workspace.active_tab();
    const bool show_header = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type == nw::ObjectType::placeable;
    if (auto* header = find_el(doc, "placeable_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "placeable_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }

    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance) {
        std::string markup;
        if (state.appearance_view.appearance_selector_open) {
            append_appearance_selector_markup(markup, state.appearance_view);
        } else {
            append_placeable_appearance_markup(markup, state.appearance_view);
        }
        if (auto* target = find_el(doc, "placeable_appearance_dynamic")) {
            target->SetInnerRML(markup);
        }
    } else if (state.object_workbench_surface == ObjectWorkbenchSurface::inventory) {
        std::string markup;
        append_creature_inventory_markup(markup, state.inventory_view, object_workbench_target(state, workspace));
        if (auto* target = find_el(doc, "placeable_inventory_dynamic")) {
            target->SetInnerRML(markup);
        }
    }
}

} // namespace

void hide_object_variable_warning_tooltip(
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state)
{
    if (state.active_object_variable_warning.empty()) {
        return;
    }
    state.active_object_variable_warning.clear();
    if (auto* tooltip = find_el(doc, "object_variable_warning_tooltip")) {
        tooltip->SetProperty("display", "none");
    }
}

// One window has one pointer and one transient warning tooltip. This is a true
// UI singleton; there is no batch of simultaneous hover targets to process.
void sync_object_variable_warning_tooltip(Rml::ElementDocument* doc,
    ObjectWorkbenchViewState& state, bool palette_visible,
    Rml::Element* hit,
    Rml::Vector2f point,
    int viewport_width,
    int viewport_height)
{
    auto* warning = state.object_workbench_surface == ObjectWorkbenchSurface::variables
            && !palette_visible
        ? find_ancestor_with_class(hit, "object_variable_field_warning")
        : nullptr;
    const std::string description = warning
        ? warning->GetAttribute<Rml::String>("data-tooltip", "")
        : std::string{};
    if (description.empty() || viewport_width <= 16 || viewport_height <= 16) {
        hide_object_variable_warning_tooltip(doc, state);
        return;
    }

    auto* tooltip = find_el(doc, "object_variable_warning_tooltip");
    if (!tooltip) {
        state.active_object_variable_warning.clear();
        return;
    }
    if (description != state.active_object_variable_warning) {
        state.active_object_variable_warning = description;
        tooltip->SetInnerRML(escape_html(description));
    }

    constexpr int margin = 8;
    constexpr int pointer_offset = 14;
    constexpr int preferred_width = 280;
    constexpr int estimated_height = 54;
    const int width = std::min(preferred_width, viewport_width - 2 * margin);
    const int max_left = std::max(margin, viewport_width - width - margin);
    const int left = std::clamp(
        static_cast<int>(std::lround(point.x)) + pointer_offset,
        margin,
        max_left);
    int top = static_cast<int>(std::lround(point.y)) + pointer_offset;
    if (top + estimated_height > viewport_height - margin) {
        top = static_cast<int>(std::lround(point.y))
            - estimated_height - pointer_offset;
    }
    top = std::clamp(top, margin,
        std::max(margin, viewport_height - estimated_height - margin));

    tooltip->SetProperty("display", "block");
    tooltip->SetProperty("width", std::to_string(width) + "px");
    tooltip->SetProperty("left", std::to_string(left) + "px");
    tooltip->SetProperty("top", std::to_string(top) + "px");
}

bool active_tab_has_object_workbench(const nw::toolset::WorkspaceTab* active_tab)
{
    return active_tab
        && (active_tab->kind == nw::toolset::WorkspaceTabKind::preview
            || active_tab->kind == nw::toolset::WorkspaceTabKind::area
            || active_tab->kind == nw::toolset::WorkspaceTabKind::home);
}

ObjectWorkbenchTarget object_workbench_target(const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    const auto* tab = workspace.active_tab();
    return {.object = state.object_details.object,
        .surface = state.object_workbench_surface,
        .matches_active_tab = active_tab_has_object_workbench(tab) && state.active_object_tab_id == tab->id,
        .area_tab = tab && tab->kind == WorkspaceTabKind::area,
        .details_ready = state.object_details.status == ObjectDetailsStatus::ready};
}

bool active_object_details_matches_tab(const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    const auto* active_tab = workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.object_details.status == nw::toolset::ObjectDetailsStatus::ready;
}

void refresh_object_workbench_queries(Rml::ElementDocument* doc, ObjectWorkbenchViewState& state,
    const WorkspaceState& workspace, const ToolsetBackend& backend, uint64_t resource_generation)
{
    const auto query_value = [&](const char* id) {
        auto* input = find_el(doc, id);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            return control->GetValue();
        }
        return input ? input->GetAttribute<Rml::String>("value", "") : Rml::String{};
    };
    if (state.object_workbench_surface == ObjectWorkbenchSurface::feats) {
        const auto query = query_value("creature_feat_search");
        if (query != state.creature_view.creature_feat_query) {
            state.creature_view.creature_feat_query = query;
            if (state.object_details.object.type == ObjectType::creature
                && active_object_details_matches_tab(state, workspace)) {
                rebuild_active_creature_feats(state.creature_view, state.object_details.object);
                state.creature_view.creature_feat_list.set_scroll_top(0);
                (void)sync_creature_feat_window(doc, state.creature_view, object_workbench_target(state, workspace), true);
            }
        }
    }
    if (state.object_workbench_surface == ObjectWorkbenchSurface::spells) {
        const auto query = query_value("creature_spell_search");
        if (query != state.creature_view.creature_spell_query) {
            state.creature_view.creature_spell_query = query;
            if (state.object_details.object.type == ObjectType::creature
                && active_object_details_matches_tab(state, workspace)) {
                filter_active_creature_spells(state.creature_view);
                state.creature_view.creature_spell_list.set_scroll_top(0);
                (void)sync_creature_spell_window(doc, state.creature_view, object_workbench_target(state, workspace), true);
            }
        }
    }
    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance
        && state.appearance_view.appearance_selector_open) {
        const auto query = query_value("appearance_search");
        if (query != state.appearance_view.appearance_query) {
            state.appearance_view.appearance_query = query;
            if (appearance_catalog_kind(state.object_details.object.type)
                && active_object_details_matches_tab(state, workspace)) {
                rebuild_active_appearances(state.appearance_view, backend.module_generation(), state.object_details.object);
                state.appearance_view.appearance_list.set_scroll_top(0);
                (void)sync_appearance_window(doc, state.appearance_view, object_workbench_target(state, workspace), true);
            }
        }
    }
    if (active_sound_resource_selector_matches_tab(state.appearance_view, object_workbench_target(state, workspace))) {
        const auto query = query_value("sound_catalog_search");
        if (query != state.appearance_view.sound_catalog_query) {
            state.appearance_view.sound_catalog_query = query;
            rebuild_sound_catalog(state.appearance_view, resource_generation, true);
            state.appearance_view.sound_catalog_list.set_scroll_top(0);
            (void)sync_sound_catalog_window(doc, state.appearance_view, object_workbench_target(state, workspace), resource_generation, true);
        }
    }
}

bool active_object_matches_tab(const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    const auto* active_tab = workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type != nw::ObjectType::invalid;
}

size_t active_details_row_count(const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    return active_object_details_matches_tab(state, workspace) ? state.object_details.rows.size() : 0;
}

bool active_object_variables_match_tab(const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    const auto* active_tab = workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.object_variables.object == state.object_details.object
        && state.object_variables.status
        == nw::toolset::ObjectVariableSnapshotStatus::ready;
}

void configure_object_variable_list(ObjectWorkbenchViewState& state)
{
    if (state.object_variable_list_configured) {
        return;
    }
    state.object_variable_list.set_row_height(kObjectVariableRowHeightPx);
    state.object_variable_list.set_overscan(kObjectVariableOverscanRows);
    state.object_variable_list_configured = true;
}

void invalidate_object_variable_render(ObjectWorkbenchViewState& state)
{
    configure_object_variable_list(state);
    const size_t row_count = state.object_variables.status
            == nw::toolset::ObjectVariableSnapshotStatus::ready
        ? state.object_variables.rows.size()
        : 0;
    state.object_variable_list.set_total_rows(static_cast<int>(row_count));
    state.object_variables_rendered = false;
}

void configure_details_list(ObjectWorkbenchViewState& state)
{
    if (state.details_list_configured) {
        return;
    }
    state.details_list.set_row_height(kObjectDetailsRowHeightPx);
    state.details_list.set_overscan(kObjectDetailsOverscanRows);
    state.details_list_configured = true;
}

void invalidate_details_render(ObjectWorkbenchViewState& state)
{
    configure_details_list(state);
    const auto* snapshot = &state.object_details;
    const size_t row_count = snapshot->status == nw::toolset::ObjectDetailsStatus::ready
        ? snapshot->rows.size()
        : 0;
    state.details_list.set_total_rows(static_cast<int>(row_count));
    state.details_rendered = false;
}

void clear_object_details_combobox_state(ObjectWorkbenchViewState& state)
{
    state.object_details_combobox.close();
    state.object_details_combobox_row.reset();
    state.object_details_combobox_placement.reset();
}

void close_object_details_combobox(
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state)
{
    if (state.object_details_combobox_row) {
        const auto row_index = *state.object_details_combobox_row;
        const bool locstring = row_index < state.object_details.rows.size()
            && state.object_details.rows[row_index].editor == ObjectDetailsEditorKind::locstring;
        const auto field_id = locstring ? std::string{"object_locstring_language"}
                                        : "object_details_sound_position_field_" + std::to_string(row_index);
        if (auto* field = find_el(doc, field_id.c_str())) {
            field->SetClass("open", false);
        }
    }
    clear_object_details_combobox_state(state);
    if (auto* popup = find_el(doc, "object_details_combobox_popup")) {
        popup->SetClass("active", false);
        popup->SetInnerRML("");
    }
}

bool open_object_details_sound_position_combobox(
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace, uint32_t row_index)
{
    if (!active_object_details_matches_tab(state, workspace)
        || row_index >= state.object_details.rows.size()) {
        return false;
    }
    const auto& row = state.object_details.rows[row_index];
    if (row.kind != nw::toolset::ObjectDetailsRowKind::value
        || row.editor != nw::toolset::ObjectDetailsEditorKind::sound_position
        || row.edit_value < 0 || row.edit_value > 2) {
        return false;
    }
    if (state.object_details_combobox_row == row_index
        && state.object_details_combobox.is_active()) {
        if (state.object_details_combobox.popup_visible()) {
            state.object_details_combobox.hide_popup();
        } else {
            (void)state.object_details_combobox.show_popup();
        }
        state.object_details_combobox_placement.reset();
        return true;
    }

    std::vector<nw::toolset::VirtualComboBoxItem> options{
        {.key = 0, .label = "Everywhere"},
        {.key = 1, .label = "Positional"},
        {.key = 2, .label = "Random Position"},
    };
    close_object_details_combobox(doc, state);
    if (!state.object_details_combobox.open(
            std::move(options), row.edit_value)) {
        return false;
    }
    state.object_details_combobox_row = row_index;
    return true;
}

bool open_object_details_locstring_source_combobox(
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state,
    const WorkspaceState& workspace, uint32_t row_index)
{
    if (state.object_workbench_surface != ObjectWorkbenchSurface::locstring
        || !active_object_details_matches_tab(state, workspace)
        || !state.locstring_row || *state.locstring_row != row_index
        || row_index >= state.object_details.rows.size()
        || state.object_details.rows[row_index].editor != ObjectDetailsEditorKind::locstring) {
        return false;
    }
    if (state.object_details_combobox_row == row_index
        && state.object_details_combobox.is_active()) {
        if (state.object_details_combobox.popup_visible()) {
            state.object_details_combobox.hide_popup();
        } else {
            (void)state.object_details_combobox.show_popup();
        }
        state.object_details_combobox_placement.reset();
        return true;
    }

    close_object_details_combobox(doc, state);
    if (!state.object_details_combobox.open(locstring_source_options(),
            locstring_source_key(state.locstring_language))) { return false; }
    state.object_details_combobox_row = row_index;
    return true;
}

bool sync_object_details_combobox(
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace, bool force)
{
    if (!state.object_details_combobox.is_active()
        || !state.object_details_combobox_row) {
        return false;
    }
    const auto row_index = *state.object_details_combobox_row;
    if (!active_object_details_matches_tab(state, workspace)
        || row_index >= state.object_details.rows.size()) {
        close_object_details_combobox(doc, state);
        return true;
    }
    const auto& row = state.object_details.rows[row_index];
    const auto selected = state.object_details_combobox.selected_key();
    std::optional<LanguageID> source_language;
    const bool sound = row.editor == ObjectDetailsEditorKind::sound_position
        && state.object_workbench_surface == ObjectWorkbenchSurface::details
        && selected && *selected >= 0 && *selected <= 2;
    const bool locstring = row.editor == ObjectDetailsEditorKind::locstring
        && state.object_workbench_surface == ObjectWorkbenchSurface::locstring
        && state.locstring_row && *state.locstring_row == row_index
        && selected && locstring_source_language(*selected, source_language);
    if (!sound && !locstring) {
        close_object_details_combobox(doc, state);
        return true;
    }

    const auto field_id = locstring ? std::string{"object_locstring_language"}
                                    : "object_details_sound_position_field_" + std::to_string(row_index);
    auto* field = find_el(doc, field_id.c_str());
    auto* popup = find_el(doc, "object_details_combobox_popup");
    auto* workbench = find_el(doc, "object_workbench");
    if (!field || !popup || !workbench) {
        close_object_details_combobox(doc, state);
        return true;
    }

    const bool visible = state.object_details_combobox.popup_visible();
    field->SetClass("open", visible);
    popup->SetClass("active", visible);
    if (!visible) {
        return false;
    }

    const nw::toolset::VirtualComboBoxRect anchor{
        .x = static_cast<int>(std::lround(
            field->GetAbsoluteLeft() - workbench->GetAbsoluteLeft())),
        .y = static_cast<int>(std::lround(
            field->GetAbsoluteTop() - workbench->GetAbsoluteTop())),
        .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
        .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
    };
    const nw::toolset::VirtualComboBoxRect bounds{
        .width = static_cast<int>(std::lround(workbench->GetClientWidth())),
        .height = static_cast<int>(std::lround(workbench->GetClientHeight())),
    };
    const auto placement = state.object_details_combobox.place_popup(
        anchor, bounds);
    if (placement.width <= 0 || placement.height <= 0) {
        return false;
    }

    bool changed = false;
    if (!state.object_details_combobox_placement
        || *state.object_details_combobox_placement != placement) {
        popup->SetProperty("left", std::to_string(placement.left) + "px");
        popup->SetProperty("top", std::to_string(placement.top) + "px");
        popup->SetProperty("width", std::to_string(placement.width) + "px");
        popup->SetProperty("height", std::to_string(placement.height) + "px");
        state.object_details_combobox_placement = placement;
        changed = true;
    }

    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(popup->GetScrollTop())));
    auto update = state.object_details_combobox.update(
        placement.height, observed_scroll_top, force);
    if (update.replace_markup) {
        popup->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        popup->SetScrollTop(static_cast<float>(update.scroll_top));
    }
    return changed || update.replace_markup || update.set_scroll;
}

bool sync_object_variable_window(Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace, bool force)
{
    if (state.object_workbench_surface != ObjectWorkbenchSurface::variables) {
        return false;
    }
    auto* list = find_el(doc, "object_variable_rows");
    if (!list) {
        return false;
    }

    configure_object_variable_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(
            std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(
        0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.object_variable_list.set_viewport_height(viewport_height);
    state.object_variable_list.set_scroll_top(scroll_top);
    const auto range = state.object_variable_list.compute_range();
    const int row_count = active_object_variables_match_tab(state, workspace)
        ? static_cast<int>(state.object_variables.rows.size())
        : 0;
    if (!force && state.object_variables_rendered
        && row_count == state.rendered_object_variable_row_count
        && range.start == state.rendered_object_variable_range.start
        && range.end == state.rendered_object_variable_range.end) {
        return false;
    }

    std::string markup;
    if (active_object_matches_tab(state, workspace)
        && state.object_variables.status
            != nw::toolset::ObjectVariableSnapshotStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.object_variables.diagnostic.empty()
                ? std::string_view{"Object variable data is unavailable."}
                : std::string_view{state.object_variables.diagnostic});
        markup += "</div>";
    } else if (!active_object_variables_match_tab(state, workspace)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live object.</div>";
    } else if (state.object_variables.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">No variables.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            state.object_variable_list,
            ObjectVariableListAdapter{state.object_variables});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "object_variable_count")) {
        count->SetInnerRML(std::to_string(row_count));
    }
    state.rendered_object_variable_range = range;
    state.rendered_object_variable_row_count = row_count;
    state.object_variables_rendered = true;
    return true;
}

bool sync_object_locstring_panel(Rml::ElementDocument* doc,
    ObjectWorkbenchViewState& state, const WorkspaceState& workspace, bool force)
{
    auto* rows = find_el(doc, "object_locstring_rows");
    if (!rows || state.object_workbench_surface != ObjectWorkbenchSurface::locstring) {
        return false;
    }
    if (!force && state.locstring_panel_rendered) { return false; }

    const auto row_index = state.locstring_row;
    std::optional<LocString> current;
    if (active_object_details_matches_tab(state, workspace) && row_index
        && *row_index < state.object_details.rows.size()) {
        const auto& row = state.object_details.rows[*row_index];
        if (row.editor == ObjectDetailsEditorKind::locstring) {
            current = read_object_locstring(kernel::runtime(),
                {state.object_details.object, row.locstring_storage,
                    row.propset_type, row.field_index});
            if (auto* title = find_el(doc, "object_locstring_title")) {
                title->SetInnerRML(escape_html(state.object_details.text_view(row.label)));
            }
        }
    }
    auto* unavailable = find_el(doc, "object_locstring_unavailable");
    auto* editor = find_el(doc, "object_locstring_editor");
    const bool available = current.has_value() && row_index.has_value();
    if (unavailable) { unavailable->SetClass("active", !available); }
    if (editor) { editor->SetClass("active", available); }
    if (!available) {
        state.locstring_text_before.clear();
        state.locstring_panel_rendered = true;
        return true;
    }

    const auto row_value = std::to_string(*row_index);
    auto* source = find_el(doc, "object_locstring_language");
    if (source) { source->SetAttribute("data-row", row_value); }
    if (auto* value = find_el(doc, "object_locstring_language_value")) {
        value->SetInnerRML(state.locstring_language
                ? escape_html(Language::to_string(*state.locstring_language, true))
                : "String Reference");
    }

    auto* strref_editor = find_el(doc, "object_locstring_strref_editor");
    auto* text_editor = find_el(doc, "object_locstring_text_editor");
    const bool editing_strref = !state.locstring_language;
    if (strref_editor) { strref_editor->SetClass("active", editing_strref); }
    if (text_editor) { text_editor->SetClass("active", !editing_strref); }

    if (editing_strref) {
        const auto strref = current->strref();
        if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(
                find_el(doc, "object_locstring_strref"))) {
            const auto displayed = strref == UINT32_MAX
                ? std::string{}
                : std::to_string(strref);
            input->SetAttribute("data-row", row_value);
            input->SetAttribute("data-current", std::to_string(strref));
            input->SetAttribute("data-last-valid", displayed);
            input->SetValue(displayed);
        }
        if (auto* preview = find_el(doc, "object_locstring_resolved")) {
            preview->SetInnerRML(locstring_strref_preview_markup(strref));
        }
    } else {
        const auto language = *state.locstring_language;
        if (auto* gender = find_el(doc, "object_locstring_gender")) {
            gender->SetClass("hidden", !Language::has_feminine(language));
        }
        if (auto* toggle = find_el(doc, "object_locstring_gender_toggle")) {
            toggle->SetClass("active", state.locstring_feminine);
            toggle->SetInnerRML(state.locstring_feminine ? "Feminine" : "Masculine");
        }
        state.locstring_text_before = current->get(language, state.locstring_feminine);
        if (auto* text = rmlui_dynamic_cast<Rml::ElementFormControl*>(
                find_el(doc, "object_locstring_text"))) {
            text->SetAttribute("data-row", row_value);
            text->SetAttribute("data-runtime",
                std::to_string(Language::to_runtime_id(language,
                    state.locstring_feminine)));
            text->SetValue(state.locstring_text_before);
        }
    }
    state.locstring_panel_rendered = true;
    return true;
}

bool sync_object_details_window(Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace, bool force)
{
    const bool variable_changed = sync_object_variable_window(doc, state, workspace, force);
    if (state.object_workbench_surface == ObjectWorkbenchSurface::locstring) {
        const bool panel_changed = sync_object_locstring_panel(doc, state, workspace, force);
        const bool combobox_changed = sync_object_details_combobox(doc, state, workspace, force);
        return variable_changed || panel_changed || combobox_changed;
    }
    if (state.object_workbench_surface != ObjectWorkbenchSurface::details) {
        const bool combobox_changed = state.object_details_combobox.is_active();
        if (combobox_changed) {
            close_object_details_combobox(doc, state);
        }
        return variable_changed || combobox_changed;
    }
    auto* list = find_el(doc, "property_tree_rows");
    if (!list) {
        return false;
    }

    configure_details_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.details_list.set_viewport_height(viewport_height);
    state.details_list.set_scroll_top(scroll_top);
    const auto range = state.details_list.compute_range();
    const int row_count = static_cast<int>(active_details_row_count(state, workspace));

    if (!force && state.details_rendered
        && row_count == state.rendered_details_row_count
        && range.start == state.rendered_details_range.start
        && range.end == state.rendered_details_range.end) {
        return variable_changed
            || sync_object_details_combobox(doc, state, workspace, false);
    }

    const auto& snapshot = state.object_details;
    std::string markup;
    if (active_object_matches_tab(state, workspace)
        && snapshot.status != nw::toolset::ObjectDetailsStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(snapshot.diagnostic.empty()
                ? std::string_view{"Live object Details are unavailable."}
                : std::string_view{snapshot.diagnostic});
        markup += "</div>";
    } else if (!active_object_details_matches_tab(state, workspace)) {
        markup = "<div class=\"property_tree_empty\">Waiting for the live preview object.</div>";
    } else if (snapshot.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">No Details available.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            state.details_list,
            ObjectDetailsListAdapter{snapshot});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "property_tree_count")) {
        count->SetInnerRML(std::to_string(active_details_row_count(state, workspace)));
    }
    state.rendered_details_range = range;
    state.rendered_details_row_count = row_count;
    state.details_rendered = true;
    (void)sync_object_details_combobox(doc, state, workspace, true);
    return true;
}

void rebuild_object_workbench_snapshots(ObjectWorkbenchViewState& state, nw::ObjectHandle object)
{
    if (state.pending_sound_volume
        && (state.pending_sound_volume->object != object
            || state.pending_sound_volume->tab_id != state.active_object_tab_id)) {
        state.pending_sound_volume.reset();
    }
    auto& runtime = nw::kernel::runtime();
    nw::toolset::build_object_details(runtime, object, state.object_details,
        state.toolset_language);
    if (state.object_details.status == nw::toolset::ObjectDetailsStatus::invalid_data) {
        LOG_F(WARNING, "rollnw-client: %s", state.object_details.diagnostic.c_str());
    }
    nw::toolset::snapshot_object_variables(object, state.object_variables);
    if (state.object_variables.status
        == nw::toolset::ObjectVariableSnapshotStatus::invalid_data) {
        LOG_F(WARNING, "rollnw-client: %s",
            state.object_variables.diagnostic.c_str());
    }
    invalidate_details_render(state);
    state.locstring_panel_rendered = false;
    invalidate_object_variable_render(state);
    rebuild_creature_class_presentation(state.creature_view, object);
}

void clear_object_workbench_snapshots(ObjectWorkbenchViewState& state)
{
    state.pending_sound_volume.reset();
    clear_object_details_combobox_state(state);
    state.object_details = {};
    state.object_variables = {};
    configure_details_list(state);
    state.details_list.set_total_rows(0);
    state.details_list.set_scroll_top(0);
    state.details_rendered = false;
    state.locstring_panel_rendered = false;
    state.locstring_row.reset();
    configure_object_variable_list(state);
    state.object_variable_list.set_total_rows(0);
    state.object_variable_list.set_scroll_top(0);
    state.object_variables_rendered = false;
    state.object_workbench_surface = ObjectWorkbenchSurface::details;
}

bool commit_object_details_sound_position(
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int32_t desired)
{
    if (!state.object_details_combobox_row
        || !state.object_details_combobox.select_key(desired)) {
        return false;
    }
    const auto row_index = *state.object_details_combobox_row;
    if (!active_object_details_matches_tab(state, workspace)
        || row_index >= state.object_details.rows.size()) {
        close_object_details_combobox(doc, state);
        return false;
    }
    const auto& row = state.object_details.rows[row_index];
    if (row.editor != nw::toolset::ObjectDetailsEditorKind::sound_position
        || row.edit_value < 0 || row.edit_value > 2
        || desired < 0 || desired > 2) {
        close_object_details_combobox(doc, state);
        return false;
    }
    if (desired == row.edit_value) {
        close_object_details_combobox(doc, state);
        return true;
    }

    const auto result = backend.execute_command(
        "object.details.set_sound_position",
        {std::to_string(row_index), std::to_string(row.edit_value),
            std::to_string(desired)},
        context);
    append_command_results(shell, {&result, 1});
    close_object_details_combobox(doc, state);
    return result.ok();
}

bool commit_object_details_locstring_source(Rml::ElementDocument* doc,
    ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context,
    int32_t desired)
{
    if (!state.object_details_combobox_row
        || !state.object_details_combobox.is_active()
        || !active_object_details_matches_tab(state, workspace)
        || *state.object_details_combobox_row >= state.object_details.rows.size()
        || state.object_details.rows[*state.object_details_combobox_row].editor
            != ObjectDetailsEditorKind::locstring) { return false; }
    auto* field = find_el(doc, "object_locstring_language");
    if (!field || !ObjectWorkbenchChangeHandler{state, workspace, backend, shell, context}.select_locstring_source(field, desired)) { return false; }
    close_object_details_combobox(doc, state);
    return true;
}

void process_object_workbench_change(Rml::Event& event, ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context)
{
    ObjectWorkbenchChangeHandler{state, workspace, backend, shell, context}.process_event(event);
}
bool commit_object_workbench_sound_volume(ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context)
{
    return ObjectWorkbenchChangeHandler{state, workspace, backend, shell, context}.commit_sound_volume();
}

void hydrate_object_workbench(Rml::ElementDocument* doc, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    switch (state.object_details.object.type) {
    case ObjectType::creature:
        hydrate_creature_workbench(doc, state, workspace);
        break;
    case ObjectType::item:
        hydrate_item_workbench(doc, state, workspace);
        break;
    case ObjectType::door:
        hydrate_door_workbench(doc, state, workspace);
        break;
    case ObjectType::placeable:
        hydrate_placeable_workbench(doc, state, workspace);
        break;
    default:
        break;
    }
    if (auto* surface = find_el(doc, "object_surface_locstring")) {
        surface->SetClass("active",
            state.object_workbench_surface == ObjectWorkbenchSurface::locstring);
    }
}
void append_placed_area_object_list_markup(std::string& content_markup,
    std::span<const PlacedAreaObjectRow> rows, bool area_available)
{
    content_markup += "<div id=\"object_workbench\" class=\"object_workbench area_object_list_workbench\">";
    content_markup += "<div class=\"object_workbench_header area_object_list_header\">";
    content_markup += "<div class=\"object_workbench_title\">Placed Objects</div>";
    content_markup += "<span class=\"area_object_list_count\">";
    content_markup += std::to_string(rows.size());
    content_markup += "</span></div><div class=\"area_object_list\">";
    if (!area_available) {
        content_markup += "<div class=\"property_tree_empty\">Loading placed objects...</div>";
    } else if (rows.empty()) {
        content_markup += "<div class=\"property_tree_empty\">This area has no placed objects.</div>";
    } else {
        for (const auto& row : rows) {
            content_markup += "<button type=\"button\" class=\"area_object_row\" data-object=\"";
            content_markup += std::to_string(row.object.to_ull());
            content_markup += "\"><span class=\"area_object_row_name\">";
            content_markup += escape_html(row.name);
            content_markup += "</span><span class=\"area_object_row_type\">";
            content_markup += escape_html(
                nw::toolset::placed_area_object_type_label(row.object.type));
            content_markup += "</span></button>";
        }
    }
    content_markup += "</div></div>";
}

std::optional<PlacedAreaObjectClick> capture_placed_area_object_click(Rml::Element* hit)
{
    if (auto* row = find_ancestor_with_class(hit, "area_object_row")) {
        const auto value = row->GetAttribute<Rml::String>("data-object", "");
        uint64_t packed = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), packed);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
            return PlacedAreaObjectClick{};
        }
        const auto object = ObjectHandle::from_ull(packed);
        if (!kernel::objects().valid(object)) { return PlacedAreaObjectClick{}; }
        return PlacedAreaObjectClick{object, PlacedAreaObjectClickKind::select};
    }
    if (find_ancestor_with_class(hit, "area_object_list_back")) {
        return PlacedAreaObjectClick{{}, PlacedAreaObjectClickKind::back};
    }
    return std::nullopt;
}

void append_object_workbench_markup(std::string& content_markup, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace, const ToolsetBackend& backend)
{
    const auto* active_tab = workspace.active_tab();
    const bool area_tab = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area;
    if (state.object_details.object.type == nw::ObjectType::creature) {
        content_markup += "<template src=\"creature-workbench\"></template>";
        return;
    }
    if (state.object_details.object.type == nw::ObjectType::item) {
        content_markup += "<template src=\"item-workbench\"></template>";
        return;
    }
    if (state.object_details.object.type == nw::ObjectType::door) {
        content_markup += "<template src=\"door-workbench\"></template>";
        return;
    }
    if (state.object_details.object.type == nw::ObjectType::placeable) {
        content_markup += "<template src=\"placeable-workbench\"></template>";
        return;
    }
    const auto object_type = state.object_details.object.type;
    const bool project_module = object_type == nw::ObjectType::module
        && !backend.current_project_dir().empty();
    content_markup += "<div id=\"object_workbench\" class=\"object_workbench\">";
    if (area_tab && active_object_matches_tab(state, workspace)
        && state.object_details.object.type != nw::ObjectType::area) {
        const std::string object_name = nw::toolset::live_object_display_name(
            state.object_details.object);
        content_markup += "<div class=\"object_workbench_header area_object_header\">";
        content_markup += "<button type=\"button\" "
                          "class=\"area_object_list_back panel_back_button\" "
                          "title=\"Back to placed objects\">"
                          "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                          "<span class=\"panel_back_shaft\"></span></span></button>";
        content_markup += "<div class=\"object_workbench_title\">";
        content_markup += escape_html(object_name);
        content_markup += "</div></div>";
    }
    content_markup += "<div id=\"object_workbench_tab_bar\" class=\"object_workbench_tab_bar\">"
                      "<div id=\"object_workbench_tabs\" class=\"object_workbench_tabs\">"
                      "<div id=\"object_workbench_tab_track\" class=\"object_workbench_tab_track\">";
    content_markup += "<div class=\"object_workbench_tab";
    if (state.object_workbench_surface == ObjectWorkbenchSurface::details) {
        content_markup += " active";
    }
    content_markup += "\" data-surface=\"details\">Details</div>";
    content_markup += "<div class=\"object_workbench_tab";
    if (state.object_workbench_surface == ObjectWorkbenchSurface::variables) {
        content_markup += " active";
    }
    content_markup += "\" data-surface=\"variables\">Variables</div>";
    if (project_module) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::haks) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"haks\">Haks</div>";
    }
    if (object_type == nw::ObjectType::encounter) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::spawns) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"spawns\">Spawns</div>";
    } else if (object_type == nw::ObjectType::sound) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::sounds) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"sounds\">Sounds</div>";
    } else if (object_type == nw::ObjectType::store) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::store_inventory) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"store-inventory\">Inventory</div>";
    }
    content_markup += "</div></div>"
                      "<button id=\"object_workbench_tabs_previous\" "
                      "class=\"object_workbench_tab_scroll_button disabled\" "
                      "type=\"button\" title=\"Scroll editor tabs left\">&#x2039;</button>"
                      "<button id=\"object_workbench_tabs_next\" "
                      "class=\"object_workbench_tab_scroll_button disabled\" "
                      "type=\"button\" title=\"Scroll editor tabs right\">&#x203a;</button>"
                      "</div>";

    if (state.object_workbench_surface == ObjectWorkbenchSurface::locstring) {
        content_markup += "<template src=\"object-locstring-editor\"></template>";
    } else if (state.object_workbench_surface == ObjectWorkbenchSurface::variables) {
        content_markup += "<div class=\"object_variable_toolbar\"><button id=\"object_variable_add\" "
                          "type=\"button\" title=\"Add integer variable\">Add Variable</button></div>"
                          "<div class=\"object_variable_header\"><span class=\"object_variable_header_name\">Name</span>"
                          "<span class=\"object_variable_header_type\">Type</span>"
                          "<span class=\"object_variable_header_value\">Value</span>"
                          "<span id=\"object_variable_count\" class=\"property_tree_count\">";
        content_markup += active_object_variables_match_tab(state, workspace)
            ? std::to_string(state.object_variables.rows.size())
            : std::string{"0"};
        content_markup += "</span></div><div id=\"object_variable_rows\" "
                          "class=\"object_variable_rows\"><div class=\"property_tree_empty\">"
                          "Waiting for a live object.</div></div>";
    } else if (project_module && state.object_workbench_surface == ObjectWorkbenchSurface::haks) {
        const auto module_summary = backend.project_module_summary();
        content_markup += "<div class=\"module_hak_list\">";
        if (!module_summary.ok) {
            content_markup += "<div class=\"module_hak_empty\">";
            content_markup += escape_html(module_summary.message.empty()
                    ? std::string{"Module metadata unavailable."}
                    : module_summary.message);
            content_markup += "</div>";
        } else if (module_summary.haks.empty()) {
            content_markup += "<div class=\"module_hak_empty\">No module haks.</div>";
        } else {
            for (const auto& hak : module_summary.haks) {
                content_markup += "<div class=\"module_hak_item\"><span class=\"module_hak_name\">";
                content_markup += escape_html(hak);
                content_markup += "</span></div>";
            }
        }
        content_markup += "</div>";
    } else if (object_type == nw::ObjectType::encounter
        && state.object_workbench_surface == ObjectWorkbenchSurface::spawns) {
        content_markup += "<div id=\"encounter_spawn_collection\" "
                          "class=\"data_object_collection smalls_refresh\" "
                          "onrefresh=\"encounter_spawns_refresh()\">"
                          "<div class=\"data_collection_header encounter_spawn_header\">"
                          "<span>Creature</span><span>CR</span><span>Appearance</span><span>Single</span>"
                          "</div><div class=\"data_collection_summary managed_list_title\" "
                          "data-list-id=\"data.encounter.spawns\"></div>"
                          "<div class=\"data_collection_rows encounter_spawn_rows managed_list_rows\" "
                          "tabindex=\"0\" "
                          "data-list-id=\"data.encounter.spawns\" "
                          "data-empty-text=\"This encounter has no spawn entries.\"></div></div>";
    } else if (object_type == nw::ObjectType::sound
        && state.object_workbench_surface == ObjectWorkbenchSurface::sounds) {
        if (state.appearance_view.sound_resource_selector_open) {
            append_sound_resource_selector_markup(content_markup, state.appearance_view);
        } else {
            content_markup += "<div id=\"sound_resource_collection\" "
                              "class=\"data_object_collection smalls_refresh\" "
                              "onrefresh=\"sound_resources_refresh()\">"
                              "<div class=\"data_collection_header sound_resource_header\">"
                              "<span>Resource</span></div>"
                              "<div class=\"data_collection_summary managed_list_title\" "
                              "data-list-id=\"data.sound.resources\"></div>"
                              "<div id=\"sound_resource_rows\" "
                              "class=\"data_collection_rows sound_resource_rows managed_list_rows\" "
                              "tabindex=\"0\" "
                              "data-list-id=\"data.sound.resources\" "
                              "data-empty-text=\"This sound object has no sound resources.\"></div>"
                              "<div class=\"data_collection_action_bar\">"
                              "<button id=\"sound_resource_add\" type=\"button\" "
                              "class=\"data_collection_action add\" title=\"Add sound resource\">"
                              "<span class=\"data_collection_action_mark horizontal\"></span>"
                              "<span class=\"data_collection_action_mark vertical\"></span>"
                              "</button></div></div>";
        }
    } else if (object_type == nw::ObjectType::store
        && state.object_workbench_surface == ObjectWorkbenchSurface::store_inventory) {
        content_markup += "<div id=\"store_inventory_collection\" "
                          "class=\"data_object_collection smalls_refresh\" "
                          "onrefresh=\"store_inventory_refresh()\">"
                          "<div class=\"store_inventory_drop_targets\">"
                          "<div id=\"store_inventory_drop_0\" class=\"store_inventory_drop_target\" "
                          "data-category=\"0\">Armor</div>"
                          "<div id=\"store_inventory_drop_1\" class=\"store_inventory_drop_target\" "
                          "data-category=\"1\">Misc</div>"
                          "<div id=\"store_inventory_drop_2\" class=\"store_inventory_drop_target\" "
                          "data-category=\"2\">Potions</div>"
                          "<div id=\"store_inventory_drop_3\" class=\"store_inventory_drop_target\" "
                          "data-category=\"3\">Rings</div>"
                          "<div id=\"store_inventory_drop_4\" class=\"store_inventory_drop_target\" "
                          "data-category=\"4\">Weapons</div>"
                          "</div>"
                          "<div class=\"data_collection_header store_inventory_header\">"
                          "<span>Category</span><span>Item</span><span>Resref</span><span>Stack</span>"
                          "</div><div class=\"data_collection_summary managed_list_title\" "
                          "data-list-id=\"data.store.inventory\"></div>"
                          "<div class=\"data_collection_rows store_inventory_rows managed_list_rows\" "
                          "tabindex=\"0\" "
                          "data-list-id=\"data.store.inventory\" "
                          "data-empty-text=\"This store has no inventory items.\"></div></div>";
    } else {
        content_markup += "<div class=\"property_tree_header\"><span class=\"property_tree_header_name\">Field</span>";
        content_markup += "<span class=\"property_tree_header_value\">Value</span>";
        content_markup += "<span id=\"property_tree_count\" class=\"property_tree_count\">";
        content_markup += std::to_string(active_details_row_count(state, workspace));
        content_markup += "</span></div><div id=\"property_tree_rows\" class=\"property_tree_rows\">";
        content_markup += "<div class=\"property_tree_empty\">Select an object to inspect.</div></div>";
    }
    if (object_type == nw::ObjectType::sound
        && state.object_workbench_surface != ObjectWorkbenchSurface::locstring) {
        content_markup += "<div id=\"object_details_combobox_popup\" "
                          "class=\"combobox_options combobox_popup "
                          "object_details_combobox_popup\"></div>";
    }
    content_markup += "</div>";
}

void clear_object_workbench_children(ObjectWorkbenchViewState& state)
{
    state.creature_view.creature_class_presentation = {};
    clear_active_creature_feats(state.creature_view);
    clear_active_creature_spells(state.creature_view);
    clear_active_creature_inventory(state.inventory_view);
    clear_active_appearances(state.appearance_view);
    clear_active_sound_catalog(state.appearance_view);
}

void activate_object_workbench(ObjectWorkbenchViewState& state, ObjectHandle object, std::string_view tab_id)
{
    state.active_object_tab_id = tab_id;
    state.object_workbench_surface = default_object_workbench_surface();
    clear_active_appearances(state.appearance_view);
    clear_active_sound_catalog(state.appearance_view);
    configure_details_list(state);
    state.details_list.set_scroll_top(0);
    rebuild_object_workbench_snapshots(state, object);
    if (object.type == ObjectType::creature) {
        configure_creature_feat_list(state.creature_view);
        state.creature_view.creature_feat_list.set_scroll_top(0);
        rebuild_active_creature_feats(state.creature_view, object);
        configure_creature_spell_list(state.creature_view);
        state.creature_view.creature_spell_list.set_scroll_top(0);
        rebuild_active_creature_spells(state.creature_view, object);
        state.inventory_view.creature_inventory_page = 0;
        state.inventory_view.creature_inventory_selection = -1;
        rebuild_active_creature_inventory(state.inventory_view, object);
    } else {
        clear_active_creature_feats(state.creature_view);
        clear_active_creature_spells(state.creature_view);
        if (object_has_grid_inventory(object.type)) {
            state.inventory_view.creature_inventory_page = 0;
            state.inventory_view.creature_inventory_selection = -1;
            rebuild_active_creature_inventory(state.inventory_view, object);
        } else {
            clear_active_creature_inventory(state.inventory_view);
        }
    }
}

bool refresh_object_workbench_snapshots(ObjectWorkbenchViewState& state, ObjectHandle object)
{
    if (object != state.object_details.object || state.object_details.status != ObjectDetailsStatus::ready) {
        return false;
    }
    rebuild_object_workbench_snapshots(state, object);
    if (object.type == ObjectType::creature) {
        const auto selected_class = state.creature_view.creature_spells.selected_class;
        const auto selected_metamagic = state.creature_view.creature_spells.selected_metamagic;
        rebuild_active_creature_feats(state.creature_view, object);
        rebuild_active_creature_spells(state.creature_view, object, selected_class, selected_metamagic);
        rebuild_active_creature_inventory(state.inventory_view, object);
    } else if (object_has_grid_inventory(object.type)) {
        rebuild_active_creature_inventory(state.inventory_view, object);
    }
    return true;
}
std::optional<ObjectWorkbenchCommandClick> capture_object_workbench_command_click(
    Rml::Element* hit, const ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    uint64_t module_generation)
{
    ObjectWorkbenchCommandClick click;
    const auto finish = [&] {
        if (click.kind != ObjectWorkbenchCommandKind::none) {
            click.object = state.object_details.object;
            click.module_generation = module_generation;
            click.tab_id = workspace.active_tab_id();
        }
        return std::move(click);
    };
    const auto args = [&](std::initializer_list<std::string> values) {
        click.args.reserve(values.size());
        for (const auto& value : values) {
            click.args.push_back(CommandArg::positional_string(value));
        }
    };
    bool add = false;
    for (auto* cursor = hit; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == "object_variable_add") {
            add = true;
            break;
        }
    }
    if (add) {
        click.kind = ObjectWorkbenchCommandKind::variable_add;
        click.release_phase = ClientRmlForwardPhase::before_native;
        return finish();
    }
    if (auto* remove = find_ancestor_with_class(hit, "object_variable_remove")) {
        click.kind = ObjectWorkbenchCommandKind::variable_remove;
        click.release_phase = ClientRmlForwardPhase::before_native;
        args({remove->GetAttribute<Rml::String>("data-name", ""),
            remove->GetAttribute<Rml::String>("data-type", "")});
        return finish();
    }
    if (auto* type = find_ancestor_with_class(hit, "object_variable_type")) {
        click.release_phase = ClientRmlForwardPhase::before_native;
        const auto current_text = type->GetAttribute<Rml::String>("data-type", "");
        const auto current = parse_decimal_int32(current_text);
        if (current && *current >= 1 && *current <= 3) {
            click.kind = ObjectWorkbenchCommandKind::variable_type;
            args({type->GetAttribute<Rml::String>("data-name", ""), current_text,
                std::to_string(*current == 3 ? 1 : *current + 1)});
        }
        return finish();
    }
    auto* control = find_ancestor_with_class(hit, "object_details_integer_step");
    auto kind = ObjectWorkbenchCommandKind::integer_step;
    if (!control) {
        control = find_ancestor_with_class(hit, "object_details_boolean");
        kind = ObjectWorkbenchCommandKind::boolean;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "object_details_cycle_state");
        kind = ObjectWorkbenchCommandKind::door_state;
    }
    if (!control) { return std::nullopt; }
    click.release_phase = kind == ObjectWorkbenchCommandKind::integer_step
        ? ClientRmlForwardPhase::before_native
        : ClientRmlForwardPhase::after_native;
    const auto row_index = parse_decimal_int32(control->GetAttribute<Rml::String>("data-row", ""));
    const auto current = parse_decimal_int32(control->GetAttribute<Rml::String>("data-current", ""));
    if (!row_index || *row_index < 0 || !current || !active_object_details_matches_tab(state, workspace)
        || static_cast<size_t>(*row_index) >= state.object_details.rows.size()) {
        return finish();
    }
    const auto& row = state.object_details.rows[static_cast<size_t>(*row_index)];
    if (row.kind != ObjectDetailsRowKind::value || row.edit_value != *current) { return finish(); }
    int32_t desired = 0;
    if (kind == ObjectWorkbenchCommandKind::integer_step) {
        const auto delta = parse_decimal_int32(control->GetAttribute<Rml::String>("data-delta", ""));
        if (!delta || (*delta != -1 && *delta != 1) || row.editor != ObjectDetailsEditorKind::integer
            || (*delta < 0 ? row.edit_value <= row.edit_min : row.edit_value >= row.edit_max)) {
            return finish();
        }
        desired = *current + *delta;
    } else if (kind == ObjectWorkbenchCommandKind::boolean) {
        if ((*current != 0 && *current != 1) || row.editor != ObjectDetailsEditorKind::boolean) { return finish(); }
        desired = 1 - *current;
    } else {
        if (*current < 0 || *current > 2 || row.editor != ObjectDetailsEditorKind::door_state) { return finish(); }
        desired = (*current + 1) % 3;
    }
    click.kind = kind;
    click.property = ObjectWorkbenchCommandRow{
        .propset_type = row.propset_type,
        .field_index = row.field_index,
        .element_index = row.element_index,
        .row = static_cast<uint32_t>(*row_index),
        .current = *current,
        .editor = row.editor,
    };
    args({std::to_string(*row_index), std::to_string(*current), std::to_string(desired)});
    return finish();
}

bool close_active_smalls_selector(Rml::ElementDocument* document)
{
    if (!document) { return false; }
    Rml::ElementList selectors;
    document->GetElementsByClassName(selectors, "smalls_selector");
    for (auto* selector : selectors) {
        if (!selector->IsClassSet("active")) { continue; }
        Rml::ElementList close_buttons;
        selector->GetElementsByClassName(close_buttons, "smalls_selector_close");
        if (!close_buttons.empty()) { return close_buttons.front()->DispatchEvent("click", {}); }
    }
    return false;
}

std::optional<ObjectWorkbenchSurfaceClick> capture_object_workbench_surface_click(Rml::Element* hit)
{
    auto* control = find_ancestor_with_class(hit, "object_workbench_tab");
    if (!control) {
        control = find_ancestor_with_class(hit, "object_workbench_surface_button");
    }
    if (!control) { return std::nullopt; }
    const auto row_value = parse_decimal_int32(
        control->GetAttribute<Rml::String>("data-row", ""));
    return ObjectWorkbenchSurfaceClick{
        .surface = object_workbench_surface_from_name(control->GetAttribute<Rml::String>("data-surface", "")),
        .row = row_value && *row_value >= 0
            ? std::optional{static_cast<uint32_t>(*row_value)}
            : std::nullopt,
    };
}

bool apply_object_workbench_surface_click(ObjectWorkbenchSurfaceClick& click,
    ObjectWorkbenchViewState& state, Rml::ElementDocument* document, ToolsetBackend& backend)
{
    if (!std::exchange(click.pending, false)) { return false; }
    clear_creature_spell_filter(state.creature_view);
    clear_color_editor(state.appearance_view);
    (void)close_active_smalls_selector(document);
    close_appearance_selector(state.appearance_view);
    close_sound_resource_selector(state.appearance_view);
    const auto type = state.object_details.object.type;
    bool allowed = false;
    if (click.surface) {
        switch (*click.surface) {
        case ObjectWorkbenchSurface::details:
            allowed = true;
            break;
        case ObjectWorkbenchSurface::locstring:
            allowed = click.row && *click.row < state.object_details.rows.size()
                && state.object_details.rows[*click.row].editor
                    == ObjectDetailsEditorKind::locstring;
            break;
        case ObjectWorkbenchSurface::sheet:
        case ObjectWorkbenchSurface::classes:
        case ObjectWorkbenchSurface::feats:
        case ObjectWorkbenchSurface::spells:
            allowed = type == ObjectType::creature;
            break;
        case ObjectWorkbenchSurface::variables:
            allowed = type != ObjectType::invalid;
            break;
        case ObjectWorkbenchSurface::haks:
            allowed = type == ObjectType::module && !backend.current_project_dir().empty();
            break;
        case ObjectWorkbenchSurface::appearance:
            allowed = appearance_catalog_kind(type).has_value() || type == ObjectType::door || type == ObjectType::item;
            break;
        case ObjectWorkbenchSurface::item_properties:
            allowed = type == ObjectType::item;
            break;
        case ObjectWorkbenchSurface::inventory:
            allowed = object_has_grid_inventory(type);
            break;
        case ObjectWorkbenchSurface::spawns:
            allowed = type == ObjectType::encounter;
            break;
        case ObjectWorkbenchSurface::sounds:
            allowed = type == ObjectType::sound;
            break;
        case ObjectWorkbenchSurface::store_inventory:
            allowed = type == ObjectType::store;
            break;
        default:
            break;
        }
    }
    if (allowed) {
        if (*click.surface == ObjectWorkbenchSurface::locstring) {
            const auto& row = state.object_details.rows[*click.row];
            const auto current = read_object_locstring(kernel::runtime(),
                {state.object_details.object, row.locstring_storage,
                    row.propset_type, row.field_index});
            if (!current) { return false; }
            state.locstring_row = click.row;
            state.locstring_language = current->strref() != UINT32_MAX
                ? std::nullopt
                : std::optional<LanguageID>{state.toolset_language};
            state.locstring_feminine = false;
        }
        state.object_workbench_surface = *click.surface;
        state.locstring_panel_rendered = false;
        if (*click.surface == ObjectWorkbenchSurface::appearance && appearance_catalog_kind(type)) {
            rebuild_active_appearances(state.appearance_view, backend.module_generation(), state.object_details.object);
        }
    }
    invalidate_details_render(state);
    return true;
}

namespace {
Rml::Element* combo_ancestor_with_id(Rml::Element* hit, const char* id)
{
    for (auto* element = hit; element; element = element->GetParentNode()) {
        if (element->GetId() == id) { return element; }
    }
    return nullptr;
}

bool current_command_property_matches(ObjectHandle object, const ObjectWorkbenchCommandRow& property)
{
    ObjectDetailsSnapshot current;
    build_object_details(kernel::runtime(), object, current);
    if (current.status != ObjectDetailsStatus::ready || property.row >= current.rows.size()) { return false; }
    const auto& row = current.rows[property.row];
    return row.kind == ObjectDetailsRowKind::value && row.editor == property.editor
        && row.propset_type == property.propset_type && row.field_index == property.field_index
        && row.element_index == property.element_index && row.edit_value == property.current;
}
} // namespace

std::optional<ObjectWorkbenchComboClick> capture_object_workbench_combo_click(Rml::Element* hit,
    const ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    uint64_t module_generation, uint64_t resource_generation)
{
    auto* control = find_ancestor_with_class(hit, "combobox_option");
    const bool option = control != nullptr;
    bool details_combo = option && state.object_details_combobox.is_active()
        && combo_ancestor_with_id(control, "object_details_combobox_popup");
    if (!control) {
        control = find_ancestor_with_class(hit, "object_details_sound_position_field");
        details_combo = control != nullptr;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "object_locstring_source_field");
        details_combo = control != nullptr;
    }
    if (!control) { control = find_ancestor_with_class(hit, "creature_spell_filter_field"); }
    if (!control) { return std::nullopt; }
    ObjectWorkbenchComboClick click;
    const auto target = object_workbench_target(state, workspace);
    if (!active_object_matches_tab(state, workspace) || !workspace.active_tab() || !kernel::objects().valid(target.object)) { return click; }
    if (option) {
        const auto value = parse_decimal_int32(control->GetAttribute<Rml::String>("data-key", ""));
        if (!value) { return click; }
        click.value = *value;
    }
    if (details_combo) {
        const auto row_index = option ? state.object_details_combobox_row
                                      : [&]() -> std::optional<uint32_t> {
            const auto row = parse_decimal_int32(control->GetAttribute<Rml::String>("data-row", ""));
            return row && *row >= 0 ? std::optional{static_cast<uint32_t>(*row)} : std::nullopt;
        }();
        if (!active_object_details_matches_tab(state, workspace)
            || !row_index || *row_index >= state.object_details.rows.size()) { return click; }
        const auto& row = state.object_details.rows[*row_index];
        const bool sound = row.editor == ObjectDetailsEditorKind::sound_position
            && state.object_workbench_surface == ObjectWorkbenchSurface::details
            && row.edit_value >= 0 && row.edit_value <= 2
            && (!option || (click.value >= 0 && click.value <= 2));
        std::optional<LanguageID> source_language;
        const bool locstring = row.editor == ObjectDetailsEditorKind::locstring
            && state.object_workbench_surface == ObjectWorkbenchSurface::locstring
            && state.locstring_row && *state.locstring_row == *row_index
            && (!option || locstring_source_language(click.value, source_language));
        if (row.kind != ObjectDetailsRowKind::value || (!sound && !locstring)) { return click; }
        click.property = ObjectWorkbenchCommandRow{.propset_type = row.propset_type, .field_index = row.field_index, .element_index = row.element_index, .row = *row_index, .current = row.edit_value, .editor = row.editor};
        click.source_row = state.object_details_combobox_row;
        click.source_selection = state.object_details_combobox.selected_key();
        click.active = state.object_details_combobox.is_active();
        click.popup_visible = state.object_details_combobox.popup_visible();
        click.kind = sound
            ? (option ? ObjectWorkbenchComboKind::sound_select : ObjectWorkbenchComboKind::sound_open)
            : (option ? ObjectWorkbenchComboKind::locstring_source_select
                      : ObjectWorkbenchComboKind::locstring_source_open);
        if (!option) { click.field_id = control->GetId(); }
    } else {
        const auto& creature = state.creature_view;
        if (option && !active_creature_spell_filter_matches_tab(creature, target)) { return click; }
        const auto field = option ? std::optional{creature.creature_spell_filter_field}
                                  : creature_spell_filter_field_from_name(control->GetAttribute<Rml::String>("data-filter", ""));
        if (!field || *field == CreatureSpellFilterField::none || !active_creature_spells_match_tab(creature, target)) { return click; }
        click.field = *field;
        click.source_field = creature.creature_spell_filter_field;
        click.selected_class = creature.creature_spells.selected_class;
        click.selected_metamagic = creature.creature_spells.selected_metamagic;
        click.level = creature.creature_spell_level;
        click.query = creature.creature_spell_query;
        click.source_selection = creature.creature_spell_combobox.selected_key();
        click.active = creature.creature_spell_combobox.is_active();
        click.popup_visible = creature.creature_spell_combobox.popup_visible();
        click.kind = option ? ObjectWorkbenchComboKind::spell_select : ObjectWorkbenchComboKind::spell_open;
    }
    click.object = target.object;
    click.surface = target.surface;
    click.tab_id = workspace.active_tab_id();
    click.module_generation = module_generation;
    click.resource_generation = resource_generation;
    click.mutation_epoch = object_mutation_state().epoch;
    return click;
}

ObjectWorkbenchComboEffect apply_object_workbench_combo_click(ObjectWorkbenchComboClick& click,
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context)
{
    const auto kind = std::exchange(click.kind, ObjectWorkbenchComboKind::none);
    const auto target = object_workbench_target(state, workspace);
    if (kind == ObjectWorkbenchComboKind::none || kind > ObjectWorkbenchComboKind::spell_select
        || click.surface > ObjectWorkbenchSurface::store_inventory
        || click.field > CreatureSpellFilterField::metamagic || click.source_field > CreatureSpellFilterField::metamagic
        || !active_object_matches_tab(state, workspace) || target.object != click.object || target.surface != click.surface
        || workspace.active_tab_id() != click.tab_id || context.active_tab_id != click.tab_id || context.workspace != &workspace
        || smalls_rmlui_host().active_object() != click.object || !kernel::objects().valid(click.object)
        || backend.module_generation() != click.module_generation || kernel::resman().generation() != click.resource_generation
        || ((kind != ObjectWorkbenchComboKind::locstring_source_open
                && kind != ObjectWorkbenchComboKind::locstring_source_select)
            && object_mutation_state().epoch != click.mutation_epoch)) { return ObjectWorkbenchComboEffect::none; }
    if (kind == ObjectWorkbenchComboKind::sound_open || kind == ObjectWorkbenchComboKind::sound_select) {
        if (!click.property || click.property->editor != ObjectDetailsEditorKind::sound_position
            || state.object_details_combobox_row != click.source_row || state.object_details_combobox.is_active() != click.active
            || state.object_details_combobox.popup_visible() != click.popup_visible
            || state.object_details_combobox.selected_key() != click.source_selection
            || !current_command_property_matches(click.object, *click.property)) { return ObjectWorkbenchComboEffect::none; }
        if (kind == ObjectWorkbenchComboKind::sound_open) {
            return open_object_details_sound_position_combobox(doc, state, workspace, click.property->row)
                ? ObjectWorkbenchComboEffect::sound_opened
                : ObjectWorkbenchComboEffect::none;
        }
        return click.active && commit_object_details_sound_position(doc, state, workspace, backend, shell, context, click.value)
            ? ObjectWorkbenchComboEffect::sound_selected
            : ObjectWorkbenchComboEffect::none;
    }
    if (kind == ObjectWorkbenchComboKind::locstring_source_open
        || kind == ObjectWorkbenchComboKind::locstring_source_select) {
        if (!click.property || click.property->editor != ObjectDetailsEditorKind::locstring
            || state.object_details_combobox_row != click.source_row
            || state.object_details_combobox.is_active() != click.active
            || state.object_details_combobox.popup_visible() != click.popup_visible
            || state.object_details_combobox.selected_key() != click.source_selection
            || !current_command_property_matches(click.object, *click.property)) {
            return ObjectWorkbenchComboEffect::none;
        }
        if (kind == ObjectWorkbenchComboKind::locstring_source_open) {
            return open_object_details_locstring_source_combobox(doc, state, workspace,
                       click.property->row)
                ? ObjectWorkbenchComboEffect::locstring_source_opened
                : ObjectWorkbenchComboEffect::none;
        }
        return click.active && commit_object_details_locstring_source(doc, state, workspace, backend, shell, context, click.value)
            ? ObjectWorkbenchComboEffect::locstring_source_selected
            : ObjectWorkbenchComboEffect::none;
    }
    auto& creature = state.creature_view;
    if (click.field == CreatureSpellFilterField::none || click.field > CreatureSpellFilterField::metamagic
        || !active_creature_spells_match_tab(creature, target) || creature.creature_spell_filter_field != click.source_field
        || creature.creature_spells.selected_class != click.selected_class
        || creature.creature_spells.selected_metamagic != click.selected_metamagic || creature.creature_spell_level != click.level
        || creature.creature_spell_query != click.query || creature.creature_spell_combobox.is_active() != click.active
        || creature.creature_spell_combobox.popup_visible() != click.popup_visible
        || creature.creature_spell_combobox.selected_key() != click.source_selection) { return ObjectWorkbenchComboEffect::none; }
    if (kind == ObjectWorkbenchComboKind::spell_open) {
        if (creature.creature_spell_filter_field == click.field && creature.creature_spell_combobox.is_active()) {
            if (creature.creature_spell_combobox.popup_visible()) {
                creature.creature_spell_combobox.hide_popup();
            } else {
                (void)creature.creature_spell_combobox.show_popup();
            }
        } else {
            (void)open_creature_spell_filter(creature, target, click.field);
        }
        return ObjectWorkbenchComboEffect::spell_opened;
    }
    if (creature.creature_spell_filter_field != click.field) { return ObjectWorkbenchComboEffect::none; }
    return commit_creature_spell_filter(creature, target, click.value)
        ? ObjectWorkbenchComboEffect::spell_selected
        : ObjectWorkbenchComboEffect::none;
}

bool focus_object_workbench_combo_field(Rml::ElementDocument* doc, const ObjectWorkbenchComboClick& click,
    const ObjectWorkbenchViewState& state, const WorkspaceState& workspace)
{
    if (!active_object_details_matches_tab(state, workspace) || state.object_details.object != click.object
        || workspace.active_tab_id() != click.tab_id || !click.property
        || click.property->row > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) { return false; }
    auto* field = doc && !click.field_id.empty() ? doc->GetElementById(click.field_id) : nullptr;
    if (!field || !click.property
        || parse_decimal_int32(field->GetAttribute<Rml::String>("data-row", "")) != std::optional{static_cast<int32_t>(click.property->row)}) { return false; }
    return field->Focus();
}

bool execute_object_workbench_command_click(ObjectWorkbenchCommandClick& click,
    const ObjectWorkbenchViewState& state, const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context)
{
    const auto kind = std::exchange(click.kind, ObjectWorkbenchCommandKind::none);
    const auto phase = kind <= ObjectWorkbenchCommandKind::integer_step
        ? ClientRmlForwardPhase::before_native
        : ClientRmlForwardPhase::after_native;
    if (kind == ObjectWorkbenchCommandKind::none
        || kind > ObjectWorkbenchCommandKind::door_state || click.release_phase != phase
        || !active_object_matches_tab(state, workspace) || state.object_details.object != click.object
        || workspace.active_tab_id() != click.tab_id || context.active_tab_id != click.tab_id
        || context.workspace != &workspace || backend.module_generation() != click.module_generation
        || smalls_rmlui_host().active_object() != click.object) {
        return false;
    }
    const char* command = nullptr;
    size_t arg_count = 3;
    switch (kind) {
    case ObjectWorkbenchCommandKind::variable_add:
        command = "object.variables.add";
        arg_count = 0;
        break;
    case ObjectWorkbenchCommandKind::variable_remove:
        command = "object.variables.remove";
        arg_count = 2;
        break;
    case ObjectWorkbenchCommandKind::variable_type:
        command = "object.variables.set_type";
        break;
    case ObjectWorkbenchCommandKind::integer_step:
    case ObjectWorkbenchCommandKind::door_state:
        command = "object.details.set_integer";
        break;
    case ObjectWorkbenchCommandKind::boolean:
        command = "object.details.set_boolean";
        break;
    default:
        return false;
    }
    if (click.args.size() != arg_count) { return false; }
    const bool details = kind == ObjectWorkbenchCommandKind::integer_step
        || kind == ObjectWorkbenchCommandKind::boolean || kind == ObjectWorkbenchCommandKind::door_state;
    if (details) {
        if (!click.property) { return false; }
        const auto& property = *click.property;
        if (!current_command_property_matches(click.object, property)) { return false; }
    }
    const auto result = backend.execute_command({command, std::move(click.args)}, context);
    append_command_results(shell, {&result, 1});
    return result.ok();
}

ObjectWorkbenchFieldKeyEffect handle_object_workbench_field_key(const SDL_KeyboardEvent& key,
    Rml::Context* context, Rml::ElementDocument* doc, ObjectWorkbenchViewState& state,
    const WorkspaceState& workspace, ToolsetBackend& backend, ShellController& shell,
    const CommandContext& command)
{
    if (!context) { return ObjectWorkbenchFieldKeyEffect::none; }
    const auto blur_focus = [&] { if (auto* focus = context->GetFocusElement()) { focus->Blur(); } };
    auto* focused_variable_name = find_ancestor_with_class(
        context->GetFocusElement(), "object_variable_name");
    auto* focused_variable_value = find_ancestor_with_class(
        context->GetFocusElement(), "object_variable_value");
    if (!key.repeat && key.key == SDLK_ESCAPE
        && (focused_variable_name || focused_variable_value)) {
        blur_focus();
        sync_object_variable_window(doc, state, workspace, true);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }

    auto* focused_details_integer = find_ancestor_with_class(
        context->GetFocusElement(), "object_details_integer");
    auto* focused_panel_text = find_ancestor_with_class(
        context->GetFocusElement(), "object_locstring_text_input");
    auto* focused_panel_strref = find_ancestor_with_class(
        context->GetFocusElement(), "object_locstring_strref_input");
    if (!key.repeat && key.key == SDLK_ESCAPE
        && (focused_panel_text || focused_panel_strref)) {
        state.suppress_blur_commit = true;
        blur_focus();
        state.suppress_blur_commit = false;
        sync_object_locstring_panel(doc, state, workspace, true);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }
    if (!key.repeat && key.key == SDLK_S
        && (key.mod & SDL_KMOD_CTRL)
        && !(key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI))
        && (focused_panel_text || focused_panel_strref)) {
        blur_focus();
        return ObjectWorkbenchFieldKeyEffect::none;
    }
    if (!key.repeat && key.key == SDLK_ESCAPE
        && focused_details_integer) {
        blur_focus();
        sync_object_details_window(doc, state, workspace, true);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }

    if ((key.key == SDLK_UP || key.key == SDLK_DOWN)
        && focused_details_integer
        && !(key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
        const auto minimum = parse_decimal_int32(
            focused_details_integer->GetAttribute<Rml::String>("data-min", ""));
        const auto maximum = parse_decimal_int32(
            focused_details_integer->GetAttribute<Rml::String>("data-max", ""));
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
            focused_details_integer);
        const auto value = input
            ? parse_decimal_int32(input->GetValue())
            : std::nullopt;
        if (input && value && minimum && maximum
            && *minimum <= *value && *value <= *maximum) {
            const bool can_decrement = key.key == SDLK_DOWN
                && *value > *minimum;
            const bool can_increment = key.key == SDLK_UP
                && *value < *maximum;
            if (can_decrement || can_increment) {
                const int32_t adjusted = *value
                    + (can_increment ? 1 : -1);
                const Rml::String adjusted_text = std::to_string(adjusted);
                input->SetValue(adjusted_text);
                const int cursor = static_cast<int>(adjusted_text.size());
                input->SetSelectionRange(cursor, cursor);
            }
        }
        return ObjectWorkbenchFieldKeyEffect::handled;
    }

    if (!key.repeat
        && (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER)
        && focused_details_integer
        && !(key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
        const std::string row = focused_details_integer->GetAttribute<Rml::String>(
            "data-row", "");
        const std::string current = focused_details_integer->GetAttribute<Rml::String>(
            "data-current", "");
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(
            focused_details_integer);
        const std::string desired = input ? input->GetValue() : std::string{};
        const auto result = backend.execute_command(
            "object.details.set_integer",
            {row, current, desired},
            command);
        append_command_results(shell, {&result, 1});
        if (result.ok()) {
            blur_focus();
        }
        return ObjectWorkbenchFieldKeyEffect::handled;
    }

    auto* focused_sound_position = find_ancestor_with_class(
        context->GetFocusElement(),
        "object_details_sound_position_field");
    const auto focused_sound_position_row = focused_sound_position
        ? parse_decimal_int32(focused_sound_position->GetAttribute<Rml::String>(
              "data-row", ""))
        : std::nullopt;
    const bool sound_position_focused = focused_sound_position_row
        && *focused_sound_position_row >= 0
        && !(key.mod
            & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
    if (sound_position_focused
        && (key.key == SDLK_UP || key.key == SDLK_DOWN)) {
        const auto row_index = static_cast<uint32_t>(
            *focused_sound_position_row);
        if (state.object_details_combobox_row != row_index
            || !state.object_details_combobox.is_active()) {
            (void)open_object_details_sound_position_combobox(
                doc, state, workspace, row_index);
        } else if (!state.object_details_combobox.popup_visible()) {
            (void)state.object_details_combobox.show_popup();
        }
        (void)state.object_details_combobox.move_selection(
            key.key == SDLK_UP ? -1 : 1);
        (void)sync_object_details_combobox(doc, state, workspace, true);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }
    if (!key.repeat && sound_position_focused
        && (key.key == SDLK_RETURN
            || key.key == SDLK_KP_ENTER)) {
        const auto row_index = static_cast<uint32_t>(
            *focused_sound_position_row);
        if (state.object_details_combobox_row != row_index
            || !state.object_details_combobox.is_active()) {
            if (open_object_details_sound_position_combobox(
                    doc, state, workspace, row_index)) {
                (void)sync_object_details_combobox(doc, state, workspace, true);
            }
        } else if (!state.object_details_combobox.popup_visible()) {
            (void)state.object_details_combobox.show_popup();
            (void)sync_object_details_combobox(doc, state, workspace, true);
        } else if (const auto selected
            = state.object_details_combobox.selected_key()) {
            if (commit_object_details_sound_position(
                    doc, state, workspace, backend, shell, command, *selected)) {
                return ObjectWorkbenchFieldKeyEffect::content_changed;
            }
        }
        return ObjectWorkbenchFieldKeyEffect::handled;
    }

    auto* focused_locstring_source = find_ancestor_with_class(
        context->GetFocusElement(), "object_locstring_source_field");
    const auto focused_locstring_source_row = focused_locstring_source
        ? parse_decimal_int32(focused_locstring_source->GetAttribute<Rml::String>(
              "data-row", ""))
        : std::nullopt;
    const bool locstring_source_focused = focused_locstring_source_row
        && *focused_locstring_source_row >= 0
        && !(key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
    if (locstring_source_focused && !key.repeat && key.key == SDLK_ESCAPE) {
        close_object_details_combobox(doc, state);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }
    if (locstring_source_focused && (key.key == SDLK_UP || key.key == SDLK_DOWN)) {
        const auto row_index = static_cast<uint32_t>(*focused_locstring_source_row);
        if (state.object_details_combobox_row != row_index
            || !state.object_details_combobox.is_active()) {
            (void)open_object_details_locstring_source_combobox(doc, state, workspace, row_index);
        } else if (!state.object_details_combobox.popup_visible()) {
            (void)state.object_details_combobox.show_popup();
        }
        (void)state.object_details_combobox.move_selection(key.key == SDLK_UP ? -1 : 1);
        (void)sync_object_details_combobox(doc, state, workspace, true);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }
    if (locstring_source_focused && !key.repeat
        && (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER)) {
        const auto row_index = static_cast<uint32_t>(*focused_locstring_source_row);
        if (state.object_details_combobox_row != row_index
            || !state.object_details_combobox.is_active()) {
            (void)open_object_details_locstring_source_combobox(doc, state, workspace, row_index);
        } else if (!state.object_details_combobox.popup_visible()) {
            (void)state.object_details_combobox.show_popup();
        } else if (const auto selected = state.object_details_combobox.selected_key()) {
            if (commit_object_details_locstring_source(doc, state, workspace,
                    backend, shell, command, *selected)) {
                return ObjectWorkbenchFieldKeyEffect::content_changed;
            }
        }
        (void)sync_object_details_combobox(doc, state, workspace, true);
        return ObjectWorkbenchFieldKeyEffect::handled;
    }

    return ObjectWorkbenchFieldKeyEffect::none;
}

std::optional<ObjectWorkbenchClick> capture_object_workbench_click(Rml::Element* hit, Rml::Vector2f point,
    const ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    uint64_t module_generation, uint64_t resource_generation)
{
    const auto target = object_workbench_target(state, workspace);
    if (auto click = capture_color_editor_click(hit, point, state.appearance_view, target, workspace, module_generation, resource_generation)) {
        const auto phase = click->release_phase;
        return ObjectWorkbenchClick{std::move(*click), phase};
    }
    if (auto click = capture_object_workbench_combo_click(hit, state, workspace, module_generation, resource_generation)) {
        return ObjectWorkbenchClick{std::move(*click)};
    }
    if (auto click = capture_placed_area_object_click(hit)) {
        const auto phase = click->kind == PlacedAreaObjectClickKind::none ? ClientRmlForwardPhase::none : ClientRmlForwardPhase::before_native;
        return ObjectWorkbenchClick{std::move(*click), phase};
    }
    if (auto click = capture_object_workbench_command_click(hit, state, workspace, module_generation)) {
        const auto phase = click->release_phase;
        return ObjectWorkbenchClick{std::move(*click), phase};
    }
    if (auto click = capture_sound_resource_click(hit, state.appearance_view, target, workspace, module_generation, resource_generation)) {
        return ObjectWorkbenchClick{std::move(*click)};
    }
    if (auto click = capture_appearance_back_click(hit, state.appearance_view, target, workspace, module_generation, resource_generation)) {
        return ObjectWorkbenchClick{std::move(*click)};
    }
    if (auto click = capture_object_workbench_surface_click(hit)) {
        return ObjectWorkbenchClick{std::move(*click)};
    }
    if (auto click = capture_appearance_catalog_click(hit, state.appearance_view, target, workspace, module_generation, resource_generation)) {
        return ObjectWorkbenchClick{std::move(*click)};
    }
    if (auto click = capture_inventory_workbench_click(hit, state.inventory_view, target, workspace, module_generation)) {
        const auto phase = click->release_phase;
        return ObjectWorkbenchClick{std::move(*click), phase};
    }
    if (auto click = capture_creature_workbench_command_click(hit, state.creature_view, target, workspace, module_generation)) {
        const auto phase = click->release_phase;
        return ObjectWorkbenchClick{std::move(*click), phase};
    }
    return std::nullopt;
}

void prepare_object_workbench_click(const ObjectWorkbenchClick& click, Rml::ElementDocument* doc)
{
    if (!click.pending || click.payload.valueless_by_exception()
        || click.release_phase > ClientRmlForwardPhase::after_native) { return; }
    const bool close = std::visit([](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, ColorEditorClick>) {
            return payload.kind == ColorEditorClickKind::field;
        } else if constexpr (std::is_same_v<T, SoundResourceClick>) {
            return payload.kind == SoundResourceClickKind::open;
        } else if constexpr (std::is_same_v<T, AppearanceCatalogClick>) {
            return payload.kind == AppearanceCatalogClickKind::open;
        }
        return false;
    },
        click.payload);
    if (close) { (void)close_active_smalls_selector(doc); }
}

ObjectWorkbenchClickEffect apply_object_workbench_click(ObjectWorkbenchClick& click, Rml::ElementDocument* doc,
    ObjectWorkbenchViewState& state, const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context)
{
    if (!std::exchange(click.pending, false) || click.payload.valueless_by_exception()
        || click.release_phase > ClientRmlForwardPhase::after_native) { return {}; }
    const auto target = [&] { return object_workbench_target(state, workspace); };
    return std::visit([&](auto& payload) -> ObjectWorkbenchClickEffect {
        using T = std::decay_t<decltype(payload)>;
        ObjectWorkbenchClickEffect effect;
        if constexpr (std::is_same_v<T, ColorEditorClick>) {
            effect.refresh_content = apply_color_editor_click(payload, state.appearance_view, target(), workspace, backend, shell, context);
        } else if constexpr (std::is_same_v<T, ObjectWorkbenchComboClick>) {
            switch (apply_object_workbench_combo_click(payload, doc, state, workspace, backend, shell, context)) {
            case ObjectWorkbenchComboEffect::sound_opened:
                effect.finish = ObjectWorkbenchClickFinish::sound_combo;
                break;
            case ObjectWorkbenchComboEffect::sound_selected:
                effect.refresh_content = true;
                break;
            case ObjectWorkbenchComboEffect::locstring_source_opened:
            case ObjectWorkbenchComboEffect::locstring_source_selected:
                effect.finish = ObjectWorkbenchClickFinish::locstring_source;
                break;
            case ObjectWorkbenchComboEffect::spell_opened:
                effect.refresh_content = true;
                effect.finish = ObjectWorkbenchClickFinish::spell_filter;
                break;
            case ObjectWorkbenchComboEffect::spell_selected:
                effect.refresh_content = true;
                effect.finish = ObjectWorkbenchClickFinish::spells;
                break;
            case ObjectWorkbenchComboEffect::none:
                break;
            }
        } else if constexpr (std::is_same_v<T, PlacedAreaObjectClick>) {
            effect.selection = payload;
        } else if constexpr (std::is_same_v<T, ObjectWorkbenchCommandClick>) {
            (void)execute_object_workbench_command_click(payload, state, workspace, backend, shell, context);
        } else if constexpr (std::is_same_v<T, SoundResourceClick>) {
            const auto changed = apply_sound_resource_click(payload, state.appearance_view, target(), workspace, backend, shell, context);
            effect.refresh_content = changed != SoundResourceClickEffect::none;
            if (changed == SoundResourceClickEffect::opened) { effect.finish = ObjectWorkbenchClickFinish::sound_catalog; }
        } else if constexpr (std::is_same_v<T, AppearanceCatalogClick>) {
            const auto changed = apply_appearance_catalog_click(payload, state.appearance_view, target(), workspace, backend, shell, context);
            effect.refresh_content = changed != AppearanceCatalogClickEffect::none;
            if (changed == AppearanceCatalogClickEffect::opened) { effect.finish = ObjectWorkbenchClickFinish::appearance_catalog; }
        } else if constexpr (std::is_same_v<T, ObjectWorkbenchSurfaceClick>) {
            (void)apply_object_workbench_surface_click(payload, state, doc, backend);
            effect.sync_body = true;
            effect.refresh_content = true;
            effect.finish = ObjectWorkbenchClickFinish::all_windows;
        } else if constexpr (std::is_same_v<T, InventoryWorkbenchClick>) {
            if (apply_inventory_workbench_click(payload, state.inventory_view, target(), workspace, backend, shell, context)) {
                effect.finish = ObjectWorkbenchClickFinish::inventory;
            }
        } else if constexpr (std::is_same_v<T, CreatureWorkbenchCommandClick>) {
            (void)execute_creature_workbench_command_click(payload, state.creature_view, target(), workspace, backend, shell, context);
        }
        return effect;
    },
        click.payload);
}

void finish_object_workbench_click(ObjectWorkbenchClickEffect& effect, const ObjectWorkbenchClick& click,
    Rml::ElementDocument* doc, ObjectWorkbenchViewState& state, const WorkspaceState& workspace,
    uint64_t resource_generation)
{
    const auto finish = std::exchange(effect, {}).finish;
    const auto target = [&] { return object_workbench_target(state, workspace); };
    const auto focus = [&](const char* id) { if (auto* field = find_el(doc, id)) { field->Focus(); } };
    switch (finish) {
    case ObjectWorkbenchClickFinish::sound_combo:
        (void)sync_object_details_combobox(doc, state, workspace, true);
        if (const auto* payload = std::get_if<ObjectWorkbenchComboClick>(&click.payload)) {
            (void)focus_object_workbench_combo_field(doc, *payload, state, workspace);
        }
        break;
    case ObjectWorkbenchClickFinish::locstring_source:
        // Opening must not replace an unsaved textarea. Selection marks the
        // panel dirty, so the same non-forced sync renders the new source.
        (void)sync_object_details_window(doc, state, workspace, false);
        if (const auto* payload = std::get_if<ObjectWorkbenchComboClick>(&click.payload)) {
            (void)focus_object_workbench_combo_field(doc, *payload, state, workspace);
        }
        break;
    case ObjectWorkbenchClickFinish::spell_filter:
        (void)sync_creature_spell_filter_window(doc, state.creature_view, target(), true);
        focus("active_creature_spell_filter_field");
        break;
    case ObjectWorkbenchClickFinish::spells:
        (void)sync_creature_spell_window(doc, state.creature_view, target(), true);
        break;
    case ObjectWorkbenchClickFinish::sound_catalog:
        (void)sync_sound_catalog_window(doc, state.appearance_view, target(), resource_generation, true);
        focus("sound_catalog_search");
        break;
    case ObjectWorkbenchClickFinish::appearance_catalog:
        (void)sync_appearance_window(doc, state.appearance_view, target(), true);
        focus("appearance_search");
        break;
    case ObjectWorkbenchClickFinish::inventory:
        (void)sync_creature_inventory_window(doc, state.inventory_view, target(), true);
        break;
    case ObjectWorkbenchClickFinish::all_windows:
        (void)sync_object_details_window(doc, state, workspace, true);
        (void)sync_creature_feat_window(doc, state.creature_view, target(), true);
        (void)sync_creature_spell_window(doc, state.creature_view, target(), true);
        (void)sync_creature_inventory_window(doc, state.inventory_view, target(), true);
        (void)sync_appearance_window(doc, state.appearance_view, target(), true);
        (void)sync_managed_lists(doc, ui_v1_host(), state.managed_lists, true);
        break;
    case ObjectWorkbenchClickFinish::none:
    default:
        break;
    }
}

} // namespace nw::toolset
