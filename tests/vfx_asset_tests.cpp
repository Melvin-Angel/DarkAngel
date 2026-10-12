#include <darkangel/vfx_assets.hpp>
#include <darkangel/hash.hpp>
#include <fstream>
#include <iostream>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>std::string refusal(F call){try{call();}catch(const std::exception& error){return error.what();}return {};}
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);check(bool(in),"Effect fixture missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,std::string_view bytes){std::ofstream out(p,std::ios::binary);out<<bytes;check(bool(out),"Effect fixture write");}
}
int main(){try{
    // The cooker compiles a source with the pinned Effekseer tool; without the tool there is nothing to test.
    check(std::filesystem::is_regular_file(effekseer_tool()),"The pinned Effekseer tool is missing from .tools");
    const auto owned=std::filesystem::path(DAE_SOURCE_DIR)/"content/vfx/placeholder",samples=effekseer_tool().parent_path().parent_path()/"Sample/00_Basic";
    const auto workspace=std::filesystem::path(DAE_BINARY_DIR)/("vfx-"+AssetId::random().text()),mount=workspace/"source";std::filesystem::create_directories(mount);
    std::filesystem::copy(owned,mount/"placeholder",std::filesystem::copy_options::recursive);std::filesystem::create_directories(mount/"samples");std::filesystem::copy(samples/"Texture",mount/"samples/Texture",std::filesystem::copy_options::recursive);
    AssetService assets(mount,workspace/"cache");assets.scan();check(assets.assets().size()==2,"The owned placeholder effects are not in the catalog");
    AssetId spark;for(const auto& asset:assets.assets())if(asset.path=="placeholder/hit_spark.efkproj")spark=asset.id;check(spark!=AssetId{},"The hit spark has no identity");
    // Cook, package, load: the compiled effect and the texture it names, under the path the effect asks for.
    check(assets.cook("placeholder/hit_spark.efkproj").changed,"The hit spark did not cook");assets.package(spark,workspace/"registry.json");auto cooked=load_cooked_vfx(workspace/"registry.json",assets.cas_path(),spark);
    const auto needs=read_vfx_dependencies(cooked.effect);check(cooked.id==spark&&cooked.generation.size()==64&&needs.color==std::vector<std::string>{"Texture/glow.png"}&&cooked.effect.size()>1000,"The cooked hit spark is not the compiled effect with its texture reference");
    check(cooked.textures.size()==1&&cooked.textures.front().first=="Texture/glow.png"&&!cooked.textures.front().second.mips.empty()&&cooked.textures.front().second.mips.front().width==64&&cooked.textures.front().second.mips.front().height==64,"The cooked hit spark does not carry its texture");
    check(!assets.cook("placeholder/hit_spark.efkproj").changed,"A warm effect cook compiled again");
    // A changed texture is a changed effect; a missing one is refused and keeps the product.
    {const auto texture=mount/"placeholder/Texture/glow.png";const auto original=read(texture);std::filesystem::copy_file(mount/"samples/Texture/Burst01.png",texture,std::filesystem::copy_options::overwrite_existing);check(assets.cook("placeholder/hit_spark.efkproj").changed,"A changed texture did not cook the effect again");
        assets.package(spark,workspace/"changed.json");check(load_cooked_vfx(workspace/"changed.json",assets.cas_path(),spark).generation!=cooked.generation,"A changed texture left the product unchanged");
        std::filesystem::remove(texture);check(!refusal([&]{assets.cook("placeholder/hit_spark.efkproj");}).empty(),"An effect with a missing texture cooked");write(texture,original);check(assets.cook("placeholder/hit_spark.efkproj").changed,"A restored texture did not cook");}
    // A malformed source does not compile and is refused.
    {const auto source=mount/"placeholder/hit_spark.efkproj";const auto original=read(source);write(source,"<EffekseerProject><Root><Chil");check(!refusal([&]{assets.cook("placeholder/hit_spark.efkproj");}).empty(),"A malformed effect source cooked");write(source,original);assets.cook("placeholder/hit_spark.efkproj");}
    // Effekseer's packed project format, adopted like any import: a sample with three textures cooks.
    for(auto name:{"Laser01.efkefc","Simple_Distortion.efkefc"})std::filesystem::copy_file(samples/name,mount/"samples"/name);
    const auto laser=assets.adopt("samples/Laser01.efkefc");assets.adopt("samples/Simple_Distortion.efkefc");check(assets.cook("samples/Laser01.efkefc").changed,"A packed Effekseer project did not cook");assets.package(laser,workspace/"laser.json");check(load_cooked_vfx(workspace/"laser.json",assets.cas_path(),laser).textures.size()==3,"A packed project lost its textures");
    // What the renderer does not draw is refused by name.
    const auto why=refusal([&]{assets.cook("samples/Simple_Distortion.efkefc");});check(why.find("distortion")!=why.npos,"An effect that uses distortion was not refused by name");
    std::cout<<"Visual effect assets: catalog, compile through the Effekseer tool, package and load with textures, warm cook, changed and missing texture, malformed source, packed projects, distortion refused by name passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
