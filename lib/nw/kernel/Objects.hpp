#pragma once

#include "../objects/ObjectBase.hpp"

#include <deque>
#include <stack>
#include <variant>

namespace nw::kernel {

struct Objects {
    virtual ~Objects();

    /// Clears all objects
    virtual void clear();

    /// Destroys object
    virtual void destroy(ObjectHandle handle);

    /// Gets object
    virtual ObjectBase* get(ObjectHandle handle) const;

    template <typename T>
    T* get_as(ObjectHandle handle) const;

    /// Initializes object system.
    /// @note: in the current implementation this does nothing.
    virtual void initialize();

    /// Instantiates object from a blueprint
    /// @note will call kernel::resources()
    virtual ObjectHandle load(std::string_view resref, ObjectType type);

    /// Tests if object is valid
    /// @note This is a simple nullptr check around Objects::get.  Not calling this before
    /// the latter is **not** undefined behavior.
    virtual bool valid(ObjectHandle handle) const;

protected:
    virtual ObjectHandle next_handle(ObjectType type);

private:
    std::stack<uint32_t> object_free_list_;
    std::deque<std::variant<ObjectBase*, ObjectHandle>> objects_;
};

template <typename T>
T* Objects::get_as(ObjectHandle handle) const
{
    auto obj = get(handle);
    if (obj && obj->handle().type == T::object_type) {
        return static_cast<T*>(obj);
    }
    return nullptr;
}

} // namespace nw::kernel
