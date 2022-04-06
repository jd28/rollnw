#include "GffInputArchive.hpp"

#include "../log.hpp"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace nw {

// GffField -------------------------------------------------------------------

GffInputArchiveField::GffInputArchiveField()
    : parent_{nullptr}
{
}

GffInputArchiveField::GffInputArchiveField(const GffInputArchive* parent, const GffFieldEntry* entry)
    : parent_{parent}
    , entry_{entry}
{
}

std::string_view GffInputArchiveField::name() const
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

size_t GffInputArchiveField::size() const
{
    if (!valid()) { return 0; }

    if (entry_->data_or_offset >= parent_->head_->list_idx_count) {
        LOG_F(ERROR, "invalid list index: {}", entry_->data_or_offset);
        return {};
    }

    return entry_->type == SerializationType::list
        ? parent_->list_indices_[entry_->data_or_offset / 4] // This a byte offset into list indices, not an index
        : 0;                                                 // itself.
}

SerializationType::type GffInputArchiveField::type() const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff field");
        return SerializationType::invalid;
    }

    return static_cast<SerializationType::type>(entry_->type);
}

GffInputArchiveStruct GffInputArchiveField::operator[](size_t index) const
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
    return GffInputArchiveStruct(parent_, &parent_->structs_[parent_->list_indices_[idx + index + 1]]);
}

// GffStruct ------------------------------------------------------------------

GffInputArchiveStruct::GffInputArchiveStruct(const GffInputArchive* parent, const GffStructEntry* entry)
    : parent_{parent}
    , entry_{entry}
{
}

bool GffInputArchiveStruct::has_field(std::string_view label) const
{
    return operator[](label).valid();
}

GffInputArchiveField GffInputArchiveStruct::operator[](std::string_view label) const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff struct");
        return {};
    }

    if (entry_->field_count == 1) {
        if (entry_->field_index >= parent_->head_->field_count) { return {}; }
        auto f = GffInputArchiveField(parent_, &parent_->fields_[entry_->field_index]);
        return string::icmp(f.name(), label) ? f : GffInputArchiveField{};
    } else {
        if (entry_->field_index >= parent_->head_->field_idx_count) { return {}; }
        auto fi = &parent_->field_indices_[entry_->field_index / 4];
        for (size_t i = 0; i < entry_->field_count; ++i) {
            if (fi[i] >= parent_->head_->field_count) { return {}; }
            GffInputArchiveField field(parent_, &parent_->fields_[fi[i]]);
            if (string::icmp(field.name(), label)) {
                return field;
            }
        }
        return GffInputArchiveField();
    }
}

GffInputArchiveField GffInputArchiveStruct::operator[](size_t index) const
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
        if (entry_->field_index >= parent_->head_->field_count) { return {}; }
        return GffInputArchiveField(parent_, &parent_->fields_[entry_->field_index]);
    } else {
        if (entry_->field_index >= parent_->head_->field_idx_count) { return {}; }
        auto fi = &parent_->field_indices_[entry_->field_index / 4]; // Byte offset, not index
        return GffInputArchiveField(parent_, &parent_->fields_[fi[index]]);
    }
}

// GffInputArchive ------------------------------------------------------------------------

GffInputArchive::GffInputArchive(const std::filesystem::path& filename)
    : bytes_{ByteArray::from_file(filename)}
{
    is_loaded_ = parse();
}

GffInputArchive::GffInputArchive(ByteArray bytes)
    : bytes_{std::move(bytes)}
{
    is_loaded_ = parse();
}

GffInputArchiveStruct GffInputArchive::toplevel() const
{
    return valid() ? GffInputArchiveStruct(this, &structs_[0]) : GffInputArchiveStruct();
}

bool GffInputArchive::valid() const
{
    return is_loaded_;
}

#define CHECK_OFF(cond)                                             \
    do {                                                            \
        if (!(cond)) {                                              \
            LOG_F(ERROR, "Corrupt GFF: {}", LIBNW_STRINGIFY(cond)); \
            return false;                                           \
        }                                                           \
    } while (0)

bool GffInputArchive::parse()
{
    // Order is how file is laid out.
    CHECK_OFF(sizeof(GffHeader) < bytes_.size());
    head_ = reinterpret_cast<GffHeader*>(bytes_.data());
    CHECK_OFF(head_->label_offset < bytes_.size() && head_->label_offset + head_->label_count * sizeof(Resref) < bytes_.size());
    labels_ = reinterpret_cast<GffLabel*>(bytes_.data() + head_->label_offset);
    CHECK_OFF(head_->struct_offset < bytes_.size() && head_->struct_offset + head_->struct_count * sizeof(GffStructEntry) < bytes_.size());
    structs_ = reinterpret_cast<GffStructEntry*>(bytes_.data() + head_->struct_offset);
    CHECK_OFF(head_->field_offset < bytes_.size() && head_->field_offset + head_->field_count * sizeof(GffFieldEntry) < bytes_.size());
    fields_ = reinterpret_cast<GffFieldEntry*>(bytes_.data() + head_->field_offset);
    CHECK_OFF(head_->field_data_offset < bytes_.size() && head_->field_data_offset + head_->field_data_count < bytes_.size());
    CHECK_OFF(head_->field_idx_offset < bytes_.size() && head_->field_idx_offset + head_->field_idx_count <= bytes_.size());
    field_indices_ = reinterpret_cast<uint32_t*>(bytes_.data() + head_->field_idx_offset);

    // If there are no list indexes.. offset will be the size of file, but no data will
    // be there.
    CHECK_OFF(head_->list_idx_offset <= bytes_.size());
    if (head_->list_idx_count > 0) {
        CHECK_OFF(head_->list_idx_offset + head_->list_idx_count <= bytes_.size());
        list_indices_ = reinterpret_cast<uint32_t*>(bytes_.data() + head_->list_idx_offset);
    } else {
        list_indices_ = nullptr;
    }

    return true;
}

#undef CHECK_OFF

} // namespace nw
