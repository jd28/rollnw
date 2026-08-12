#include "rml_smalls_data_model.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace nw::toolset {

namespace {

constexpr uint32_t invalid_index = std::numeric_limits<uint32_t>::max();
constexpr uint8_t maximum_path_depth = 32;

void* encode_index(uint32_t index)
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(index) + 1);
}

bool decode_index(void* pointer, uint32_t& index)
{
    const uintptr_t encoded = reinterpret_cast<uintptr_t>(pointer);
    if (encoded == 0 || encoded - 1 > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    index = static_cast<uint32_t>(encoded - 1);
    return true;
}

const nw::smalls::Type* underlying_type(
    const nw::smalls::Runtime& runtime, nw::smalls::TypeID type_id)
{
    const nw::smalls::Type* type = runtime.get_type(type_id);
    for (uint8_t depth = 0;
         type && depth < maximum_path_depth
         && (type->type_kind == nw::smalls::TK_alias
             || type->type_kind == nw::smalls::TK_newtype);
         ++depth) {
        if (type->type_params.empty()) {
            return nullptr;
        }
        type_id = type->type_params[0].as<nw::smalls::TypeID>();
        type = runtime.get_type(type_id);
    }
    return type;
}

} // namespace

struct RmlSmallsDataModel::Impl {
    enum class StepKind : uint8_t {
        field,
        array,
    };

    struct Binding {
        std::string variable;
        std::string module;
        std::string global;
    };

    struct Node {
        uint32_t parent = invalid_index;
        uint32_t binding = invalid_index;
        uint32_t slot = invalid_index;
        StepKind kind = StepKind::field;
        uint8_t depth = 0;
    };

    class Definition final : public Rml::VariableDefinition {
    public:
        Definition(Impl& owner, bool root)
            : Rml::VariableDefinition(Rml::DataVariableType::Scalar)
            , owner_{owner}
            , root_{root}
        {
        }

        bool Get(void* pointer, Rml::Variant& variant) override
        {
            nw::smalls::Value value;
            if (!owner_.resolve(pointer, root_, value)) {
                return false;
            }
            return owner_.get_scalar(value, variant);
        }

        bool Set(void*, const Rml::Variant&) override
        {
            return false;
        }

        int Size(void* pointer) override
        {
            nw::smalls::Value value;
            if (!owner_.resolve(pointer, root_, value)) {
                return 0;
            }
            return owner_.array_size(value);
        }

        Rml::DataVariable Child(
            void* pointer, const Rml::DataAddressEntry& address) override
        {
            if (root_) {
                owner_.nodes.clear();
            }
            return owner_.child(pointer, root_, address);
        }

    private:
        Impl& owner_;
        bool root_ = false;
    };

    Impl()
        : root_definition{*this, true}
        , nested_definition{*this, false}
    {
        nodes.reserve(64);
    }

    bool current_runtime() const
    {
        auto& services = nw::kernel::services();
        return runtime
            && runtime_generation == services.generation()
            && services.get<nw::smalls::Runtime>() == runtime;
    }

    bool resolve_global(uint32_t binding_index, nw::smalls::Value& output)
    {
        if (!current_runtime() || binding_index >= bindings.size()) {
            return false;
        }

        const Binding& binding = bindings[binding_index];
        auto* script = runtime->get_module(binding.module);
        auto* module = script ? runtime->get_or_compile_module(script) : nullptr;
        if (!module) {
            return false;
        }

        const auto slot = module->global_slot_map.find(binding.global);
        if (slot == module->global_slot_map.end()
            || slot->second >= module->globals.size()) {
            return false;
        }

        output = module->globals[slot->second];
        return output.type_id != nw::smalls::invalid_type_id;
    }

    bool apply_step(
        const nw::smalls::Value& input, const Node& node, nw::smalls::Value& output)
    {
        if (!current_runtime()) {
            return false;
        }

        if (node.kind == StepKind::array) {
            if (input.storage != nw::smalls::ValueStorage::heap) {
                return false;
            }
            auto* array = runtime->get_array_typed(input.data.hptr);
            return array
                && node.slot < array->size()
                && array->get_value(node.slot, output, *runtime);
        }

        const auto* definition = runtime->get_struct_def(input.type_id);
        if (!definition || node.slot >= definition->field_count) {
            return false;
        }
        output = runtime->read_struct_value_field(input, definition, node.slot);
        return output.type_id != nw::smalls::invalid_type_id;
    }

    bool resolve_node(uint32_t node_index, nw::smalls::Value& output)
    {
        if (node_index >= nodes.size()) {
            return false;
        }

        const Node node = nodes[node_index];
        nw::smalls::Value input;
        if (node.parent == invalid_index) {
            if (!resolve_global(node.binding, input)) {
                return false;
            }
        } else if (!resolve_node(node.parent, input)) {
            return false;
        }
        return apply_step(input, node, output);
    }

    bool resolve(void* pointer, bool root, nw::smalls::Value& output)
    {
        uint32_t index = invalid_index;
        if (!decode_index(pointer, index)) {
            return false;
        }
        return root ? resolve_global(index, output) : resolve_node(index, output);
    }

    int array_size(const nw::smalls::Value& value) const
    {
        if (!current_runtime()
            || value.storage != nw::smalls::ValueStorage::heap) {
            return 0;
        }
        const auto* array = runtime->get_array_typed(value.data.hptr);
        if (!array) {
            return 0;
        }
        return static_cast<int>(std::min(
            array->size(), static_cast<size_t>(std::numeric_limits<int>::max())));
    }

    bool get_scalar(const nw::smalls::Value& value, Rml::Variant& variant) const
    {
        if (!current_runtime()) {
            return false;
        }

        const auto* type = underlying_type(*runtime, value.type_id);
        if (!type || type->type_kind != nw::smalls::TK_primitive) {
            return false;
        }

        switch (type->primitive_kind) {
        case nw::smalls::PK_int:
            variant = value.data.ival;
            return true;
        case nw::smalls::PK_float:
            variant = value.data.fval;
            return true;
        case nw::smalls::PK_bool:
            variant = value.data.bval;
            return true;
        case nw::smalls::PK_string:
            if (value.storage != nw::smalls::ValueStorage::heap) {
                return false;
            }
            variant = value.data.hptr.value == 0
                ? Rml::String{}
                : Rml::String{runtime->get_string_view(value.data.hptr)};
            return true;
        default:
            return false;
        }
    }

    Rml::DataVariable child(
        void* pointer, bool root, const Rml::DataAddressEntry& address)
    {
        uint32_t source_index = invalid_index;
        nw::smalls::Value value;
        if (!decode_index(pointer, source_index)
            || !resolve(pointer, root, value)) {
            return {};
        }

        if (address.index < 0 && address.name == "size") {
            const int size = array_size(value);
            if (size > 0
                || (value.storage == nw::smalls::ValueStorage::heap
                    && runtime->get_array_typed(value.data.hptr))) {
                return Rml::MakeLiteralIntVariable(size);
            }
            return {};
        }

        Node node;
        node.parent = root ? invalid_index : source_index;
        node.binding = root ? source_index : nodes[source_index].binding;
        node.depth = root ? 1 : static_cast<uint8_t>(nodes[source_index].depth + 1);
        if (node.depth > maximum_path_depth) {
            return {};
        }

        if (address.index >= 0) {
            if (value.storage != nw::smalls::ValueStorage::heap) {
                return {};
            }
            auto* array = runtime->get_array_typed(value.data.hptr);
            if (!array
                || static_cast<size_t>(address.index) >= array->size()) {
                return {};
            }
            node.kind = StepKind::array;
            node.slot = static_cast<uint32_t>(address.index);
        } else {
            const auto* definition = runtime->get_struct_def(value.type_id);
            if (!definition) {
                return {};
            }
            const uint32_t field = definition->field_index(address.name);
            if (field == invalid_index) {
                return {};
            }
            node.kind = StepKind::field;
            node.slot = field;
        }

        nodes.push_back(node);
        return Rml::DataVariable{
            &nested_definition, encode_index(static_cast<uint32_t>(nodes.size() - 1))};
    }

    Rml::Context* context = nullptr;
    nw::smalls::Runtime* runtime = nullptr;
    uint64_t runtime_generation = 0;
    std::string context_name;
    std::string model_name;
    std::vector<Binding> bindings;
    std::vector<Node> nodes;
    Definition root_definition;
    Definition nested_definition;
    Rml::DataModelHandle handle;
};

RmlSmallsDataModel::RmlSmallsDataModel()
    : impl_{std::make_unique<Impl>()}
{
}

RmlSmallsDataModel::~RmlSmallsDataModel()
{
    shutdown();
}

bool RmlSmallsDataModel::initialize(Rml::Context& context,
    nw::smalls::Runtime& runtime,
    std::string_view model_name,
    std::span<const RmlSmallsGlobalBinding> bindings)
{
    shutdown();
    if (model_name.empty() || bindings.empty()) {
        return false;
    }

    impl_->context = &context;
    impl_->runtime = &runtime;
    impl_->runtime_generation = nw::kernel::services().generation();
    impl_->context_name = context.GetName();
    impl_->model_name = model_name;
    impl_->bindings.reserve(bindings.size());
    for (const auto& binding : bindings) {
        if (binding.variable.empty()
            || binding.module.empty()
            || binding.global.empty()) {
            shutdown();
            return false;
        }
        impl_->bindings.push_back({
            binding.variable,
            binding.module,
            binding.global,
        });
    }
    for (uint32_t i = 0; i < impl_->bindings.size(); ++i) {
        nw::smalls::Value value;
        if (!impl_->resolve_global(i, value)) {
            shutdown();
            return false;
        }
    }

    auto constructor = context.CreateDataModel(impl_->model_name);
    if (!constructor) {
        shutdown();
        return false;
    }

    for (uint32_t i = 0; i < impl_->bindings.size(); ++i) {
        const auto& binding = impl_->bindings[i];
        if (!constructor.BindCustomDataVariable(
                binding.variable,
                Rml::DataVariable{&impl_->root_definition, encode_index(i)})) {
            shutdown();
            return false;
        }
    }

    impl_->handle = constructor.GetModelHandle();
    return static_cast<bool>(impl_->handle);
}

bool RmlSmallsDataModel::synchronize(nw::smalls::Runtime& runtime)
{
    const uint64_t generation = nw::kernel::services().generation();
    const bool changed = impl_->runtime != &runtime
        || impl_->runtime_generation != generation;
    impl_->runtime = &runtime;
    impl_->runtime_generation = generation;
    if (changed) {
        impl_->nodes.clear();
        for (uint32_t i = 0; i < impl_->bindings.size(); ++i) {
            nw::smalls::Value value;
            if (!impl_->resolve_global(i, value)) {
                return false;
            }
        }
        dirty_all();
    }
    return impl_->context && static_cast<bool>(impl_->handle);
}

void RmlSmallsDataModel::dirty_all()
{
    if (impl_->handle) {
        impl_->handle.DirtyAllVariables();
    }
}

void RmlSmallsDataModel::shutdown()
{
    if (impl_->context && !impl_->context_name.empty()
        && Rml::GetContext(impl_->context_name) == impl_->context
        && !impl_->model_name.empty()) {
        impl_->context->RemoveDataModel(impl_->model_name);
    }
    impl_->handle = {};
    impl_->nodes.clear();
    impl_->bindings.clear();
    impl_->model_name.clear();
    impl_->context_name.clear();
    impl_->runtime_generation = 0;
    impl_->runtime = nullptr;
    impl_->context = nullptr;
}

} // namespace nw::toolset
