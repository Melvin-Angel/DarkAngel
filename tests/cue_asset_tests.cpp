#include <darkangel/cue_assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>bool refused(F call){try{call();}catch(const std::exception&){return true;}return false;}
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);check(bool(in),"Cue fixture missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,std::string_view bytes){std::ofstream out(p,std::ios::binary);out<<bytes;check(bool(out),"Cue fixture write");}
}
int main(){try{
    // The owned cues are valid sources, and they answer to the keys the owned content publishes.
    const auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content/royal_district/combat";const auto swing_source=read(content/"cues/swing.dacue"),burn_source=read(content/"cues/burn.dacue");const auto swing=decode_cue(swing_source),burn=decode_cue(burn_source);
    check(swing.key=="Attack.Start"&&swing.predictable&&!swing.audio.empty()&&burn.key=="Status.Burn"&&!burn.predictable&&!burn.visual.empty(),"The owned cues changed their keys or timing");
    {const auto effect=Json::parse(read(content/"burn.daeffect"));bool bound=false;for(const auto& cue:effect.at("cues"))bound|=cue.at("id")==burn.id.text()&&cue.at("key")==burn.key;check(bound,"The Burn effect's cue binding and the Burn cue asset disagree");
        const auto light=Json::parse(read(content/"light.daaction"));bool keyed=false;for(const auto& block:light.at("blocks"))keyed|=block.at("kind")=="cue"&&block.at("key")==swing.key;check(keyed,"The Light action has no cue block for the swing cue's key");}
    // Cook, package and load: a cue is an ordinary native source with a frozen product.
    const auto workspace=std::filesystem::path(DAE_BINARY_DIR)/("cue-"+AssetId::random().text()),mount=workspace/"source";std::filesystem::create_directories(mount/"cues");write(mount/"cues/swing.dacue",swing_source);write(mount/"cues/burn.dacue",burn_source);
    AssetService assets(mount,workspace/"cache");check(assets.adopt("cues/swing.dacue")==swing.id&&assets.adopt("cues/burn.dacue")==burn.id,"Cue source identity");assets.scan();check(assets.assets().size()==2,"The cue sources are not in the catalog");
    check(assets.cook("cues/swing.dacue").changed&&assets.cook("cues/burn.dacue").changed&&!assets.cook("cues/swing.dacue").changed,"Cue cook, or a warm cook that did work again");
    const std::array<AssetId,1> more{burn.id};assets.package(swing.id,workspace/"registry.json",more);const auto frozen=read(workspace/"registry.json");auto cues=load_cooked_cues(workspace/"registry.json",assets.cas_path());
    check(cues.size()==2,"A packaged registry did not give back its cues");const auto found=std::find_if(cues.begin(),cues.end(),[&](const auto& cue){return cue.id==swing.id;});check(found!=cues.end()&&found->key==swing.key&&found->audio==swing.audio&&found->tail_ticks==swing.tail_ticks&&found->predictable,"A cooked cue differs from its source");
    // A broken source keeps the previous product; an edit through the native path cooks the new value.
    write(mount/"cues/swing.dacue","broken");check(refused([&]{assets.cook("cues/swing.dacue");}),"A broken cue source cooked");assets.package(swing.id,workspace/"retained.json",more);check(read(workspace/"retained.json")==frozen,"A failed cue cook replaced the frozen product");write(mount/"cues/swing.dacue",swing_source);
    {auto edited=Json::parse(swing_source);edited["tail_ticks"]=20;assets.edit_native("cues/swing.dacue",darkangel::sha256(swing_source),edited.dump(2)+"\n");assets.package(swing.id,workspace/"edited.json",more);for(const auto& cue:load_cooked_cues(workspace/"edited.json",assets.cas_path()))if(cue.id==swing.id)check(cue.tail_ticks==20,"An edited cue did not cook its new tail");
        auto invalid=edited;invalid["audio"]="";check(refused([&]{assets.edit_native("cues/swing.dacue",darkangel::sha256(read(mount/"cues/swing.dacue")),invalid.dump(2)+"\n");})&&decode_cue(read(mount/"cues/swing.dacue")).tail_ticks==20,"An invalid cue edit was accepted or damaged the source");}
    // Two cues may not answer to one key, and a new cue is created through the native path.
    {auto twin=Json::parse(burn_source);twin["asset"]=AssetId::random().text();twin["key"]=swing.key;const auto made=assets.create_native("cues/twin.dacue",twin.dump(2)+"\n");const std::array<AssetId,2> all{burn.id,made.root};assets.package(swing.id,workspace/"twins.json",all);check(refused([&]{load_cooked_cues(workspace/"twins.json",assets.cas_path());}),"Two cues answering to one key were both loaded");}
    std::cout<<"Cue assets: owned cues and their bindings, catalog, cook, package and load, failed cook retention, native edit and create, one cue per key passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
