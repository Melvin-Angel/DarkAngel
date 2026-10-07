#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <Windows.h>
#include <bcrypt.h>
#include <stdexcept>
namespace {void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}}
namespace darkangel {
StableId asset_key(AssetId asset){auto text=asset.text();text.erase(23,1);text.erase(18,1);text.erase(13,1);text.erase(8,1);return StableId::parse(text);}
AssetId asset_id(StableId key){auto text=key.text();text.insert(20,"-");text.insert(16,"-");text.insert(12,"-");text.insert(8,"-");return AssetId::parse(text);}
AssetId AssetId::random(){AssetId id;if(BCryptGenRandom(nullptr,id.bytes.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("UUID allocation failed");id.bytes[6]=(id.bytes[6]&15)|0x40;id.bytes[8]=(id.bytes[8]&63)|0x80;return id;}
std::string AssetId::text() const{constexpr char h[]="0123456789abcdef";std::string s;for(std::size_t i=0;i<16;++i){if(i==4 || i==6 || i==8 || i==10)s+='-';s+=h[bytes[i]>>4];s+=h[bytes[i]&15];}return s;}
AssetId AssetId::parse(std::string_view s){require(s.size()==36 && s[8]=='-' && s[13]=='-' && s[18]=='-' && s[23]=='-',"Invalid canonical AssetID");AssetId result;std::size_t at{};auto digit=[](char c){auto i=std::string_view("0123456789abcdef").find(c);require(i!=std::string_view::npos,"AssetID must be lowercase hex");return static_cast<unsigned>(i);};for(std::size_t i=0;i<16;++i){if(at==8 || at==13 || at==18 || at==23)++at;result.bytes[i]=static_cast<std::uint8_t>((digit(s[at])<<4)|digit(s[at+1]));at+=2;}require((result.bytes[6]>>4)==4 && (result.bytes[8]&0xc0)==0x80,"AssetID must be UUIDv4");return result;}
}
