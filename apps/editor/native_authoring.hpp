#pragma once
#include <darkangel/assets.hpp>
#include <nlohmann/json.hpp>
#include <map>
namespace darkangel::editor_app {
struct NativeDraft {AssetInfo asset;std::string saved; nlohmann::json value;bool dirty()const{return nlohmann::json::parse(saved)!=value;}};
class NativeAuthoring {
public:
    NativeDraft& open(AssetService&,AssetId);
    CookResult save(AssetService&,AssetId);
    AssetId duplicate(AssetService&,AssetId,std::string_view name,bool blank=false);
    void assign(AssetService&,AssetId kit,std::size_t slot,AssetId ability);
    void bind(AssetService&,AssetId kit,AssetId ability,unsigned block,AssetId effect,double power);
    bool dirty()const;
    void save_all(AssetService&);
    std::map<AssetId,NativeDraft> drafts;
};
// Cook every selected scene root and its complete native closure into a candidate
// package. The caller validates runtime/GPU/session resources before switching.
void package_authoring(AssetService&,std::span<const AssetId>,const std::filesystem::path&);
}
