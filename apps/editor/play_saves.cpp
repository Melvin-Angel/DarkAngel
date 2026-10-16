#include "play_saves.hpp"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <array>
#include <chrono>
#include <stdexcept>

namespace darkangel::editor_app {
namespace {void require(bool b,const char* e){if(!b)throw std::runtime_error(e);}}
std::span<const LedgerKind> play_ledger_kinds(){static const std::array<LedgerKind,9> kinds{LedgerKind{"interactable",1},LedgerKind{"trigger",1},LedgerKind{"encounter",1},LedgerKind{"actor",1},LedgerKind{"pickup",1},LedgerKind{"mover",1},LedgerKind{"altar",1},LedgerKind{"checkpoint",1},LedgerKind{"puzzle",1}};return kinds;}
std::string encode_play_save(const WorldLedger& ledger,const PartyInventory* inventory,AssetId scene,StableId entry){
    nlohmann::json value{{"version",play_save_version},{"kind","darkangel.play"},{"ledger",nlohmann::json::parse(ledger.serialize())},{"inventories",nlohmann::json::array()},{"scene",scene.text()},{"entry",entry?nlohmann::json(entry.text()):nlohmann::json(nullptr)}};
    if(inventory)for(const auto& snapshot:inventory->capture())value["inventories"].push_back(nlohmann::json::parse(encode_inventory(snapshot)));
    return value.dump();
}
PlaySave decode_play_save(std::string_view text){
    const auto value=nlohmann::json::parse(text);require(value.is_object()&&value.value("kind",std::string())=="darkangel.play"&&value.value("version",0u)==play_save_version,"Not a DarkAngel play save of this version");
    PlaySave result{WorldLedger::deserialize(value.at("ledger").dump(),play_ledger_kinds())};require(value.at("inventories").is_array()&&value.at("inventories").size()<=64,"Save inventory bound");
    for(const auto& snapshot:value.at("inventories"))result.inventories.push_back(decode_inventory(snapshot.dump()));
    result.scene=AssetId::parse(value.at("scene").get<std::string>());if(value.at("entry").is_string())result.entry=StableId::parse(value.at("entry").get<std::string>());return result;
}
PlaySaves::PlaySaves(std::filesystem::path slot):slot_(std::move(slot)){}
PlaySaves::~PlaySaves()=default;
PersistenceService& PlaySaves::service(){if(!service_){std::filesystem::create_directories(slot_.parent_path());service_=std::make_unique<PersistenceService>(slot_,"Project save");}return *service_;}
bool PlaySaves::save(std::string payload,std::string reason){
    if(pending_){status_="A save is still being written.";return false;}
    auto& s=service();operation_=std::max<std::uint64_t>(operation_+1,std::uint64_t(std::chrono::system_clock::now().time_since_epoch().count()));
    const auto receipt=s.submit({operation_,s.revision(),std::move(reason),std::move(payload)});pending_=operation_;status_=std::string("Saving: ")+save_outcome_text(receipt.outcome);return true;
}
void PlaySaves::poll(){
    if(!pending_||!service_)return;const auto receipt=service_->receipt(pending_);if(!receipt||receipt->outcome==SaveOutcome::Captured||receipt->outcome==SaveOutcome::Writing)return;
    status_=receipt->outcome==SaveOutcome::Committed?"Saved (revision "+std::to_string(receipt->revision)+").":"Save failed: "+receipt->error+". The previous save is intact.";pending_=0;inspected_.reset();
}
std::optional<PlaySave> PlaySaves::load(std::string& warning)const{
    if(service_)service_->flush();const auto loaded=load_save(slot_,{},play_save_version);if(!loaded)return std::nullopt;warning=loaded->warning;return decode_play_save(loaded->snapshot.payload);
}
void PlaySaves::draw(bool playing_host,const std::function<void()>& save_now){
    poll();ImGui::TextUnformatted("Saves");ImGui::TextDisabled("%s",slot_.string().c_str());
    ImGui::Checkbox("Play from this slot",&play_from_slot);ImGui::SameLine();ImGui::TextDisabled("(the party starts at the saved checkpoint with saved inventories)");
    ImGui::BeginDisabled(!playing_host||pending_);if(ImGui::Button("Save now")&&save_now)save_now();ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Verify")){try{if(service_)service_->flush();inspected_=inspect_save(slot_);status_="Verified.";}catch(const std::exception& error){status_=error.what();}}ImGui::SameLine();
    if(ImGui::Button("Back up")){try{if(service_)service_->flush();auto copy=slot_;copy+=".backup";backup_save(slot_,copy);status_="Backed up to "+copy.string();}catch(const std::exception& error){status_=error.what();}}ImGui::SameLine();
    ImGui::BeginDisabled(playing_host||pending_);if(ImGui::Button("Delete slot"))ImGui::OpenPopup("Delete the save slot?");ImGui::EndDisabled();
    if(ImGui::BeginPopupModal("Delete the save slot?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Delete every snapshot in this slot? This cannot be undone.");if(ImGui::Button("Delete")){service_.reset();std::error_code ignored;std::filesystem::remove(slot_,ignored);inspected_.reset();status_="Deleted the save slot.";ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
    if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
    if(!inspected_){try{inspected_=inspect_save(slot_);}catch(const std::exception& error){status_=error.what();inspected_=SaveSlotInfo{};}}
    const auto& info=*inspected_;if(info.snapshots.empty()){ImGui::TextDisabled("The slot is empty.");return;}
    ImGui::Text("Head revision %llu, container version %u",static_cast<unsigned long long>(info.head),info.container);
    if(selected_>=info.snapshots.size())selected_=0;
    for(std::size_t i=0;i<info.snapshots.size();++i){const auto& s=info.snapshots[i];const auto label="Revision "+std::to_string(s.revision)+" - "+s.reason+" - "+s.created+(s.valid?"":" (DAMAGED)")+"##save"+std::to_string(i);if(ImGui::Selectable(label.c_str(),selected_==i))selected_=i;}
    const auto& chosen=info.snapshots[selected_];ImGui::Text("Digest %s",chosen.digest.substr(0,16).c_str());
    try{const auto save=decode_play_save(chosen.payload);ImGui::Text("Scene %s, %zu ledger records, %zu inventories",save.scene.text().c_str(),save.ledger.records().size(),save.inventories.size());
        if(ImGui::TreeNode("Ledger records")){for(const auto& [id,record]:save.ledger.records()){ImGui::Text("%s  %s/%s",record.kind.c_str(),id.scene.text().substr(0,8).c_str(),id.object.text().c_str());for(const auto& [name,property]:record.properties){std::string shown=std::visit([](const auto& v)->std::string{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,bool>)return v?"true":"false";else if constexpr(std::is_same_v<T,std::string>)return v;else if constexpr(std::is_same_v<T,LedgerTransform>)return "transform";else return std::to_string(v);},property.value);ImGui::Text("    %s = %s",name.c_str(),shown.c_str());}}ImGui::TreePop();}
    }catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"Unreadable: %s",error.what());}
}
}
