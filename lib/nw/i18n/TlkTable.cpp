#include "TlkTable.hpp"

#include "../log.hpp"

namespace fs = std::filesystem;

namespace nw {

const uint32_t TlkTable::custom_id = 0x01000000;

TlkTable::TlkTable(const fs::path& dialog, const fs::path& custom)
    : dialog_{dialog}
{
    is_valid_ = dialog_.is_valid();

    auto fem = dialog.parent_path() / (dialog.stem().string() + "f" + ".tlk");
    LOG_F(INFO, "tlktable checking for feminine tlk: '{}'", fem.string());
    if (fs::exists(fem)) {
        dialogf_ = Tlk{fem};
        is_valid_ = is_valid_ && dialogf_.is_valid();
    }

    if (!custom.empty()) {
        custom_ = Tlk{custom};
        is_valid_ = is_valid_ && custom_.is_valid();

        fem = dialog.parent_path() / (custom.stem().string() + "f" + ".tlk");
        LOG_F(INFO, "tlktable checking for feminine tlk: '{}'", fem.string());
        if (fs::exists(fem)) {
            customf_ = Tlk{fem};
            is_valid_ = is_valid_ && customf_.is_valid();
        }
    }
}

std::string_view TlkTable::get(uint32_t strref, bool feminine) const
{
    if (custom_id & strref) {
        strref ^= custom_id;
        if (feminine && customf_.is_valid()) {
            return customf_.get(strref);
        } else if (custom_.is_valid()) {
            return custom_.get(strref);
        }
    } else if (feminine && dialogf_.is_valid()) {
        return dialogf_.get(strref);
    } else if (dialog_.is_valid()) {
        return dialog_.get(strref);
    }

    LOG_F(ERROR, "tlk: attempting to get strref from empty tlk table: {}, is fem: {}",
        strref, feminine);
    return {};
}

bool TlkTable::is_valid() const noexcept { return is_valid_; }

} // namespace nw
