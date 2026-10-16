#pragma once
#include <darkangel/ui_document.hpp>
#include <RmlUi/Core.h>
namespace darkangel {
// Native owner projection only. UiService owns the document, context, input and rendering.
class OwnerUiBinding {
public:
 explicit OwnerUiBinding(Rml::Context&);
 void update(const OwnerUiModel&);
 bool downed{},wiped{},reviving{};Rml::String revive_text;float revive_percent{};
 OwnerUiModel model;Rml::DataModelHandle handle;
    // Bound variables. Text is formatted natively so the document carries no rules.
    float health_percent{},stamina_percent{},target_percent{};Rml::String health_text,stamina_text,target_text,mask,target_name,prompt,loading_text;bool dead{},staggered{},has_target{},has_mask{},target_locked{},crosshair{},has_prompt{},loading{};float loading_percent{};
    void assign(const OwnerUiModel&);
};
}
