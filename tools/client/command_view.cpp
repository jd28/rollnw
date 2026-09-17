#include "command_view.hpp"
#include "../ui/rml_managed_list.hpp"
#include "blueprint_edits.hpp"
#include "shell_controller.hpp"
#include "toolset_backend.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>

#include <SDL3/SDL.h>

#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace nw::toolset {
namespace {

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

std::string get_input_value(Rml::ElementDocument* doc, const char* id)
{
    if (!doc) {
        return {};
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            return control->GetValue();
        }
        return input->GetAttribute<Rml::String>("value", "");
    }
    return {};
}

void clear_rml_focus(Rml::Context* context)
{
    if (auto* focus = context ? context->GetFocusElement() : nullptr) {
        focus->Blur();
    }
}

std::string focus_restore_id(Rml::Element* focus)
{
    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        const Rml::String id = cursor->GetId();
        if (!id.empty()) {
            return id;
        }
    }
    return {};
}

void capture_command_palette_focus(Rml::Context* context, CommandViewState& state, bool viewport_focused)
{
    if (state.command_palette_restore_captured) {
        return;
    }

    state.command_palette_restore_focus_id.clear();
    state.command_palette_restore_viewport_focus = false;
    state.command_palette_restore_captured = true;

    if (auto* focus = context ? context->GetFocusElement() : nullptr) {
        state.command_palette_restore_focus_id = focus_restore_id(focus);
        return;
    }

    state.command_palette_restore_viewport_focus = viewport_focused;
}

void restore_command_palette_focus(Rml::Context* context, Rml::Context* palette_context, Rml::ElementDocument* doc, CommandViewState& state, bool& viewport_focused)
{
    clear_rml_focus(palette_context);

    if (!state.command_palette_restore_captured) {
        return;
    }

    const std::string restore_focus_id = std::move(state.command_palette_restore_focus_id);
    const bool restore_viewport_focus = state.command_palette_restore_viewport_focus;
    state.command_palette_restore_focus_id.clear();
    state.command_palette_restore_viewport_focus = false;
    state.command_palette_restore_captured = false;

    if (!restore_focus_id.empty()) {
        if (auto* element = find_el(doc, restore_focus_id.c_str())) {
            if (element->IsVisible(true)) {
                element->Focus();
                viewport_focused = false;
                return;
            }
        }
    }

    if (restore_viewport_focus) {
        clear_rml_focus(context);
        viewport_focused = true;
    }
}

std::optional<int32_t> parse_decimal_int32(std::string_view value)
{
    if (value.empty()) { return std::nullopt; }
    int32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) { return std::nullopt; }
    return result;
}

} // namespace

Rml::ElementDocument* load_command_palette_document(Rml::Context& context)
{
    static constexpr const char* kCommandPaletteRml = R"RML(
<rml>
<head>
  <link type="text/rcss" href="panel.rcss" />
  <style>
    body {
      background: transparent;
    }
  </style>
</head>
<body>
  <div id="command_palette">
    <input id="command_input" type="text" value="" />
    <div id="command_list">
      <div id="command_list_items"></div>
    </div>
    <div id="command_details"></div>
  </div>
</body>
</rml>
)RML";

    return context.LoadDocumentFromMemory(kCommandPaletteRml, "command_palette.rml");
}

void set_command_palette_visibility(CommandViewState& state, Rml::Context* context,
    Rml::Context* palette_context, Rml::ElementDocument* document,
    Rml::ElementDocument* palette_document, bool& viewport_focused, bool visible)
{
    const bool was_visible = state.command_palette_ui_visible;
    state.command_palette_ui_visible = visible;
    if (auto* palette = find_el(palette_document, "command_palette")) {
        palette->SetClass("visible", visible);
    }
    if (visible) {
        if (!was_visible) {
            capture_command_palette_focus(context, state, viewport_focused);
        }
        viewport_focused = false;
    } else if (was_visible || state.command_palette_restore_captured) {
        restore_command_palette_focus(context, palette_context, document, state, viewport_focused);
    }
}

CommandOverlayAction handle_command_overlay_target(CommandViewState& state,
    const ToolsetBackend& backend, bool project_load_active, Rml::Element* target)
{
    if (auto* page_button = find_ancestor_with_class(target, "blueprint_operation_page")) {
        const auto delta = parse_decimal_int32(page_button->GetAttribute<Rml::String>("data-delta", ""));
        if (delta && *delta < 0 && state.blueprint_review_page) {
            --state.blueprint_review_page;
        } else if (delta && *delta > 0) {
            ++state.blueprint_review_page;
        }
        sync_blueprint_operation(state, backend, project_load_active);
    } else if (auto* option = find_ancestor_with_class(target, "combobox_option")) {
        const auto key = parse_decimal_int32(option->GetAttribute<Rml::String>("data-key", ""));
        if (key) { (void)commit_command_form_combobox(state, backend, project_load_active, *key); }
    } else if (auto* field = find_ancestor_with_class(target, "command_form_choice_field")) {
        const auto index = parse_decimal_int32(field->GetAttribute<Rml::String>("data-field", ""));
        if (index && *index >= 0 && open_command_form_combobox(state, static_cast<size_t>(*index))) {
            sync_command_form_combobox(state, true);
            field->Focus();
        }
    } else if (auto* button = find_ancestor_with_class(target, "blueprint_authoring_action")) {
        return {CommandOverlayActionKind::dispatch, button->GetAttribute<Rml::String>("data-command", "")};
    } else if (auto* action = find_ancestor_with_class(target, "command_form_action")) {
        const auto index = parse_decimal_int32(action->GetAttribute<Rml::String>("data-index", ""));
        if (index && *index >= 0) {
            return {CommandOverlayActionKind::submit, {}, static_cast<size_t>(*index)};
        }
    } else if (find_ancestor_with_class(target, "command_form_browse")) {
        return {CommandOverlayActionKind::browse_directory};
    } else if (state.command_form_combobox.is_active() && !combobox_contains_element(target)) {
        close_command_form_combobox(state);
    } else {
        return {};
    }
    return {CommandOverlayActionKind::handled};
}

std::optional<CommandPromptAction> take_command_form_action(CommandViewState& state,
    const ToolsetBackend& backend, bool project_load_active, bool dialog_open, size_t index)
{
    if (!state.command_form || index >= state.command_form->actions.size() || dialog_open) { return std::nullopt; }
    sync_command_form(state, backend, project_load_active, true);
    const auto button_id = "command_form_action_" + std::to_string(index);
    if (auto* button = find_el(state.command_overlay_document, button_id.c_str()); button && button->HasAttribute("disabled")) { return std::nullopt; }
    auto action = state.command_form->actions[index];
    if (action.id != "cancel") {
        for (size_t field = 0; field < state.command_form->fields.size(); ++field) {
            const auto& prompt_field = state.command_form->fields[field];
            if (prompt_field.choices.empty()) {
                const auto id = "command_form_field_" + std::to_string(field);
                action.args.push_back(get_input_value(state.command_overlay_document, id.c_str()));
            } else {
                action.args.push_back(prompt_field.value);
            }
        }
    }
    close_command_form_combobox(state);
    state.command_form.reset();
    ++state.command_form_generation;
    return action;
}

bool apply_command_form_directory_result(CommandViewState& state,
    const std::string& path, const std::string& error, bool canceled)
{
    if (!state.command_form || state.command_form_browse_generation != state.command_form_generation) { return false; }
    if (!error.empty()) {
        state.command_form->detail = error;
    } else if (!canceled && state.command_form->fields.size() >= 2) {
        state.command_form->fields[1].value = path;
    }
    ++state.command_form_generation;
    return true;
}

bool take_command_form_prompt(CommandViewState& state, CommandResult& result)
{
    if (!result.prompt || (result.prompt->fields.empty() && !result.prompt->id.starts_with("blueprint."))) { return false; }
    state.command_form = std::move(*result.prompt);
    ++state.command_form_generation;
    result.prompt.reset();
    result.status = CommandStatus::noop;
    result.output_channel = CommandOutputChannel::none;
    return true;
}

void append_command_results(ShellController& shell, std::span<const CommandResult> results)
{
    for (const auto& result : results) {
        if (result.should_log()) {
            shell.append_output(command_output_channel_name(result.output_channel), result.message);
        }
    }
}

void append_terminal_results(ShellController& shell, std::span<const CommandResult> results)
{
    for (const auto& result : results) {
        if (result.should_log()) {
            const auto channel = command_output_channel_name(result.output_channel);
            shell.append_terminal(channel, result.message);
            shell.append_output(channel, result.message);
        }
    }
}

void refresh_command_palette(Rml::ElementDocument* doc, CommandViewState& state, const ToolsetBackend& backend)
{
    if (!doc) {
        return;
    }

    const std::string query = get_input_value(doc, "command_input");
    state.commands = backend.list_commands(query);

    std::string markup;
    for (const auto& cmd : state.commands) {
        markup += "<div class=\"nw_list_row command_item\" data-key=\"";
        markup += Rml::StringUtilities::EncodeRml(cmd.id);
        markup += "\">";
        markup += "<div class=\"nw_list_col nw_list_col_id\">" + Rml::StringUtilities::EncodeRml(cmd.id) + "</div>";
        markup += "<div class=\"nw_list_col\">" + Rml::StringUtilities::EncodeRml(cmd.title) + "</div>";
        markup += "<div class=\"nw_list_col nw_list_col_desc\">" + Rml::StringUtilities::EncodeRml(cmd.description) + "</div>";
        markup += "</div>";
    }
    if (markup.empty()) {
        markup = "<div class=\"nw_list_empty\">No matching commands.</div>";
    }

    if (auto* list = doc->GetElementById("command_list")) {
        if (auto* items = doc->GetElementById("command_list_items")) {
            items->SetInnerRML(markup);
        } else {
            list->SetInnerRML(markup);
        }
    }
    if (auto* details = doc->GetElementById("command_details")) {
        details->SetInnerRML(state.commands.empty() ? "" : Rml::StringUtilities::EncodeRml(state.commands.front().usage));
    }
}

void refresh_command_palette_query(Rml::ElementDocument* document,
    CommandViewState& state, const ToolsetBackend& backend, bool visible)
{
    const std::string query = get_input_value(document, "command_input");
    if (visible && query != state.last_command_query) {
        state.last_command_query = query;
        refresh_command_palette(document, state, backend);
    }
}

std::optional<CommandPromptAction> show_command_prompt(
    SDL_Window* window, const CommandPrompt& prompt)
{
    if (prompt.actions.size() > static_cast<size_t>(std::numeric_limits<int>::max())) { return std::nullopt; }
    std::vector<SDL_MessageBoxButtonData> buttons;
    buttons.reserve(prompt.actions.size());
    for (size_t i = 0; i < prompt.actions.size(); ++i) {
        SDL_MessageBoxButtonFlags flags = 0;
        if (prompt.actions[i].id == "save") {
            flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        }
        if (prompt.actions[i].id == "cancel") {
            flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        }
        buttons.push_back(SDL_MessageBoxButtonData{
            .flags = flags,
            .buttonID = static_cast<int>(i),
            .text = prompt.actions[i].label.c_str(),
        });
    }

    std::string message = prompt.message;
    if (!prompt.detail.empty()) {
        message += "\n\n";
        message += prompt.detail;
    }
    const SDL_MessageBoxData message_box{
        .flags = SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT,
        .window = window,
        .title = prompt.title.c_str(),
        .message = message.c_str(),
        .numbuttons = static_cast<int>(buttons.size()),
        .buttons = buttons.data(),
        .colorScheme = nullptr,
    };

    int button_id = -1;
    if (!SDL_ShowMessageBox(&message_box, &button_id)
        || button_id < 0
        || static_cast<size_t>(button_id) >= prompt.actions.size()) {
        return std::nullopt;
    }
    return prompt.actions[static_cast<size_t>(button_id)];
}

void sync_command_overlay_visibility(CommandViewState& state, bool project_load_active, bool operation_active)
{
    auto* doc = state.command_overlay_document;
    if (!doc) { return; }
    const bool active = state.command_form || project_load_active
        || operation_active;
    if (active && !doc->IsVisible()) {
        doc->Show(Rml::ModalFlag::Modal);
    } else if (!active && doc->IsVisible()) {
        doc->Hide();
    }
}

void close_command_form_combobox(CommandViewState& state)
{
    if (state.command_form_combobox_field) {
        const auto id = "command_form_field_"
            + std::to_string(*state.command_form_combobox_field);
        if (auto* field = find_el(
                state.command_overlay_document, id.c_str())) {
            field->SetClass("open", false);
        }
    }
    state.command_form_combobox.close();
    state.command_form_combobox_field.reset();
    state.command_form_combobox_placement.reset();
    if (auto* popup = find_el(state.command_overlay_document,
            "command_form_combobox_popup")) {
        popup->SetClass("active", false);
        popup->SetInnerRML("");
    }
}

bool open_command_form_combobox(CommandViewState& state, size_t field_index)
{
    if (!state.command_form
        || field_index >= state.command_form->fields.size()) {
        return false;
    }
    const auto& field = state.command_form->fields[field_index];
    if (field.choices.empty()
        || field.choices.size()
            > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }
    if (state.command_form_combobox_field == field_index
        && state.command_form_combobox.is_active()) {
        if (state.command_form_combobox.popup_visible()) {
            state.command_form_combobox.hide_popup();
        } else {
            (void)state.command_form_combobox.show_popup();
        }
        state.command_form_combobox_placement.reset();
        return true;
    }

    absl::flat_hash_set<std::string_view> values;
    values.reserve(field.choices.size());
    std::vector<nw::toolset::VirtualComboBoxItem> options;
    options.reserve(field.choices.size());
    int32_t selected = -1;
    for (size_t index = 0; index < field.choices.size(); ++index) {
        const auto& choice = field.choices[index];
        if (choice.value.empty() || !values.insert(choice.value).second) {
            return false;
        }
        const auto key = static_cast<int32_t>(index);
        options.push_back({key, choice.label, {}});
        if (choice.value == field.value) { selected = key; }
    }
    if (selected < 0) { return false; }

    close_command_form_combobox(state);
    if (!state.command_form_combobox.open(
            std::move(options), selected)) {
        return false;
    }
    state.command_form_combobox_field = field_index;
    state.command_form_combobox_placement.reset();
    return true;
}

void sync_command_form_combobox(CommandViewState& state, bool force)
{
    auto* doc = state.command_overlay_document;
    auto* popup = find_el(doc, "command_form_combobox_popup");
    if (!popup || !state.command_form
        || !state.command_form_combobox_field
        || !state.command_form_combobox.is_active()) {
        return;
    }
    const auto field_index = *state.command_form_combobox_field;
    if (field_index >= state.command_form->fields.size()) {
        close_command_form_combobox(state);
        return;
    }
    const auto field_id = "command_form_field_"
        + std::to_string(field_index);
    auto* field = find_el(doc, field_id.c_str());
    auto* bounds = find_el(doc, "command_form_overlay");
    if (!field || !bounds) {
        close_command_form_combobox(state);
        return;
    }

    const bool visible = state.command_form_combobox.popup_visible();
    field->SetClass("open", visible);
    popup->SetClass("active", visible);
    if (!visible) { return; }

    const nw::toolset::VirtualComboBoxRect anchor{
        .x = static_cast<int>(std::lround(
            field->GetAbsoluteLeft() - bounds->GetAbsoluteLeft())),
        .y = static_cast<int>(std::lround(
            field->GetAbsoluteTop() - bounds->GetAbsoluteTop())),
        .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
        .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
    };
    const nw::toolset::VirtualComboBoxRect bounds_rect{
        .width = static_cast<int>(std::lround(std::max(
            bounds->GetClientWidth(), bounds->GetOffsetWidth()))),
        .height = static_cast<int>(std::lround(std::max(
            bounds->GetClientHeight(), bounds->GetOffsetHeight()))),
    };
    const auto placement = state.command_form_combobox.place_popup(
        anchor, bounds_rect);
    if (placement.width <= 0 || placement.height <= 0) { return; }
    if (!state.command_form_combobox_placement
        || *state.command_form_combobox_placement != placement) {
        popup->SetProperty("left", std::to_string(placement.left) + "px");
        popup->SetProperty("top", std::to_string(placement.top) + "px");
        popup->SetProperty("width", std::to_string(placement.width) + "px");
        popup->SetProperty("height", std::to_string(placement.height) + "px");
        state.command_form_combobox_placement = placement;
    }

    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(popup->GetScrollTop())));
    auto update = state.command_form_combobox.update(
        placement.height, observed_scroll_top, force);
    if (update.replace_markup) {
        popup->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        popup->SetScrollTop(static_cast<float>(update.scroll_top));
    }
}

void sync_command_form(CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active, bool force)
{
    sync_command_overlay_visibility(state, project_load_active,
        backend.blueprint_operation_active() || backend.blueprint_publication_pending());
    auto* doc = state.command_overlay_document;
    auto* host = find_el(doc, "command_form_overlay");
    if (!host) { return; }
    const bool rendered = state.rendered_command_form_generation != state.command_form_generation;
    if (rendered) {
        close_command_form_combobox(state);
        state.rendered_command_form_generation = state.command_form_generation;
        host->SetClass("active", bool(state.command_form));
        if (!state.command_form) {
            host->SetInnerRML("");
            return;
        }
        const auto& form = *state.command_form;
        std::optional<size_t> close_action;
        if (form.action_list) {
            const auto cancel = std::ranges::find(
                form.actions, std::string{"cancel"},
                &nw::toolset::CommandPromptAction::id);
            if (cancel != form.actions.end()) {
                close_action = static_cast<size_t>(
                    std::distance(form.actions.begin(), cancel));
            }
        }
        std::string markup = "<div class=\"command_form";
        if (form.action_list) { markup += " command_form_action_picker"; }
        markup += "\">";
        if (close_action) {
            markup += "<div class=\"command_form_title_row\"><div class=\"command_form_title\">"
                + Rml::StringUtilities::EncodeRml(form.title)
                + "</div><button type=\"button\" class=\"command_form_action command_form_close\" title=\"Close\" id=\"command_form_action_"
                + std::to_string(*close_action) + "\" data-index=\""
                + std::to_string(*close_action)
                + "\"><span class=\"command_form_close_glyph\">&#215;</span></button></div>";
        } else {
            markup += "<div class=\"command_form_title\">"
                + Rml::StringUtilities::EncodeRml(form.title) + "</div>";
        }
        markup += "<div class=\"command_form_message\">"
            + Rml::StringUtilities::EncodeRml(form.message) + "</div>";
        for (size_t index = 0; index < form.fields.size(); ++index) {
            const auto& field = form.fields[index];
            const auto id = "command_form_field_" + std::to_string(index);
            markup += "<div class=\"command_form_row\"><label for=\"" + id + "\">" + Rml::StringUtilities::EncodeRml(field.label) + "</label>";
            if (field.choices.empty()) {
                markup += "<input type=\"text\" id=\"" + id + "\" value=\"" + Rml::StringUtilities::EncodeRml(field.value) + "\"/>";
            } else {
                const auto selected = std::ranges::find(
                    field.choices, field.value,
                    &nw::toolset::CommandPromptChoice::value);
                markup += "<button type=\"button\" id=\"" + id
                    + "\" class=\"combobox_field command_form_choice_field\" data-field=\""
                    + std::to_string(index)
                    + "\"><span class=\"combobox_value\">";
                if (selected != field.choices.end()) {
                    markup += Rml::StringUtilities::EncodeRml(selected->label);
                }
                markup += "</span><span class=\"combobox_arrow\"><span class=\"combobox_arrow_indicator\"></span></span></button>";
            }
            if (field.directory) { markup += "<button class=\"command_form_browse\">Browse...</button>"; }
            markup += "</div>";
        }
        const bool has_feedback = !form.fields.empty() || !form.detail.empty() || !form.file_suffix.empty();
        if (has_feedback) {
            markup += "<div class=\"command_form_feedback\"><div id=\"command_form_filename\"></div><div id=\"command_form_detail\">"
                + Rml::StringUtilities::EncodeRml(form.detail) + "</div><div id=\"command_form_error\"></div></div>";
        }
        markup += "<div class=\"command_form_actions";
        if (form.action_list) { markup += " command_form_action_list"; }
        markup += "\">";
        for (size_t index = 0; index < form.actions.size(); ++index) {
            if (close_action && *close_action == index) { continue; }
            markup += "<button class=\"command_form_action "
                + std::string{index == 0 ? "command_form_action_primary" : "command_form_action_secondary"}
                + "\" id=\"command_form_action_" + std::to_string(index)
                + "\" data-index=\"" + std::to_string(index) + "\">" + Rml::StringUtilities::EncodeRml(form.actions[index].label) + "</button>";
        }
        markup += "</div></div><div id=\"command_form_combobox_popup\" class=\"combobox_options combobox_popup command_form_combobox_popup\"></div>";
        host->SetInnerRML(markup);
        if (auto* input = find_el(doc, "command_form_field_0")) { input->Focus(); }
    }
    if (!state.command_form) { return; }
    auto& form = *state.command_form;
    bool changed = false;
    bool complete = true;
    for (size_t index = 0; index < form.fields.size(); ++index) {
        auto& field = form.fields[index];
        if (field.choices.empty()) {
            const auto id = "command_form_field_" + std::to_string(index);
            const auto value = get_input_value(doc, id.c_str());
            changed |= field.value != value;
            field.value = value;
        }
        const auto selected = std::ranges::find(
            field.choices, field.value,
            &nw::toolset::CommandPromptChoice::value);
        complete &= (!field.required || !field.value.empty())
            && (field.choices.empty() || selected != field.choices.end());
    }
    if (!changed && !rendered && !force) {
        sync_command_form_combobox(state);
        return;
    }
    if (changed) { form.detail.clear(); }
    std::string diagnostic;
    std::string filename;
    if (!form.file_suffix.empty() && form.fields.size() >= 2) {
        std::string normalized;
        if (nw::toolset::validate_blueprint_resref(form.fields[0].value, normalized, diagnostic)) {
            const std::array destinations{nw::toolset::BlueprintDestination{
                nw::Resource::from_filename(normalized + form.file_suffix), form.fields[1].value}};
            const auto result = nw::toolset::validate_blueprint_destinations(backend.current_project_dir(), destinations);
            diagnostic = result[0].error;
            if (!result[0].target.empty()) { filename = result[0].target.lexically_relative(backend.current_project_dir()).generic_string(); }
        }
    }
    if (auto* target = find_el(doc, "command_form_filename")) { target->SetInnerRML(Rml::StringUtilities::EncodeRml(filename)); }
    if (auto* detail = find_el(doc, "command_form_detail")) { detail->SetInnerRML(Rml::StringUtilities::EncodeRml(form.detail)); }
    if (auto* error = find_el(doc, "command_form_error")) { error->SetInnerRML(Rml::StringUtilities::EncodeRml(diagnostic)); }
    if (auto* button = find_el(doc, "command_form_action_0")) {
        const bool enabled = complete && diagnostic.empty();
        button->SetClass("disabled", !enabled);
        if (enabled) {
            button->RemoveAttribute("disabled");
        } else {
            button->SetAttribute("disabled", true);
        }
    }
    sync_command_form_combobox(state, force);
}

bool commit_command_form_combobox(CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active, int32_t choice_index)
{
    if (!state.command_form || !state.command_form_combobox_field
        || choice_index < 0) {
        return false;
    }
    const auto field_index = *state.command_form_combobox_field;
    if (field_index >= state.command_form->fields.size()) { return false; }
    auto& field = state.command_form->fields[field_index];
    const auto index = static_cast<size_t>(choice_index);
    if (index >= field.choices.size()
        || !state.command_form_combobox.select_key(choice_index)) {
        return false;
    }

    field.value = field.choices[index].value;
    state.command_form->detail.clear();
    const auto field_id = "command_form_field_" + std::to_string(field_index);
    auto* field_element = find_el(
        state.command_overlay_document, field_id.c_str());
    if (field_element) {
        if (auto* value = find_ancestor_with_class(
                field_element->GetChild(0), "combobox_value")) {
            value->SetInnerRML(Rml::StringUtilities::EncodeRml(field.choices[index].label));
        }
    }
    close_command_form_combobox(state);
    sync_command_form(state, backend, project_load_active, true);
    if (field_element) { field_element->Focus(); }
    return true;
}

void sync_blueprint_operation(CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active)
{
    sync_command_overlay_visibility(state, project_load_active,
        backend.blueprint_operation_active() || backend.blueprint_publication_pending());
    auto* doc = state.command_overlay_document;
    auto* host = find_el(doc, "blueprint_operation_overlay");
    if (!host) { return; }
    host->SetClass("active", (backend.blueprint_operation_active() || backend.blueprint_publication_pending()));
    if (!(backend.blueprint_operation_active() || backend.blueprint_publication_pending())) {
        if (!state.blueprint_operation_markup.empty()) {
            host->SetInnerRML("");
            state.blueprint_operation_markup.clear();
        }
        state.blueprint_review_page = 0;
        return;
    }
    const nw::toolset::BlueprintOperationProgress publication{.stage = "finalizing", .detail = "Blueprint saved; retry resource and document publication before editing.", .error = "Publication is pending"};
    const auto& progress = backend.blueprint_publication_pending() ? publication : backend.blueprint_progress();
    const auto& documents = backend.blueprint_updated_documents();
    const bool ready = progress.stage == "ready" && !backend.blueprint_worker_active();
    const bool restore = progress.stage == "restore_ready" || progress.stage == "recovery";
    const bool complete = progress.stage == "complete" || progress.stage == "failed";
    std::string label = progress.stage;
    const std::array<std::pair<const char*, const char*>, 16> labels{{std::pair{"starting", "Starting"}, {"resolving", "Resolving blueprint dependencies"},
        {"preparing_live", "Preparing live instances"},
        {"discovering", "Finding documents"}, {"scanning", "Scanning documents"}, {"preparing", "Preparing replacements"},
        {"ready", "Review replacements"}, {"checking", "Checking for changes"}, {"saving", "Saving documents"},
        {"saved", "Finalizing"}, {"finalizing", "Finalizing"}, {"restore_ready", "Review restoration"},
        {"restoring", "Restoring original files"}, {"recovery", "Recovery required"}, {"failed", "Update stopped"}, {"complete", "Complete"}}};
    for (const auto& [stage, title] : labels) {
        if (progress.stage == stage) {
            label = title;
            break;
        }
    }
    std::string markup = "<div class=\"command_form\"><div class=\"command_form_title\">Update Blueprint References</div><div class=\"blueprint_operation_stage\">"
        + Rml::StringUtilities::EncodeRml(label) + "</div>";
    if (!ready && !restore && !complete) {
        markup += "<div class=\"home_import_progress\"><div class=\"";
        if (progress.total) {
            const auto percent = 100.0 * static_cast<double>(progress.completed) / static_cast<double>(progress.total);
            markup += "blueprint_progress_fill\" style=\"width:" + std::to_string(std::clamp(percent, 0.0, 100.0)) + "%\"></div></div>";
            markup += "<div class=\"blueprint_operation_progress_text\">" + std::to_string(progress.completed) + " / " + std::to_string(progress.total) + " " + Rml::StringUtilities::EncodeRml(progress.unit) + "</div>";
        } else {
            markup += "home_import_progress_fill\"></div></div>";
        }
    }
    markup += "<div class=\"blueprint_operation_detail\">" + Rml::StringUtilities::EncodeRml(progress.detail) + "</div>";
    if (ready || complete) {
        markup += "<div class=\"blueprint_operation_summary\">" + std::to_string(progress.instances) + " instances, " + std::to_string(documents.size()) + " changed documents";
        if (progress.covered) { markup += "; " + std::to_string(progress.covered) + " nested matches covered by a parent replacement"; }
        if (progress.reference_uses) { markup += "; " + std::to_string(progress.reference_uses) + " blueprint reference uses already point to this blueprint"; }
        markup += "</div>";
    }
    if (ready || restore) {
        constexpr size_t page_size = 20;
        const auto pages = std::max(size_t{1}, (documents.size() + page_size - 1) / page_size);
        state.blueprint_review_page = std::min(state.blueprint_review_page, pages - 1);
        const auto start = state.blueprint_review_page * page_size;
        markup += "<div class=\"blueprint_review_documents\">";
        for (size_t index = start; index < std::min(documents.size(), start + page_size); ++index) {
            markup += "<div>" + Rml::StringUtilities::EncodeRml(documents[index].lexically_relative(backend.current_project_dir()).generic_string()) + "</div>";
        }
        markup += "</div>";
        if (pages > 1) {
            markup += "<div class=\"blueprint_operation_page_actions\"><button class=\"blueprint_operation_page\" data-delta=\"-1\">Previous</button><span>"
                + std::to_string(state.blueprint_review_page + 1) + " / " + std::to_string(pages)
                + "</span><button class=\"blueprint_operation_page\" data-delta=\"1\">Next</button></div>";
        }
    }
    if (!progress.error.empty()) { markup += "<div class=\"blueprint_operation_error\">" + Rml::StringUtilities::EncodeRml(progress.error) + "</div>"; }
    const auto button = [&](const char* command, const char* title, bool primary) {
        markup += std::string{"<button class=\"blueprint_authoring_action command_form_action "}
            + (primary ? "command_form_action_primary" : "command_form_action_secondary")
            + "\" data-command=\"" + command + "\">" + title + "</button>";
    };
    markup += "<div class=\"command_form_actions\">";
    if (ready && !documents.empty()) { button("blueprint.references.apply", "Update Instances", true); }
    if (restore) { button("blueprint.references.restore_apply", "Restore Original Files", true); }
    if (progress.stage == "finalizing" && !progress.error.empty()) { button(backend.blueprint_publication_pending() ? "blueprint.refresh" : "blueprint.references.retry", "Retry Publication", true); }
    if (progress.stage == "finalizing" && !progress.error.empty() && !backend.blueprint_publication_pending()) { button("blueprint.references.restore_apply", "Restore Original Files", false); }
    if (progress.stage != "recovery" && progress.stage != "finalizing" && progress.stage != "restoring") {
        button("blueprint.references.cancel", complete || (ready && documents.empty()) ? "Close" : "Cancel", false);
    }
    markup += "</div></div>";
    if (markup != state.blueprint_operation_markup) {
        state.blueprint_operation_markup = markup;
        host->SetInnerRML(markup);
        host->Focus();
    }
}

} // namespace nw::toolset
