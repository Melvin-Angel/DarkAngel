#include "assets_internal.hpp"
#include <darkangel/sound_assets.hpp>
#include <darkangel/hash.hpp>
#include <cstring>
#include <set>
namespace darkangel {
namespace {
using assets_detail::require;
std::uint32_t u32(std::string_view bytes,std::size_t at){require(at+4<=bytes.size(),"Sound file ends early");std::uint32_t value;std::memcpy(&value,bytes.data()+at,4);return value;}
std::uint16_t u16(std::string_view bytes,std::size_t at){require(at+2<=bytes.size(),"Sound file ends early");std::uint16_t value;std::memcpy(&value,bytes.data()+at,2);return value;}
}
SoundFormat read_sound_format(std::string_view bytes){
    require(bytes.size()>=44&&bytes.size()<=16*1024*1024&&bytes.substr(0,4)=="RIFF"&&bytes.substr(8,4)=="WAVE","A sound source is a RIFF WAVE file of at most 16 MB");SoundFormat format;bool described=false,data=false;
    for(std::size_t at=12;at+8<=bytes.size();){const auto kind=bytes.substr(at,4);const std::size_t size=u32(bytes,at+4);require(size<=bytes.size()-at-8,"A sound chunk runs past the end of the file");
        if(kind=="fmt "){require(size>=16,"Sound format chunk");const auto encoding=u16(bytes,at+8);format.channels=u16(bytes,at+10);format.rate=u32(bytes,at+12);format.bits=u16(bytes,at+22);format.floating=encoding==3;
            require((encoding==1&&(format.bits==16||format.bits==24))||(encoding==3&&format.bits==32),"A sound source is 16 or 24 bit PCM, or 32 bit float");require((format.channels==1||format.channels==2)&&format.rate>=8000&&format.rate<=96000,"A sound source is mono or stereo at 8 to 96 kHz");described=true;}
        else if(kind=="data"){require(described,"A sound's data comes after its format");format.frames=size/(std::size_t(format.channels)*format.bits/8);require(format.frames>0,"A sound has no samples");data=true;}
        at+=8+size+(size&1);}
    require(described&&data,"A sound source has a format and data");return format;
}
namespace assets_detail {
// A sound is a leaf source: a validated WAV file, stored as it is.
Import import_sound(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,16*1024*1024);read_sound_format(bytes);Import result;result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    if(!inspect){require(ids.size()==1,"Sound source identity");result.products.push_back({ids.at("$source"),"sound","wav",bytes,{}});}return result;
}
}
CookedSound load_cooked_sound(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    using namespace assets_detail;const auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Sound registry schema/limit");const Json* selected{};std::set<AssetId> unique;
    for(const auto& record:manifest.at("assets")){const auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate sound registry UUID");if(asset==id)selected=&record;}
    require(selected&&selected->at("kind")=="sound"&&selected->at("extension")=="wav","Sound product type mismatch");const auto hash=selected->at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Sound product digest");
    CookedSound cooked;cooked.id=id;cooked.generation=hash;cooked.wave=read(cas/(hash+".wav"),16*1024*1024);require(sha256(cooked.wave)==hash,"Sound product hash mismatch");cooked.format=read_sound_format(cooked.wave);return cooked;
}
}
