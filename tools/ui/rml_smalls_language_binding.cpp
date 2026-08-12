#include "rml_smalls_language_binding.hpp"

#include "smalls_diagnostics.hpp"
#include "smalls_rmlui.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/EventListenerInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace nw::toolset {

namespace {

constexpr size_t kMaxExternalScriptBytes = 1024 * 1024;
constexpr size_t kMaxSetRmlBytes = 1024 * 1024;
constexpr size_t kMaxUiCommandsPerEvent = 256;

enum class UiCommandOperation : int32_t {
    set_text = 0,
    set_value = 1,
    set_checked = 2,
    set_class = 3,
    set_visible = 4,
    focus = 5,
    set_rml = 6,
};

struct UiCommandRow {
    UiCommandOperation operation = UiCommandOperation::set_text;
    std::string element_id;
    std::string value;
    bool state = false;
};

bool valid_handler_name(std::string_view name)
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

nw::smalls::Value make_string(nw::smalls::Runtime& runtime, std::string_view value)
{
    return nw::smalls::Value::make_string(runtime.alloc_string(value));
}

bool read_string_field(nw::smalls::Runtime& runtime, const nw::smalls::Value& value,
    std::string_view field, std::string& out)
{
    if (value.storage != nw::smalls::ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const auto field_value = runtime.read_struct_field(value.data.hptr, value.type_id, field);
    if (field_value.type_id != runtime.string_type()
        || field_value.storage != nw::smalls::ValueStorage::heap
        || field_value.data.hptr.value == 0) {
        return false;
    }
    out = runtime.get_string_view(field_value.data.hptr);
    return true;
}

bool read_int_field(nw::smalls::Runtime& runtime, const nw::smalls::Value& value,
    std::string_view field, int32_t& out)
{
    if (value.storage != nw::smalls::ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const auto field_value = runtime.read_struct_field(value.data.hptr, value.type_id, field);
    if (field_value.type_id != runtime.int_type()) {
        return false;
    }
    out = field_value.data.ival;
    return true;
}

bool read_bool_field(nw::smalls::Runtime& runtime, const nw::smalls::Value& value,
    std::string_view field, bool& out)
{
    if (value.storage != nw::smalls::ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const auto field_value = runtime.read_struct_field(value.data.hptr, value.type_id, field);
    if (field_value.type_id != runtime.bool_type()) {
        return false;
    }
    out = field_value.data.bval;
    return true;
}

std::string document_identity(const Rml::ElementDocument& document)
{
    if (!document.GetId().empty()) {
        return document.GetId();
    }
    return document.GetSourceURL();
}

} // namespace

struct RmlSmallsLanguageBinding::Impl {
    struct CompiledBlock {
        std::string module_path;
        std::string source_path;
        std::string source;
        size_t first_source_line = 1;
        nw::smalls::BytecodeModule* module = nullptr;
        uint64_t runtime_generation = 0;
    };

    struct HandlerIdentity {
        nw::smalls::BytecodeModule* module = nullptr;
        const nw::smalls::CompiledFunction* function = nullptr;
        std::string source_path;
        size_t first_source_line = 1;
        uint64_t runtime_generation = 0;

        [[nodiscard]] bool valid() const noexcept { return module && function; }
    };

    struct DispatchRow {
        const HandlerIdentity* handler = nullptr;
        Rml::Event* event = nullptr;
        Rml::Element* element = nullptr;
        Rml::ElementDocument* document = nullptr;
    };

    class Document final : public Rml::ElementDocument {
    public:
        Document(const Rml::String& element_tag, Impl& owner, uint64_t document_id)
            : ElementDocument(element_tag)
            , owner_(&owner)
            , id_(document_id)
        {
        }

        ~Document() override
        {
            if (!owner_ || !owner_->runtime_is_current()) {
                return;
            }
            for (const auto& compiled_block : blocks_) {
                if (compiled_block.runtime_generation == owner_->runtime_generation) {
                    owner_->runtime->evict_module(compiled_block.module_path);
                }
            }
        }

        void LoadInlineScript(const Rml::String& content, const Rml::String& source_path, int source_line) override
        {
            compile_block(content, source_path, source_line > 0 ? static_cast<size_t>(source_line) : 1);
        }

        void LoadExternalScript(const Rml::String& source_path) override
        {
            auto* files = Rml::GetFileInterface();
            if (!files) {
                owner_->report(source_path + ": error: RmlUi file interface unavailable");
                return;
            }

            const Rml::FileHandle file = files->Open(source_path);
            if (!file) {
                owner_->report(source_path + ": error: failed to open external Smalls script");
                return;
            }

            const size_t length = files->Length(file);
            if (length > kMaxExternalScriptBytes) {
                files->Close(file);
                owner_->report(source_path + ": error: external Smalls script exceeds 1 MiB limit");
                return;
            }

            std::string source(length, '\0');
            const size_t read = length == 0 ? 0 : files->Read(source.data(), length, file);
            files->Close(file);
            if (read != length) {
                owner_->report(source_path + ": error: failed to read complete external Smalls script");
                return;
            }
            compile_block(source, source_path, 1);
        }

        HandlerIdentity resolve_handler(std::string_view name)
        {
            if (!owner_->runtime_is_current() || !bind_blocks_to_current_runtime()) {
                return {};
            }
            if (!valid_handler_name(name)) {
                owner_->report(document_identity(*this) + ": error: invalid Smalls handler identifier '" + std::string{name} + "'");
                return {};
            }

            const auto event_type = owner_->runtime->type_id("core.rmlui.Event", false);
            for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
                const auto* function = it->module->get_function(name);
                if (!function) {
                    continue;
                }

                if (function->param_count != 1
                    || owner_->runtime->get_function_param_count(function->function_type) != 1
                    || owner_->runtime->get_function_param_type(function->function_type, 0) != event_type) {
                    owner_->report(it->source_path + ": error: Smalls RML handler '" + std::string{name}
                        + "' must take one core.rmlui.Event parameter");
                    return {};
                }

                const auto command_type = owner_->runtime->type_id("core.rmlui.Command", false);
                const auto* return_type = owner_->runtime->get_type(function->return_type);
                const bool valid_return = function->return_type == owner_->runtime->void_type()
                    || (return_type
                        && return_type->type_kind == nw::smalls::TK_array
                        && !return_type->type_params.empty()
                        && return_type->type_params[0].is<nw::smalls::TypeID>()
                        && return_type->type_params[0].as<nw::smalls::TypeID>() == command_type);
                if (!valid_return) {
                    owner_->report(it->source_path + ": error: Smalls RML handler '" + std::string{name}
                        + "' must return void or array!(core.rmlui.Command)");
                    return {};
                }

                return HandlerIdentity{
                    it->module,
                    function,
                    it->source_path,
                    it->first_source_line,
                    it->runtime_generation,
                };
            }

            owner_->report(document_identity(*this) + ": error: Smalls RML handler not found: " + std::string{name});
            return {};
        }

    private:
        bool bind_blocks_to_current_runtime()
        {
            for (auto& compiled_block : blocks_) {
                if (compiled_block.runtime_generation != owner_->runtime_generation
                    && !compile_block(compiled_block)) {
                    return false;
                }
            }
            return true;
        }

        void compile_block(std::string_view source, std::string_view source_path, size_t first_source_line)
        {
            CompiledBlock compiled_block{
                .module_path = "toolset.rml.document_" + std::to_string(id_)
                    + "_block_" + std::to_string(blocks_.size()),
                .source_path = std::string{source_path},
                .source = std::string{source},
                .first_source_line = first_source_line,
            };
            if (compile_block(compiled_block)) {
                blocks_.push_back(std::move(compiled_block));
            }
        }

        bool compile_block(CompiledBlock& compiled_block)
        {
            compiled_block.module = nullptr;
            compiled_block.runtime_generation = 0;
            auto* script = owner_->runtime->load_module_from_source(
                compiled_block.module_path, compiled_block.source);
            if (!script) {
                owner_->report(compiled_block.source_path + ": error: failed to create Smalls module");
                return false;
            }

            if (script->errors() != 0) {
                for (const auto& diagnostic : script->diagnostics()) {
                    owner_->report(format_smalls_diagnostic(
                        diagnostic, compiled_block.source_path,
                        compiled_block.first_source_line));
                }
                owner_->runtime->evict_module(compiled_block.module_path);
                return false;
            }

            auto* module = owner_->runtime->get_or_compile_module(script);
            if (!module) {
                if (script->diagnostics().empty()) {
                    owner_->report(compiled_block.source_path + ": error: failed to compile Smalls module");
                } else {
                    for (const auto& diagnostic : script->diagnostics()) {
                        owner_->report(format_smalls_diagnostic(
                            diagnostic, compiled_block.source_path,
                            compiled_block.first_source_line));
                    }
                }
                owner_->runtime->evict_module(compiled_block.module_path);
                return false;
            }

            compiled_block.module = module;
            compiled_block.runtime_generation = owner_->runtime_generation;
            return true;
        }

        Impl* owner_ = nullptr;
        uint64_t id_ = 0;
        std::vector<CompiledBlock> blocks_;
    };

    class DocumentInstancer final : public Rml::ElementInstancer {
    public:
        explicit DocumentInstancer(Impl& owner)
            : owner_(&owner)
        {
        }

        Rml::ElementPtr InstanceElement(Rml::Element*, const Rml::String& tag, const Rml::XMLAttributes&) override
        {
            return Rml::ElementPtr(new Document(tag, *owner_, owner_->next_document_id++));
        }

        void ReleaseElement(Rml::Element* element) override
        {
            delete element;
        }

    private:
        Impl* owner_ = nullptr;
    };

    class Listener final : public Rml::EventListener {
    public:
        Listener(Impl& owner, std::string_view handler, Rml::Element* element)
            : owner_(&owner)
            , element_(element)
            , handler_name_(handler)
        {
        }

        void ProcessEvent(Rml::Event& event) override
        {
            if (!element_ || !owner_->runtime_is_current()) {
                return;
            }
            if (resolved_ && handler_.runtime_generation != owner_->runtime_generation) {
                handler_ = {};
                resolved_ = false;
            }
            if (!resolved_) {
                resolved_ = true;
                auto* owner_document = dynamic_cast<Document*>(element_->GetOwnerDocument());
                if (!owner_document) {
                    owner_->report("<rml>: error: Smalls listener has no owning document");
                    return;
                }
                handler_ = owner_document->resolve_handler(handler_name_);
            }
            if (!handler_.valid()) {
                return;
            }
            auto* document = element_->GetOwnerDocument();
            const std::array rows{DispatchRow{&handler_, &event, element_, document}};
            owner_->dispatch_events(rows);
        }

        void OnDetach(Rml::Element*) override
        {
            delete this;
        }

    private:
        Impl* owner_ = nullptr;
        Rml::Element* element_ = nullptr;
        std::string handler_name_;
        HandlerIdentity handler_;
        bool resolved_ = false;
    };

    class ListenerInstancer final : public Rml::EventListenerInstancer {
    public:
        explicit ListenerInstancer(Impl& owner)
            : owner_(&owner)
        {
        }

        Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element* element) override
        {
            return new Listener(*owner_, value, element);
        }

    private:
        Impl* owner_ = nullptr;
    };

    bool initialize(nw::smalls::Runtime& new_runtime)
    {
        const uint64_t new_generation = nw::kernel::services().generation();
        if (runtime == &new_runtime && runtime_generation == new_generation) {
            return true;
        }
        if (new_runtime.type_id("core.rmlui.Event", false) == nw::smalls::invalid_type_id
            || new_runtime.type_id("core.rmlui.Command", false) == nw::smalls::invalid_type_id) {
            report("<rml>: error: core.rmlui Smalls module is not loaded");
            return false;
        }

        runtime = &new_runtime;
        runtime_generation = new_generation;
        if (!document_instancer) {
            document_instancer = std::make_unique<DocumentInstancer>(*this);
            listener_instancer = std::make_unique<ListenerInstancer>(*this);
            Rml::Factory::RegisterElementInstancer("body", document_instancer.get());
            Rml::Factory::RegisterEventListenerInstancer(listener_instancer.get());
        }
        return true;
    }

    [[nodiscard]] bool runtime_is_current() const noexcept
    {
        return runtime
            && runtime_generation == nw::kernel::services().generation()
            && nw::kernel::services().get<nw::smalls::Runtime>() == runtime;
    }

    void report(std::string message)
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", message.c_str());
        LOG_F(ERROR, "[rml-smalls] {}", message);
        diagnostics.push_back({std::move(message)});
    }

    nw::smalls::Value encode_event(const DispatchRow& row, nw::ObjectHandle active_object)
    {
        const auto event_type = runtime->type_id("core.rmlui.Event", false);
        const auto ptr = runtime->alloc_struct(event_type);
        if (ptr.value == 0) {
            return {};
        }

        std::string value;
        bool checked = false;
        if (const auto* control = dynamic_cast<const Rml::ElementFormControl*>(row.element)) {
            value = control->GetValue();
        }
        if (const auto* input = dynamic_cast<const Rml::ElementFormControlInput*>(row.element)) {
            checked = input->HasAttribute("checked");
        }

        const auto active_value = nw::smalls::Value::make_object(active_object);

        if (!runtime->write_struct_field(ptr, event_type, "event_type", make_string(*runtime, row.event->GetType()))
            || !runtime->write_struct_field(ptr, event_type, "element_id", make_string(*runtime, row.element->GetId()))
            || !runtime->write_struct_field(ptr, event_type, "document_id", make_string(*runtime, document_identity(*row.document)))
            || !runtime->write_struct_field(ptr, event_type, "text", make_string(*runtime, row.element->GetInnerRML()))
            || !runtime->write_struct_field(ptr, event_type, "value", make_string(*runtime, value))
            || !runtime->write_struct_field(ptr, event_type, "checked", nw::smalls::Value::make_bool(checked))
            || !runtime->write_struct_field(ptr, event_type, "active_object", active_value)) {
            return {};
        }
        return nw::smalls::Value::make_heap(ptr, event_type);
    }

    bool decode_commands(const nw::smalls::Value& value, std::vector<UiCommandRow>& commands, std::string& error)
    {
        commands.clear();
        if (value.type_id == runtime->void_type()) {
            return true;
        }
        if (value.storage != nw::smalls::ValueStorage::heap || value.data.hptr.value == 0) {
            error = "handler must return void or array!(core.rmlui.Command)";
            return false;
        }

        auto* array = runtime->get_array_typed(value.data.hptr);
        if (!array) {
            error = "handler returned a non-array value";
            return false;
        }
        if (array->size() > kMaxUiCommandsPerEvent) {
            error = "handler returned more than 256 UI commands";
            return false;
        }

        commands.reserve(array->size());
        for (size_t i = 0; i < array->size(); ++i) {
            nw::smalls::Value item;
            if (!array->get_value(i, item, *runtime)) {
                error = "failed to read UI command row";
                return false;
            }

            int32_t operation = -1;
            UiCommandRow command;
            if (!read_int_field(*runtime, item, "operation", operation)
                || operation < static_cast<int32_t>(UiCommandOperation::set_text)
                || operation > static_cast<int32_t>(UiCommandOperation::set_rml)
                || !read_string_field(*runtime, item, "element_id", command.element_id)
                || !read_string_field(*runtime, item, "value", command.value)
                || !read_bool_field(*runtime, item, "state", command.state)) {
                error = "invalid core.rmlui.Command row";
                return false;
            }
            command.operation = static_cast<UiCommandOperation>(operation);
            if (command.operation == UiCommandOperation::set_rml
                && command.value.size() > kMaxSetRmlBytes) {
                error = "set_rml payload exceeds 1 MiB";
                return false;
            }
            commands.push_back(std::move(command));
        }
        return true;
    }

    bool apply_commands(Rml::ElementDocument& document, std::span<const UiCommandRow> commands, std::string& error)
    {
        for (const auto& command : commands) {
            if (command.element_id.empty()) {
                error = "UI command element_id must not be empty";
                return false;
            }
            auto* element = document.GetElementById(command.element_id);
            if (!element) {
                error = "UI command target not found: " + command.element_id;
                return false;
            }

            switch (command.operation) {
            case UiCommandOperation::set_text:
                element->SetInnerRML(Rml::StringUtilities::EncodeRml(command.value));
                break;
            case UiCommandOperation::set_value: {
                auto* control = dynamic_cast<Rml::ElementFormControl*>(element);
                if (!control) {
                    error = "set_value target is not a form control: " + command.element_id;
                    return false;
                }
                control->SetValue(command.value);
                break;
            }
            case UiCommandOperation::set_checked: {
                auto* input = dynamic_cast<Rml::ElementFormControlInput*>(element);
                if (!input) {
                    error = "set_checked target is not an input: " + command.element_id;
                    return false;
                }
                if (command.state) {
                    input->SetAttribute("checked", true);
                } else {
                    input->RemoveAttribute("checked");
                }
                break;
            }
            case UiCommandOperation::set_class:
                if (command.value.empty()) {
                    error = "set_class requires a class name";
                    return false;
                }
                element->SetClass(command.value, command.state);
                break;
            case UiCommandOperation::set_visible:
                element->SetProperty("visibility", command.state ? "visible" : "hidden");
                break;
            case UiCommandOperation::focus:
                if (command.state) {
                    element->Focus();
                } else {
                    element->Blur();
                }
                break;
            case UiCommandOperation::set_rml:
                element->SetInnerRML(command.value);
                break;
            }
        }
        return true;
    }

    // Runtime-owned compiled pointers are retained only for the document lifetime.
    // This hot path avoids a hash lookup on every event; document eviction occurs
    // after its listeners can no longer receive later UI dispatch.
    void dispatch_events(std::span<const DispatchRow> rows)
    {
        if (!runtime_is_current()) {
            return;
        }
        std::vector<UiCommandRow> commands;
        for (const auto& row : rows) {
            if (!row.handler || !row.handler->valid()
                || row.handler->runtime_generation != runtime_generation
                || !row.event || !row.element || !row.document) {
                report("<rml>: error: invalid Smalls event dispatch row");
                continue;
            }

            const nw::ObjectHandle active_object = smalls_rmlui_host().active_object();
            if (active_object.type == nw::ObjectType::invalid) {
                report(document_identity(*row.document) + ": error: no active object");
                continue;
            }

            const auto event_value = encode_event(row, active_object);
            if (event_value.type_id == nw::smalls::invalid_type_id) {
                report(document_identity(*row.document) + ": error: failed to encode Smalls RML event");
                continue;
            }

            const auto result = runtime->execute_compiled(row.handler->module, row.handler->function, {event_value});
            if (!result.ok()) {
                report(format_smalls_execution_error(result,
                    row.handler->source_path, row.handler->first_source_line));
                continue;
            }

            std::string error;
            if (!decode_commands(result.value, commands, error)
                || !apply_commands(*row.document, commands, error)) {
                report(row.handler->source_path + ": error: " + error);
            }
        }
    }

    nw::smalls::Runtime* runtime = nullptr;
    uint64_t runtime_generation = 0;
    uint64_t next_document_id = 1;
    std::vector<RmlSmallsDiagnostic> diagnostics;
    std::unique_ptr<DocumentInstancer> document_instancer;
    std::unique_ptr<ListenerInstancer> listener_instancer;
};

RmlSmallsLanguageBinding::RmlSmallsLanguageBinding()
    : impl_(std::make_unique<Impl>())
{
}

RmlSmallsLanguageBinding::~RmlSmallsLanguageBinding() = default;

bool RmlSmallsLanguageBinding::initialize(nw::smalls::Runtime& runtime)
{
    return impl_->initialize(runtime);
}

bool RmlSmallsLanguageBinding::initialized() const noexcept
{
    return impl_->runtime_is_current();
}

const std::vector<RmlSmallsDiagnostic>& RmlSmallsLanguageBinding::diagnostics() const noexcept
{
    return impl_->diagnostics;
}

void RmlSmallsLanguageBinding::clear_diagnostics()
{
    impl_->diagnostics.clear();
}

void RmlSmallsLanguageBinding::refresh_elements(Rml::ElementDocument* document)
{
    if (!document || !impl_->runtime_is_current()) {
        return;
    }
    Rml::ElementList elements;
    document->GetElementsByClassName(elements, "smalls_refresh");
    std::vector<Rml::String> element_ids;
    element_ids.reserve(elements.size());
    for (auto* element : elements) {
        if (element && !element->GetId().empty()) {
            element_ids.push_back(element->GetId());
        }
    }
    for (const auto& element_id : element_ids) {
        auto* element = document->GetElementById(element_id);
        if (element && element->IsClassSet("smalls_refresh")) {
            element->DispatchEvent("refresh", {});
        }
    }
}

} // namespace nw::toolset
