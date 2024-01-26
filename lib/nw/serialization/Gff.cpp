#include "Gff.hpp"

#include "../log.hpp"

#include <cstring>
#include <filesystem>

namespace nw {

// GffField -------------------------------------------------------------------

GffField::GffField()
    : parent_{nullptr}
{
}

GffField::GffField(const Gff* parent, const GffFieldEntry* entry)
    : parent_{parent}
    , entry_{entry}
{
}

std::string_view GffField::name() const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff field");
        return {};
    }

    if (entry_->label_idx >= parent_->head_->label_count) {
        LOG_F(ERROR, "invalid label index: {}", entry_->label_idx);
        return {};
    }
    return parent_->labels_[entry_->label_idx].view();
}

size_t GffField::size() const
{
    if (!valid()) {
        return 0;
    }

    if (entry_->data_or_offset >= parent_->head_->list_idx_count) {
        LOG_F(ERROR, "invalid list index: {}", entry_->data_or_offset);
        return {};
    }

    return entry_->type == SerializationType::list
        ? parent_->list_indices_[entry_->data_or_offset / 4] // This a byte offset into list indices, not an index
        : 0;                                                 // itself.
}

SerializationType::type GffField::type() const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff field");
        return SerializationType::invalid;
    }

    return static_cast<SerializationType::type>(entry_->type);
}

GffStruct GffField::operator[](size_t index) const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff field");
        return {};
    }

    if (entry_->data_or_offset + 4 >= parent_->head_->list_idx_count) {
        LOG_F(ERROR, "invalid list index: {}", entry_->data_or_offset);
        return {};
    }

    auto idx = entry_->data_or_offset / 4;
    return GffStruct(parent_, &parent_->structs_[parent_->list_indices_[idx + index + 1]]);
}

// GffStruct ------------------------------------------------------------------

GffStruct::GffStruct(const Gff* parent, const GffStructEntry* entry)
    : parent_{parent}
    , entry_{entry}
{
}

bool GffStruct::has_field(std::string_view label) const
{
    return operator[](label).valid();
}

GffField GffStruct::operator[](std::string_view label) const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff struct");
        return {};
    }

    if (entry_->field_count == 1) {
        if (entry_->field_index >= parent_->head_->field_count) {
            return {};
        }
        auto f = GffField(parent_, &parent_->fields_[entry_->field_index]);
        return string::icmp(f.name(), label) ? f : GffField{};
    } else {
        if (entry_->field_index >= parent_->head_->field_idx_count) {
            return {};
        }
        auto fi = &parent_->field_indices_[entry_->field_index / 4];
        for (size_t i = 0; i < entry_->field_count; ++i) {
            if (fi[i] >= parent_->head_->field_count) {
                return {};
            }
            GffField field(parent_, &parent_->fields_[fi[i]]);
            if (string::icmp(field.name(), label)) {
                return field;
            }
        }
        return GffField();
    }
}

GffField GffStruct::operator[](size_t index) const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff struct");
        return {};
    }

    if (index >= size()) {
        LOG_F(ERROR, "GffStruct: invalid index: {}", index);
        return {};
    }

    if (entry_->field_count == 1) {
        if (entry_->field_index >= parent_->head_->field_count) {
            return {};
        }
        return GffField(parent_, &parent_->fields_[entry_->field_index]);
    } else {
        if (entry_->field_index >= parent_->head_->field_idx_count) {
            return {};
        }
        auto fi = &parent_->field_indices_[entry_->field_index / 4]; // Byte offset, not index
        return GffField(parent_, &parent_->fields_[fi[index]]);
    }
}

// Gff ------------------------------------------------------------------------

Gff::Gff(const std::filesystem::path& file, nw::LanguageID lang)
    : data_{ResourceData::from_file(file)}
    , lang_{lang}
{
    is_loaded_ = parse();
}

Gff::Gff(ResourceData data, nw::LanguageID lang)
    : data_{std::move(data)}
    , lang_{lang}
{
    is_loaded_ = parse();
}

GffStruct Gff::toplevel() const
{
    return valid() ? GffStruct(this, &structs_[0]) : GffStruct();
}

bool Gff::valid() const
{
    return is_loaded_;
}

#define CHECK_OFF(cond)                                              \
    do {                                                             \
        if (!(cond)) {                                               \
            LOG_F(ERROR, "Corrupt GFF: {}", ROLLNW_STRINGIFY(cond)); \
            return false;                                            \
        }                                                            \
    } while (0)

bool Gff::parse()
{
    // Order is how file is laid out.
    CHECK_OFF(sizeof(GffHeader) < data_.bytes.size());
    head_ = reinterpret_cast<GffHeader*>(data_.bytes.data());
    CHECK_OFF(head_->label_offset < data_.bytes.size() && head_->label_offset + head_->label_count * sizeof(GffLabel) < data_.bytes.size());
    labels_ = reinterpret_cast<GffLabel*>(data_.bytes.data() + head_->label_offset);
    CHECK_OFF(head_->struct_offset < data_.bytes.size() && head_->struct_offset + head_->struct_count * sizeof(GffStructEntry) < data_.bytes.size());
    structs_ = reinterpret_cast<GffStructEntry*>(data_.bytes.data() + head_->struct_offset);
    CHECK_OFF(head_->field_offset < data_.bytes.size() && head_->field_offset + head_->field_count * sizeof(GffFieldEntry) < data_.bytes.size());
    fields_ = reinterpret_cast<GffFieldEntry*>(data_.bytes.data() + head_->field_offset);
    CHECK_OFF(head_->field_data_offset < data_.bytes.size() && head_->field_data_offset + head_->field_data_count < data_.bytes.size());
    CHECK_OFF(head_->field_idx_offset < data_.bytes.size() && head_->field_idx_offset + head_->field_idx_count <= data_.bytes.size());
    field_indices_ = reinterpret_cast<uint32_t*>(data_.bytes.data() + head_->field_idx_offset);

    // If there are no list indexes.. offset will be the size of file, but no data will
    // be there.
    CHECK_OFF(head_->list_idx_offset <= data_.bytes.size());
    if (head_->list_idx_count > 0) {
        CHECK_OFF(head_->list_idx_offset + head_->list_idx_count <= data_.bytes.size());
        list_indices_ = reinterpret_cast<uint32_t*>(data_.bytes.data() + head_->list_idx_offset);
    } else {
        list_indices_ = nullptr;
    }

    return true;
}

#undef CHECK_OFF

} // namespace nw
