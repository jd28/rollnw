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

    /// Intializes the loaded module
    /// warning: `nw::kernel::resman().load_module(...)` **must** be called before this.
    virtual Module* initialize_module();

    /// Instantiates object from a blueprint
    /// @note will call kernel::resources()
    virtual ObjectHandle load(std::string_view resref, ObjectType type);

    /// Instantiates object from a blueprint
    /// @note will call kernel::resources()
    template <typename T>
    T* load_as(std::string_view resref);

    /// Loads an area
    virtual Area* load_area(Resref area);

    /// Instantiates object from a blueprint
    /// @note will call kernel::resources()
    virtual ObjectHandle load_from_archive(ObjectType type, const GffInputArchiveStruct& archive,
        SerializationProfile profile = SerializationProfile::instance);

    /// Instantiates object from a blueprint
    /// @note will call kernel::resources()
    virtual ObjectHandle load_from_archive(ObjectType type, const nlohmann::json& archive,
        SerializationProfile profile = SerializationProfile::instance);

    template <typename T, typename Archive>
    T* load_from_archive_as(const Archive& archive,
        SerializationProfile profile = SerializationProfile::instance);

    /// Tests if object is valid
    /// @note This is a simple nullptr check around Objects::get.  Not calling this before
    /// the latter is **not** undefined behavior.
    virtual bool valid(ObjectHandle handle) const;

protected:
    virtual ObjectHandle next_handle(ObjectType type);

private:
    ObjectHandle commit_object(ObjectBase* obj, ObjectType type);
    Module* module_ = nullptr;
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

template <typename T>
T* Objects::load_as(std::string_view resref)
{
    auto oh = load(resref, T::object_type);
    return get_as<T>(oh);
}

template <typename T, typename Archive>
T* Objects::load_from_archive_as(const Archive& archive, SerializationProfile profile)
{
    auto oh = load_from_archive(T::object_type, archive, profile);
    return get_as<T>(oh);
}

} // namespace nw::kernel
