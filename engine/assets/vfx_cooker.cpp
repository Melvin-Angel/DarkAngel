#include "assets_internal.hpp"
#include <darkangel/vfx_assets.hpp>
#include <darkangel/hash.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace darkangel {
namespace {
std::filesystem::path configured_tool;
using assets_detail::require;
// Little-endian reader over a compiled effect or a cooked bundle; every read is bounded.
struct Reader {
    std::string_view bytes;std::size_t at{};
    std::uint32_t u32(){require(at+4<=bytes.size(),"Effect data ends early");std::uint32_t value;std::memcpy(&value,bytes.data()+at,4);at+=4;return value;}
    std::string_view take(std::size_t count){require(count<=bytes.size()-at,"Effect data ends early");auto part=bytes.substr(at,count);at+=count;return part;}
};
void put(std::string& out,std::uint32_t value){out.append(reinterpret_cast<const char*>(&value),4);}
// Effekseer stores paths as UTF-16 with a terminator; resource paths are kept as UTF-8 here.
std::string utf8(std::string_view utf16){
    std::string out;for(std::size_t index=0;index+1<utf16.size();index+=2){const unsigned unit=static_cast<unsigned char>(utf16[index])|(static_cast<unsigned char>(utf16[index+1])<<8);if(!unit)break;require(unit<0xd800||unit>0xdfff,"Effect resource path uses characters outside the basic plane");
        if(unit<0x80)out.push_back(char(unit));else if(unit<0x800){out.push_back(char(0xc0|(unit>>6)));out.push_back(char(0x80|(unit&0x3f)));}else{out.push_back(char(0xe0|(unit>>12)));out.push_back(char(0x80|((unit>>6)&0x3f)));out.push_back(char(0x80|(unit&0x3f)));}}
    return out;
}
std::vector<std::string> paths(Reader& reader){const auto count=reader.u32();require(count<=1024,"Effect resource count bound");std::vector<std::string> out;for(std::uint32_t index=0;index<count;++index){const auto length=reader.u32();require(length<=1024,"Effect resource path bound");out.push_back(utf8(reader.take(std::size_t(length)*2)));}return out;}
// Texture paths a source names, read without the tool so that inspecting a source and cooking it see the same inputs:
// an XML project lists them in elements whose name ends in "Texture"; a packed project starts with an INFO chunk
// that lists them the way the compiled effect does.
std::set<std::string> source_textures(const std::filesystem::path& source,std::string_view bytes){
    std::set<std::string> found;
    if(source.extension()==".efkproj"){static const std::regex element("<([A-Za-z0-9_]*Texture)>([^<]+)</\\1>");const std::string text(bytes);for(std::sregex_iterator at(text.begin(),text.end(),element),end;at!=end;++at)found.insert((*at)[2].str());return found;}
    Reader reader{bytes};require(reader.take(4)=="EFKE","Not an Effekseer project file");reader.u32();require(reader.take(4)=="INFO","Effekseer project has no resource list");const auto size=reader.u32();Reader info{reader.take(size)};info.u32();
    for(const auto& path:paths(info))found.insert(path);return found;
}
std::string run_tool(const std::filesystem::path& source){
#ifdef _WIN32
    const auto tool=effekseer_tool();require(std::filesystem::is_regular_file(tool),"The Effekseer tool is missing; an effect source cannot be compiled without it");
    const auto output=std::filesystem::temp_directory_path()/("darkangel-"+AssetId::random().text()+".efk");struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove(path,ignored);}} cleanup{output};
    for(const auto& path:{source,output,tool})require(path.wstring().find(L'"')==std::wstring::npos,"Unsupported character in an effect path");
    std::wstring command=L"\""+tool.wstring()+L"\" -cui -in \""+source.wstring()+L"\" -e \""+output.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,tool.parent_path().wstring().c_str(),&startup,&process)!=0,"The Effekseer tool could not be started");
    const auto waited=WaitForSingleObject(process.hProcess,120000);DWORD code=1;if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);else TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    require(waited==WAIT_OBJECT_0,"The Effekseer tool did not finish compiling the effect in two minutes");require(code==0&&std::filesystem::is_regular_file(output),"The Effekseer tool did not compile the effect; open it in Effekseer to see what is wrong");
    return assets_detail::read(output,16*1024*1024);
#else
    (void)source;throw std::runtime_error("Effect sources are compiled by the Effekseer tool, which this platform build does not run");
#endif
}
}
void set_effekseer_tool(std::filesystem::path path){configured_tool=std::move(path);}
std::filesystem::path effekseer_tool(){
    if(!configured_tool.empty())return configured_tool;
#ifdef _WIN32
    char* value{};std::size_t size{};if(_dupenv_s(&value,&size,"DAE_EFFEKSEER_TOOL")==0&&value){std::filesystem::path path(value);free(value);if(!path.empty())return path;}
#endif
#ifdef DAE_EFFEKSEER_TOOL
    return std::filesystem::path(DAE_EFFEKSEER_TOOL);
#else
    return {};
#endif
}
VfxDependencies read_vfx_dependencies(std::string_view compiled){
    Reader reader{compiled};require(reader.take(4)=="SKFE","Not a compiled Effekseer effect");VfxDependencies found;found.version=int(reader.u32());require(found.version>=15&&found.version<100000,"Unsupported compiled Effekseer effect version");
    found.color=paths(reader);found.normal=paths(reader);found.distortion=paths(reader);found.sound=paths(reader);found.model=paths(reader);found.material=paths(reader);return found;
}
namespace assets_detail {
Import import_vfx(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,4*1024*1024);Import result;result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    const auto named=source_textures(source,bytes);require(named.size()<=16,"An effect uses at most sixteen textures");std::map<std::string,std::string> images;
    for(const auto& path:named){const auto file=within(root,(source.parent_path()/std::filesystem::path(path)).lexically_relative(root));require(std::filesystem::is_regular_file(file),"An effect names a texture file that is missing next to it");auto image=read(file,16*1024*1024);result.inputs[file.lexically_relative(root).generic_string()]=sha256(image);images.emplace(path,std::move(image));}
    if(inspect)return result;require(ids.size()==1,"Effect source identity");
    const auto compiled=run_tool(source);const auto needs=read_vfx_dependencies(compiled);
    // What the renderer does not draw is refused here, by name, instead of being drawn wrongly.
    require(needs.normal.empty(),"This effect uses a normal map (lit particles), which the renderer does not draw yet");require(needs.distortion.empty(),"This effect uses distortion, which the renderer does not draw yet");
    require(needs.model.empty(),"This effect uses a model, which the renderer does not draw yet");require(needs.material.empty(),"This effect uses a custom material, which the renderer does not draw yet");require(needs.sound.empty(),"This effect plays a sound from inside Effekseer; sounds belong to cues");
    std::string bundle("DAVFX1\0\0",8);put(bundle,std::uint32_t(compiled.size()));bundle+=compiled;put(bundle,std::uint32_t(needs.color.size()));
    for(const auto& path:needs.color){const auto image=images.find(path);require(image!=images.end(),"The compiled effect names a texture its source does not list");const auto cooked=cook_color_texture(image->second);put(bundle,std::uint32_t(path.size()));bundle+=path;put(bundle,std::uint32_t(cooked.size()));bundle+=cooked;}
    result.products.push_back({ids.at("$source"),"vfx","vfx",std::move(bundle),{}});return result;
}
}
CookedVfx load_cooked_vfx(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    using namespace assets_detail;const auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Effect registry schema/limit");const Json* selected{};std::set<AssetId> unique;
    for(const auto& record:manifest.at("assets")){const auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate effect registry UUID");if(asset==id)selected=&record;}
    require(selected&&selected->at("kind")=="vfx"&&selected->at("extension")=="vfx","Visual effect product type mismatch");const auto hash=selected->at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Visual effect product digest");
    const auto bytes=read(cas/(hash+".vfx"),64*1024*1024);require(sha256(bytes)==hash,"Visual effect product hash mismatch");Reader reader{bytes};require(reader.take(8)==std::string_view("DAVFX1\0\0",8),"Visual effect product format");
    CookedVfx cooked;cooked.id=id;cooked.generation=hash;cooked.effect=std::string(reader.take(reader.u32()));read_vfx_dependencies(cooked.effect);const auto count=reader.u32();require(count<=16,"Visual effect texture bound");
    for(std::uint32_t index=0;index<count;++index){std::string path(reader.take(reader.u32()));auto texture=decode_texture(reader.take(reader.u32()));cooked.textures.emplace_back(std::move(path),std::move(texture));}
    require(reader.at==bytes.size(),"Visual effect product has trailing data");return cooked;
}
}
