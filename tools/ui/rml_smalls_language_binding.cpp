#include "rml_smalls_language_binding.hpp"

#include "rml_smalls_expression_binding.hpp"
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
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace nw::toolset {

namespace {

constexpr size_t kMaxSetRmlBytes = 1024 * 1024;
constexpr size_t kMaxUiCommandsPerEvent = 256;
constexpr size_t kMaxTargetsPerDocument = 256;
constexpr size_t kMaxRetainedDiagnostics = 256;

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

std::string element_source_identity(const Rml::Element& element)
{
    const Rml::Element* current = &element;
    while (current) {
        const auto source = current->GetAttribute<Rml::String>(
            "data-smalls-source", "");
        if (!source.empty()) {
            return source;
        }
        current = current->GetParentNode();
    }
    const auto* document = element.GetOwnerDocument();
    return document ? document_identity(*document) : "<rml>";
}

} // namespace

struct RmlSmallsLanguageBinding::Impl {
    struct HandlerTarget {
        std::string module_path;
        std::string function_name;
        nw::smalls::BytecodeModule* module = nullptr;
        const nw::smalls::CompiledFunction* function = nullptr;
        uint64_t runtime_generation = 0;

        [[nodiscard]] bool valid() const noexcept { return module && function; }
    };

    struct BoundHandler {
        uint32_t target_index = std::numeric_limits<uint32_t>::max();
        std::vector<RmlSmallsBoundArgument> arguments;
        std::string source_path;
        std::string expression;
        uint64_t runtime_generation = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return target_index != std::numeric_limits<uint32_t>::max();
        }
    };

    struct DispatchRow {
        const BoundHandler* handler = nullptr;
        Rml::Event* event = nullptr;
        Rml::Element* element = nullptr;
        Rml::ElementDocument* document = nullptr;
    };

    class Document final : public Rml::ElementDocument {
    public:
        Document(const Rml::String& element_tag, Impl& owner)
            : ElementDocument(element_tag)
            , owner_(&owner)
        {
        }

        void LoadInlineScript(const Rml::String& content,
            const Rml::String& source_path, int source_line) override
        {
            try {
                if (has_inline_script_) {
                    owner_->report(source_path
                        + ": error: an RML document may contain only one inline Smalls import block");
                    return;
                }
                has_inline_script_ = true;
                import_source_ = content;
                import_source_path_ = source_path;
                import_source_line_ = source_line > 0
                    ? static_cast<size_t>(source_line)
                    : 1;
                bind_import_scope();
            } catch (const std::bad_alloc&) {
                owner_->report_noexcept(
                    "<rml>: error: out of memory while binding inline Smalls imports");
            } catch (const std::exception& exception) {
                owner_->report_exception_noexcept(
                    "<rml>: error while binding inline Smalls imports: ",
                    exception);
            } catch (...) {
                owner_->report_noexcept(
                    "<rml>: error: unknown failure while binding inline Smalls imports");
            }
        }

        void LoadExternalScript(const Rml::String& source_path) override
        {
            try {
                owner_->report(source_path
                    + ": error: external Smalls scripts are not supported; use one inline imports-only block");
            } catch (...) {
                owner_->report_noexcept(
                    "<rml>: error: external Smalls scripts are not supported");
            }
        }

        [[nodiscard]] bool has_import_scope() const noexcept
        {
            return has_inline_script_;
        }

        BoundHandler bind_handler(std::string_view expression,
            const Rml::Element& element)
        {
            if (!owner_->runtime_is_current() || !ensure_import_scope()) {
                if (!scope_error_.empty()) {
                    report_bind_error(element, expression, scope_error_);
                } else {
                    report_bind_error(element, expression,
                        "document has no inline Smalls import block");
                }
                return {};
            }

            RmlSmallsResolvedCall resolved;
            std::string error;
            if (!resolve_rml_smalls_call(
                    *owner_->runtime, import_scope_, expression, resolved, error)) {
                report_bind_error(element, expression, error);
                return {};
            }

            const auto found = std::find_if(targets_.begin(), targets_.end(),
                [&resolved](const HandlerTarget& target) {
                    return target.module_path == resolved.module_path
                        && target.function_name == resolved.function_name;
                });
            uint32_t target_index = 0;
            if (found == targets_.end()) {
                if (targets_.size() >= kMaxTargetsPerDocument) {
                    report_bind_error(element, expression,
                        "document exceeds the 256-target limit");
                    return {};
                }
                target_index = static_cast<uint32_t>(targets_.size());
                targets_.push_back({
                    .module_path = std::move(resolved.module_path),
                    .function_name = std::move(resolved.function_name),
                    .module = resolved.module,
                    .function = resolved.function,
                    .runtime_generation = owner_->runtime_generation,
                });
                ++owner_->binding_stats.interned_target_count;
            } else {
                target_index = static_cast<uint32_t>(
                    std::distance(targets_.begin(), found));
            }

            ++owner_->binding_stats.bound_listener_count;
            owner_->binding_stats.bound_argument_count += resolved.arguments.size();
            return {
                .target_index = target_index,
                .arguments = std::move(resolved.arguments),
                .source_path = element_source_identity(element),
                .expression = std::string{expression},
                .runtime_generation = owner_->runtime_generation,
            };
        }

        [[nodiscard]] const HandlerTarget* target(uint32_t index) const noexcept
        {
            return index < targets_.size() ? &targets_[index] : nullptr;
        }

    private:
        bool ensure_import_scope()
        {
            if (!has_inline_script_) {
                return false;
            }
            if (scope_generation_ != owner_->runtime_generation) {
                bind_import_scope();
            }
            return scope_valid_;
        }

        void bind_import_scope()
        {
            import_scope_ = {};
            targets_.clear();
            scope_error_.clear();
            scope_valid_ = parse_rml_smalls_import_scope(*owner_->runtime,
                import_source_, import_scope_, scope_error_);
            scope_generation_ = owner_->runtime_generation;
            if (!scope_valid_) {
                owner_->report(import_source_path_ + ":"
                    + std::to_string(import_source_line_)
                    + ": error: " + scope_error_);
            }
        }

        void report_bind_error(const Rml::Element& element,
            std::string_view expression, std::string_view error)
        {
            const auto element_id = element.GetId().empty()
                ? std::string{"<unnamed>"}
                : std::string{element.GetId()};
            owner_->report(element_source_identity(element)
                + ": error: element '" + element_id
                + "' Smalls expression '" + std::string{expression}
                + "': " + std::string{error});
        }

        Impl* owner_ = nullptr;
        bool has_inline_script_ = false;
        bool scope_valid_ = false;
        uint64_t scope_generation_ = 0;
        size_t import_source_line_ = 1;
        std::string import_source_;
        std::string import_source_path_;
        std::string scope_error_;
        RmlSmallsImportScope import_scope_;
        std::vector<HandlerTarget> targets_;
    };

    class DocumentInstancer final : public Rml::ElementInstancer {
    public:
        explicit DocumentInstancer(Impl& owner)
            : owner_(&owner)
        {
        }

        Rml::ElementPtr InstanceElement(Rml::Element*, const Rml::String& tag, const Rml::XMLAttributes&) override
        {
            try {
                return Rml::ElementPtr(new Document(tag, *owner_));
            } catch (const std::bad_alloc&) {
                owner_->report_noexcept(
                    "<rml>: error: out of memory while creating a Smalls document");
            } catch (const std::exception& exception) {
                owner_->report_exception_noexcept(
                    "<rml>: error while creating a Smalls document: ", exception);
            } catch (...) {
                owner_->report_noexcept(
                    "<rml>: error: unknown failure while creating a Smalls document");
            }
            return nullptr;
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
        Listener(Impl& owner, std::string_view expression, Rml::Element* element)
            : owner_(&owner)
            , element_(element)
            , expression_(expression)
        {
            auto* owner_document = element_
                ? dynamic_cast<Document*>(element_->GetOwnerDocument())
                : nullptr;
            if (owner_document && owner_document->has_import_scope()) {
                attempted_ = true;
                handler_ = owner_document->bind_handler(expression_, *element_);
            }
        }

        void ProcessEvent(Rml::Event& event) override
        {
            try {
                if (!element_ || !owner_->runtime_is_current()) {
                    return;
                }
                if (handler_.runtime_generation != owner_->runtime_generation) {
                    handler_ = {};
                    attempted_ = false;
                }
                if (!attempted_) {
                    attempted_ = true;
                    auto* owner_document = dynamic_cast<Document*>(
                        element_->GetOwnerDocument());
                    if (!owner_document) {
                        owner_->report(
                            "<rml>: error: Smalls listener has no owning document");
                        return;
                    }
                    handler_ = owner_document->bind_handler(expression_, *element_);
                }
                if (!handler_.valid()) {
                    return;
                }
                auto* document = element_->GetOwnerDocument();
                const std::array rows{
                    DispatchRow{&handler_, &event, element_, document}};
                owner_->dispatch_events(rows);
            } catch (const std::bad_alloc&) {
                owner_->report_noexcept(
                    "<rml>: error: out of memory while dispatching a Smalls listener");
            } catch (const std::exception& exception) {
                owner_->report_exception_noexcept(
                    "<rml>: error while dispatching a Smalls listener: ",
                    exception);
            } catch (...) {
                owner_->report_noexcept(
                    "<rml>: error: unknown failure while dispatching a Smalls listener");
            }
        }

        void OnDetach(Rml::Element*) override
        {
            delete this;
        }

    private:
        Impl* owner_ = nullptr;
        Rml::Element* element_ = nullptr;
        std::string expression_;
        BoundHandler handler_;
        bool attempted_ = false;
    };

    class ListenerInstancer final : public Rml::EventListenerInstancer {
    public:
        explicit ListenerInstancer(Impl& owner)
            : owner_(&owner)
        {
        }

        Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element* element) override
        {
            try {
                return new Listener(*owner_, value, element);
            } catch (const std::bad_alloc&) {
                owner_->report_noexcept(
                    "<rml>: error: out of memory while binding a Smalls listener");
            } catch (const std::exception& exception) {
                owner_->report_exception_noexcept(
                    "<rml>: error while binding a Smalls listener: ",
                    exception);
            } catch (...) {
                owner_->report_noexcept(
                    "<rml>: error: unknown failure while binding a Smalls listener");
            }
            return nullptr;
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
        if (diagnostics.size() < kMaxRetainedDiagnostics) {
            diagnostics.push_back({std::move(message)});
        } else if (binding_stats.suppressed_diagnostic_count
            != std::numeric_limits<uint64_t>::max()) {
            ++binding_stats.suppressed_diagnostic_count;
        }
    }

    void report_noexcept(std::string_view message) noexcept
    {
        try {
            report(std::string{message});
        } catch (...) {
            std::fputs("RML Smalls binding failure\n", stderr);
        }
    }

    void report_exception_noexcept(
        std::string_view context, const std::exception& exception) noexcept
    {
        try {
            report(std::string{context} + exception.what());
        } catch (...) {
            std::fputs("RML Smalls binding failure\n", stderr);
        }
    }

    nw::smalls::Value encode_event(const DispatchRow& row,
        nw::ObjectHandle active_object, nw::smalls::Runtime::ScopedRoots& roots)
    {
        const auto event_type = runtime->type_id("core.rmlui.Event", false);
        const auto ptr = runtime->alloc_struct(event_type);
        if (ptr.value == 0) {
            return {};
        }
        const auto event_value = nw::smalls::Value::make_heap(ptr, event_type);
        roots.add(event_value);

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
        return event_value;
    }

    bool decode_commands(const nw::smalls::Value& value,
        nw::smalls::Runtime::ScopedRoots& roots,
        std::vector<UiCommandRow>& commands, std::string& error)
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
            roots.add(item);

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

    // Runtime-owned compiled pointers are valid only for one kernel generation.
    // Listeners retain table indices, so growing a document's target table cannot
    // invalidate their references. The common dispatch path is one indexed target
    // read followed by a linear materialization of at most 16 bound arguments.
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

            auto* owner_document = dynamic_cast<Document*>(row.document);
            const auto* target = owner_document
                ? owner_document->target(row.handler->target_index)
                : nullptr;
            if (!target || !target->valid()
                || target->runtime_generation != runtime_generation) {
                report(row.handler->source_path
                    + ": error: stale Smalls handler target");
                continue;
            }

            nw::Vector<nw::smalls::Value> arguments;
            arguments.reserve(row.handler->arguments.size());
            nw::smalls::Runtime::ScopedRoots roots{
                *runtime, row.handler->arguments.size() + 1};
            nw::smalls::Value event_value;
            bool event_encoded = false;
            bool arguments_valid = true;
            for (const auto& bound : row.handler->arguments) {
                if (bound.kind == RmlSmallsArgumentKind::event) {
                    if (!event_encoded) {
                        event_value = encode_event(row,
                            smalls_rmlui_host().active_object(), roots);
                        event_encoded = true;
                    }
                    if (event_value.type_id == nw::smalls::invalid_type_id) {
                        arguments_valid = false;
                        break;
                    }
                    arguments.push_back(event_value);
                    continue;
                }

                auto value = materialize_rml_smalls_argument(*runtime, bound);
                if (value.type_id == nw::smalls::invalid_type_id) {
                    arguments_valid = false;
                    break;
                }
                if (value.storage == nw::smalls::ValueStorage::heap
                    && value.data.hptr.value != 0) {
                    roots.add(value);
                }
                arguments.push_back(value);
            }
            if (!arguments_valid) {
                report(row.handler->source_path
                    + ": error: failed to materialize Smalls handler arguments");
                continue;
            }

            const auto result = runtime->execute_compiled(
                target->module, target->function, arguments);
            if (!result.ok()) {
                report(format_smalls_execution_error(result,
                    row.handler->source_path, 1));
                continue;
            }
            roots.add(result.value);

            std::string error;
            if (!decode_commands(result.value, roots, commands, error)
                || !apply_commands(*row.document, commands, error)) {
                report(row.handler->source_path + ": error: " + error);
            }
        }
    }

    nw::smalls::Runtime* runtime = nullptr;
    uint64_t runtime_generation = 0;
    RmlSmallsBindingStats binding_stats;
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
    try {
        return impl_->initialize(runtime);
    } catch (const std::bad_alloc&) {
        impl_->report_noexcept(
            "<rml>: error: out of memory while initializing the Smalls binding");
    } catch (const std::exception& exception) {
        impl_->report_exception_noexcept(
            "<rml>: error while initializing the Smalls binding: ", exception);
    } catch (...) {
        impl_->report_noexcept(
            "<rml>: error: unknown failure while initializing the Smalls binding");
    }
    return false;
}

bool RmlSmallsLanguageBinding::initialized() const noexcept
{
    return impl_->runtime_is_current();
}

const std::vector<RmlSmallsDiagnostic>& RmlSmallsLanguageBinding::diagnostics() const noexcept
{
    return impl_->diagnostics;
}

RmlSmallsBindingStats RmlSmallsLanguageBinding::stats() const noexcept
{
    return impl_->binding_stats;
}

void RmlSmallsLanguageBinding::clear_diagnostics()
{
    impl_->diagnostics.clear();
}

void RmlSmallsLanguageBinding::refresh_elements(Rml::ElementDocument* document)
{
    try {
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
    } catch (const std::bad_alloc&) {
        impl_->report_noexcept(
            "<rml>: error: out of memory while refreshing Smalls elements");
    } catch (const std::exception& exception) {
        impl_->report_exception_noexcept(
            "<rml>: error while refreshing Smalls elements: ", exception);
    } catch (...) {
        impl_->report_noexcept(
            "<rml>: error: unknown failure while refreshing Smalls elements");
    }
}

} // namespace nw::toolset
