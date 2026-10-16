#pragma once
#include <darkangel/altar_ui.hpp>
#include <RmlUi/Core.h>
#include <filesystem>

namespace darkangel {
// The game-owned altar menu document: rest, and fast travel to discovered altars. Events become queued actions.
class AltarDocument {
public:
    AltarDocument(Rml::Context&);
    void update(const AltarUiModel&);
    std::vector<AltarUiAction> drain();
    const AltarUiModel& model()const{return model_;}
private:
    AltarUiModel model_;Rml::DataModelHandle handle_;std::vector<AltarUiAction> actions_;
    void queue(AltarUiAction);
};
}
