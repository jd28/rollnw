#pragma once

#include "../i18n/LocString.hpp"
#include "../resources/ResourceType.hpp"
#include "../serialization/Serialization.hpp"

#include <vector>

namespace nw {

struct JournalEntry {
    LocString text;
    uint32_t id;
    uint16_t end; // Just a bool really..
};

struct JournalCategory {
    String comment;
    std::vector<JournalEntry> entries;
    LocString name;
    String tag;
    uint32_t priority;
    uint32_t xp;
    uint16_t picture;
};

struct Journal {
    explicit Journal(const GffStruct gff);

    static constexpr int json_archive_version = 1;
    static constexpr ResourceType::type restype = ResourceType::jrl;

    std::vector<JournalCategory> categories;
};

} // namespace nw
