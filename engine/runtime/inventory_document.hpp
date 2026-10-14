#pragma once
#include <darkangel/inventory_ui.hpp>
#include <RmlUi/Core.h>
#include <filesystem>

namespace darkangel {
class InventoryDocument {
public:
    InventoryDocument(Rml::Context&,const std::filesystem::path&);
    void update(const InventoryUiModel&);
    Rml::Element* find(const std::string&)const;
    std::vector<InventoryUiAction> drain();
    const InventoryUiModel& model()const{return model_;}
private:
    InventoryUiModel model_;Rml::ElementDocument* document_{};Rml::DataModelHandle handle_;
    std::string selected_,name_,description_,kind_;bool selected_mask_{},selected_usable_{},empty_{true};std::vector<InventoryUiAction> actions_;
    void select(std::string);
    void queue(InventoryUiAction);
};
}
