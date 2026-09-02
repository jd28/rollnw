#pragma once

#include "item_editor.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Rml {
class Context;
class ElementDocument;
}

namespace nw::toolset {

// This is a change-triggered presentation transform, not a frame path. It
// copies the bounded ItemEditor snapshot into the shape consumed by RML and
// holds that storage for the data model's lifetime.
class ItemEditorDataModel {
public:
    using Dispatch = std::function<bool(std::string_view command,
        std::span<const int32_t> arguments,
        std::string& diagnostic)>;

    ItemEditorDataModel();
    ~ItemEditorDataModel();

    ItemEditorDataModel(const ItemEditorDataModel&) = delete;
    ItemEditorDataModel& operator=(const ItemEditorDataModel&) = delete;

    bool initialize(Rml::Context& context, Dispatch dispatch);
    void refresh(ItemEditorAppearanceInput input);
    // Queues the retained model field for focus after RML has materialized the
    // refreshed document. No active model field leaves the request unchanged.
    void request_model_focus();
    bool apply_pending_focus(Rml::ElementDocument* document);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nw::toolset
