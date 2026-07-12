#pragma once

#include <string>

namespace nw {
struct Item;
} // namespace nw

namespace nw::smalls {
struct Runtime;
} // namespace nw::smalls

namespace nwn1 {

struct ItemNativeMaterializationResult {
    bool ok = false;
    std::string error;

    explicit operator bool() const noexcept { return ok; }
};

/// Derive native item component rows from Smalls-owned NWN1 item propsets.
///
/// This is a load/instantiate boundary for runtime rows still consumed by C++:
/// item inventory layout and standalone item visual rows.
[[nodiscard]] ItemNativeMaterializationResult materialize_item_native_components(
    nw::Item& item,
    nw::smalls::Runtime& rt);

} // namespace nwn1
