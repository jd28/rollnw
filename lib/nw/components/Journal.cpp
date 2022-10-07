#include "Journal.hpp"

namespace nw {

Journal::Journal(const GffStruct gff)
{
    size_t sz = gff["Categories"].size();
    for (size_t i = 0; i < sz; ++i) {
        JournalCategory jc;
        auto cat = gff["Categories"][i];
        cat.get_to("Comment", jc.comment);
        cat.get_to("Name", jc.name);
        cat.get_to("Picture", jc.picture);
        cat.get_to("Priority", jc.priority);
        cat.get_to("Tag", jc.tag);
        cat.get_to("XP", jc.xp);

        auto entry_sz = cat["EntryList"].size();
        for (size_t j = 0; j < entry_sz; ++j) {
            JournalEntry je;
            auto ent = cat["EntryList"][i];
            if (!ent.get_to("End", je.end)
                || !ent.get_to("ID", je.id)
                || !ent.get_to("Text", je.text)) {
                LOG_F(ERROR, "journal something weird in category {}, entry {}", i, j);
                break;
            }
            jc.entries.push_back(std::move(je));
        }

        categories.push_back(std::move(jc));
    }
}

} // namespace nw
