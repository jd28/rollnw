#pragma once

#include "../smalls/UnmanagedArray.hpp"
#include "../util/HandlePool.hpp"
#include "../util/string.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace nw {

// == RuntimeObject ===========================================================
// ============================================================================

/// Base runtime object (identity only)
/// All handle-managed runtime objects inherit from this to enable unified access
struct RuntimeObject {
    virtual ~RuntimeObject() = default;
    TypedHandle handle;
};

// == VariantObject ===========================================================
// ============================================================================

/// Generic runtime object with variable-sized inline data
/// Layout: [VariantObject header][int...][float...][String...]
/// Used for Effects, Events, ItemProps, and user-defined engine structures
struct VariantObject : RuntimeObject {
    uint16_t num_ints = 0;
    uint16_t num_floats = 0;
    uint16_t num_strings = 0;

    // Data accessors - data follows immediately after header
    int* ints() { return reinterpret_cast<int*>(this + 1); }
    const int* ints() const { return reinterpret_cast<const int*>(this + 1); }

    float* floats() { return reinterpret_cast<float*>(ints() + num_ints); }
    const float* floats() const { return reinterpret_cast<const float*>(ints() + num_ints); }

    String* strings() { return reinterpret_cast<String*>(floats() + num_floats); }
    const String* strings() const { return reinterpret_cast<const String*>(floats() + num_floats); }

    /// Total size including header and inline data
    static size_t total_size(uint16_t num_ints, uint16_t num_floats, uint16_t num_strings)
    {
        return sizeof(VariantObject)
            + sizeof(int) * num_ints
            + sizeof(float) * num_floats
            + sizeof(String) * num_strings;
    }

    size_t total_size() const
    {
        return total_size(num_ints, num_floats, num_strings);
    }
};

// == UnmanagedArrayHandle ====================================================
// ============================================================================

/// Wrapper for IArray* to satisfy HandlePool requirements
struct UnmanagedArrayHandle {
    smalls::IArray* array = nullptr;
    Handle generic_handle;

    void set_generic_handle(Handle h) { generic_handle = h; }
    void reset() { array = nullptr; }
};

// == RuntimeObjectPool =======================================================
// ============================================================================

/// Metadata for variant object layouts
struct VariantLayout {
    uint16_t num_ints = 0;
    uint16_t num_floats = 0;
    uint16_t num_strings = 0;
};

/// Top-level pool that dispatches to type-specific pools via array indexing
/// Supports both system types (Effects, Events) and user-defined VariantObjects
struct RuntimeObjectPool {
    RuntimeObjectPool();
    ~RuntimeObjectPool();

    // Type IDs for system types
    static constexpr uint8_t TYPE_INVALID = 0;
    static constexpr uint8_t TYPE_EFFECT = 1;
    static constexpr uint8_t TYPE_EVENT = 2;
    static constexpr uint8_t TYPE_UNMANAGED_ARRAY = 3;
    // Reserved: 4-127 for future system types
    // User-defined types: 128-255

    /// Allocates an effect (system type)
    TypedHandle allocate_effect();

    /// Allocates an event (system type)
    TypedHandle allocate_event();

    /// Registers a user-defined variant type
    /// @param name Type name (for debugging)
    /// @param num_ints Number of int fields
    /// @param num_floats Number of float fields
    /// @param num_strings Number of string fields
    /// @return Type ID for the registered type (128-255), or TYPE_INVALID if no slots available
    uint8_t register_variant_type(StringView name, uint16_t num_ints, uint16_t num_floats, uint16_t num_strings);

    /// Allocates a user-defined variant object
    /// @param type_id Type ID returned from register_variant_type()
    /// @return Handle to allocated object, or invalid handle if type not registered
    TypedHandle allocate_variant(uint8_t type_id);

    /// Gets object by handle (returns base RuntimeObject pointer)
    /// Cast to VariantObject* when accessing variant data
    RuntimeObject* get(TypedHandle h);
    const RuntimeObject* get(TypedHandle h) const;

    /// Validates handle
    bool valid(TypedHandle h) const;

    /// Destroys object
    void destroy(TypedHandle h);

    /// Gets the layout for a variant type (useful for validation/debugging)
    const VariantLayout* get_variant_layout(uint8_t type_id) const;

    // == Unmanaged Array Support ==============================================

    /// Allocates an unmanaged array for propset storage
    /// @param elem_type TypeID of array elements
    /// @param initial_capacity Starting capacity (0 = no allocation)
    /// @return Handle to allocated array, or invalid handle on failure
    TypedHandle allocate_unmanaged_array(smalls::TypeID elem_type, uint32_t initial_capacity = 0);

    /// Gets unmanaged array by handle
    /// @return IArray* or nullptr if handle invalid
    smalls::IArray* get_unmanaged_array(TypedHandle h);

    /// Destroys an unmanaged array
    void destroy_unmanaged_array(TypedHandle h);

    /// Validates unmanaged array handle
    bool valid_unmanaged_array(TypedHandle h) const;

private:
    /// Array of pools indexed by type ID (256 entries, ~2KB overhead)
    std::array<std::unique_ptr<nw::TypedHandlePool>, 256> pools_;

    /// Metadata for variant types (layout info for allocation)
    std::array<VariantLayout, 256> variant_layouts_;

    /// Next available user type ID (starts at 128)
    uint8_t next_user_type_id_ = 128;

    /// Dedicated pool for unmanaged arrays (stores UnmanagedArrayHandle)
    /// Separate from type-indexed pools due to different allocation pattern
    nw::HandlePool<UnmanagedArrayHandle> unmanaged_array_pool_;
};

} // namespace nw
