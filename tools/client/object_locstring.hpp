#pragma once

#include <nw/i18n/LocString.hpp>
#include <nw/i18n/TextRef.hpp>
#include <nw/objects/ObjectHandle.hpp>
#include <nw/smalls/types.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

// Explicit storage protocol for localized strings exposed by Object Details.
// Labels and field names are presentation only; they never select storage.
enum class ObjectLocStringStorage : uint8_t {
    none,
    object_name,
    area_name,
    module_name,
    module_description,
    propset_text_ref,
};

struct ObjectLocStringTarget {
    ObjectHandle object{};
    ObjectLocStringStorage storage = ObjectLocStringStorage::none;
    smalls::TypeID propset_type{};
    uint32_t field_index = UINT32_MAX;

    bool operator==(const ObjectLocStringTarget&) const = default;
};

// Reads the authored value selected by the explicit target protocol. Invalid
// objects, storage tags, propsets, or field types return no value and a
// diagnostic; no presentation name participates in target selection.
[[nodiscard]] std::optional<LocString> read_object_locstring(
    smalls::Runtime& runtime, const ObjectLocStringTarget& target,
    std::string* diagnostic = nullptr);

// Replaces one localized-string target. Propset targets require a stable
// TextRef whose authored value equals replacement; direct targets require an
// invalid reference and assign replacement in place.
[[nodiscard]] bool write_object_locstring(smalls::Runtime& runtime,
    const ObjectLocStringTarget& target, const LocString& replacement,
    TextRef replacement_ref, std::string* diagnostic = nullptr);

} // namespace nw::toolset
