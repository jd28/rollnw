#pragma once

#include "constants.hpp"
#include "rules.hpp"

#include "../../kernel/GameProfile.hpp"
#include "../../resources/Directory.hpp"
#include "../../resources/Erf.hpp"
#include "../../resources/Key.hpp"

namespace nwn1 {

/// NWN1 Game Profile
struct Profile : nw::GameProfile {
    virtual ~Profile() = default;

    /**
     * @brief Loads rules
     *
     * - Load Selector and Matcher
     * - Load Components
     * - Load 2DAs
     * - Load Constants
     * - Post Process 2DAs
     *
     */
    bool load_rules() const override;

    bool load_resources() override;

    nw::Directory ambient_user_, ambient_install_;
    nw::Directory development_;
    nw::Directory dmvault_user_, dmvault_install_;
    nw::Directory localvault_user_, localvault_install_;
    nw::Directory music_user_, music_install_;
    nw::Directory override_user_, override_install_;
    nw::Directory portraits_user_, portraits_install_;
    nw::Directory servervault_user_, servervault_install_;

    std::vector<nw::unique_container> patches_;

    std::vector<nw::Erf> texture_packs_;
    std::vector<nw::Key> keys_;
};

} // namespace nwn1
