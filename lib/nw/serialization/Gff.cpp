#include "Gff.hpp"

#include "../log.hpp"

#include <cstring>
#include <filesystem>
#include <limits>
#include <type_traits>

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

StringView GffField::name() const
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

    if (entry_->data_or_offset % 4 != 0) {
        LOG_F(ERROR, "invalid non-word-aligned list index offset: {}", entry_->data_or_offset);
        return {};
    }

    if (entry_->data_or_offset + 4 > parent_->head_->list_idx_count) {
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

    if (entry_->data_or_offset % 4 != 0) {
        LOG_F(ERROR, "invalid non-word-aligned list index offset: {}", entry_->data_or_offset);
        return {};
    }

    if (entry_->data_or_offset + 4 > parent_->head_->list_idx_count) {
        LOG_F(ERROR, "invalid list index: {}", entry_->data_or_offset);
        return {};
    }

    auto idx = entry_->data_or_offset / 4;
    if (idx >= (parent_->head_->list_idx_count / sizeof(uint32_t))) {
        LOG_F(ERROR, "invalid list index table offset: {}", idx);
        return {};
    }
    const auto list_size = parent_->list_indices_[idx];
    if (idx + list_size >= (parent_->head_->list_idx_count / sizeof(uint32_t))) {
        LOG_F(ERROR, "invalid list span: idx={}, size={}", idx, list_size);
        return {};
    }
    if (index >= list_size) {
        LOG_F(ERROR, "invalid list element index: {} >= {}", index, list_size);
        return {};
    }

    auto struct_idx = parent_->list_indices_[idx + index + 1];
    if (struct_idx >= parent_->head_->struct_count) {
        LOG_F(ERROR, "invalid struct index in list: {}", struct_idx);
        return {};
    }
    return GffStruct(parent_, &parent_->structs_[struct_idx]);
}

// GffStruct ------------------------------------------------------------------

GffStruct::GffStruct(const Gff* parent, const GffStructEntry* entry)
    : parent_{parent}
    , entry_{entry}
{
}

bool GffStruct::has_field(StringView label) const
{
    return operator[](label).valid();
}

GffField GffStruct::operator[](StringView label) const
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
        if (entry_->field_index % 4 != 0) {
            return {};
        }
        if (static_cast<size_t>(entry_->field_index) + static_cast<size_t>(entry_->field_count) * sizeof(uint32_t)
            > static_cast<size_t>(parent_->head_->field_idx_count)) {
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
        if (entry_->field_index % 4 != 0) {
            return {};
        }
        if (static_cast<size_t>(entry_->field_index) + static_cast<size_t>(entry_->field_count) * sizeof(uint32_t)
            > static_cast<size_t>(parent_->head_->field_idx_count)) {
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
    return valid() && head_ && head_->struct_count > 0 ? GffStruct(this, &structs_[0]) : GffStruct();
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
    auto in_bounds = [size = data_.bytes.size()](size_t offset, size_t bytes) {
        if (offset > size) { return false; }
        return bytes <= (size - offset);
    };

    if (!in_bounds(0, sizeof(GffHeader))) {
        LOG_F(ERROR, "Corrupt GFF: header out of bounds");
        return false;
    }

    std::memcpy(&header_storage_, data_.bytes.data(), sizeof(GffHeader));
    head_ = &header_storage_;

    const auto copy_table = [&](size_t offset, size_t elem_count, size_t elem_size, auto& out_vec) {
        using Elem = typename std::remove_reference_t<decltype(out_vec)>::value_type;
        if (elem_count == 0) {
            out_vec.clear();
            return true;
        }
        if (elem_count > (std::numeric_limits<size_t>::max() / elem_size)) {
            return false;
        }
        const size_t bytes = elem_count * elem_size;
        if (!in_bounds(offset, bytes)) {
            return false;
        }
        out_vec.resize(elem_count);
        std::memcpy(out_vec.data(), data_.bytes.data() + offset, bytes);
        static_assert(std::is_same_v<Elem, std::remove_cv_t<Elem>>);
        return true;
    };

    CHECK_OFF(copy_table(head_->label_offset, head_->label_count, sizeof(GffLabel), labels_storage_));
    CHECK_OFF(copy_table(head_->struct_offset, head_->struct_count, sizeof(GffStructEntry), structs_storage_));
    CHECK_OFF(copy_table(head_->field_offset, head_->field_count, sizeof(GffFieldEntry), fields_storage_));

    CHECK_OFF(in_bounds(head_->field_data_offset, head_->field_data_count));

    CHECK_OFF(head_->field_idx_count % sizeof(uint32_t) == 0);
    CHECK_OFF(copy_table(head_->field_idx_offset, head_->field_idx_count / sizeof(uint32_t), sizeof(uint32_t), field_indices_storage_));

    CHECK_OFF(head_->list_idx_count % sizeof(uint32_t) == 0);
    CHECK_OFF(copy_table(head_->list_idx_offset, head_->list_idx_count / sizeof(uint32_t), sizeof(uint32_t), list_indices_storage_));

    labels_ = labels_storage_.empty() ? nullptr : labels_storage_.data();
    structs_ = structs_storage_.empty() ? nullptr : structs_storage_.data();
    fields_ = fields_storage_.empty() ? nullptr : fields_storage_.data();
    field_indices_ = field_indices_storage_.empty() ? nullptr : field_indices_storage_.data();
    list_indices_ = list_indices_storage_.empty() ? nullptr : list_indices_storage_.data();

    return true;
}

#undef CHECK_OFF

} // namespace nw
