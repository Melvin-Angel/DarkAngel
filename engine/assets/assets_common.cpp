#include "assets_internal.hpp"
#include <darkangel/hash.hpp>
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <list>
#include <memory>
#include <tuple>
#include <mutex>
#include <set>
#include <stdexcept>
namespace darkangel::assets_detail {
void require(bool condition,const char* error){if(!condition)throw std::runtime_error(error);}
std::string read(const std::filesystem::path& path,std::size_t limit){std::ifstream in(path,std::ios::binary|std::ios::ate);require(in.good(),"Cannot open asset input");auto size=in.tellg();require(size>=0 && static_cast<std::uint64_t>(size)<=limit,"Asset input size limit");std::string bytes(static_cast<std::size_t>(size),'\0');in.seekg(0);in.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));require(in.good(),"Asset read failed");return bytes;}
std::shared_ptr<const Json> registry_json(const std::filesystem::path& path){
    using Key=std::tuple<std::string,std::uintmax_t,std::int64_t>;static std::mutex lock;static std::list<std::pair<Key,std::shared_ptr<const Json>>> recent;
    std::error_code failed;const auto absolute=std::filesystem::absolute(path,failed).generic_string();const auto size=std::filesystem::file_size(path,failed);require(!failed,"Cannot open asset input");
    const Key key{absolute,size,std::filesystem::last_write_time(path,failed).time_since_epoch().count()};require(!failed,"Cannot open asset input");
    {std::scoped_lock held(lock);for(auto at=recent.begin();at!=recent.end();++at)if(at->first==key){recent.splice(recent.begin(),recent,at);return recent.front().second;}}
    const auto bytes=read(path,64*1024*1024);std::size_t events{};
    auto parsed=std::make_shared<const Json>(Json::parse(bytes,[&](int depth,Json::parse_event_t,Json&){require(depth<=32&&++events<=runtime_package_product_limit*24+64,"Runtime registry work limit");return true;}));
    require(parsed->at("schema")==1&&parsed->at("assets").is_array()&&parsed->at("assets").size()<=runtime_package_product_limit,"Runtime registry schema/product limit");
    std::scoped_lock held(lock);recent.emplace_front(key,parsed);if(recent.size()>8)recent.pop_back();return parsed;
}
Json json(std::string_view bytes,std::size_t limit){require(bytes.size()<=limit,"Asset JSON limit");std::vector<std::set<std::string>> keys;std::size_t events{};return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=32 && ++events<=65536,"Asset JSON depth/work limit");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate asset JSON key");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
std::filesystem::path within(const std::filesystem::path& root,const std::filesystem::path& relative){require(!relative.empty() && !relative.is_absolute() && !relative.has_root_name(),"Asset paths must be relative");auto canonical=std::filesystem::weakly_canonical(root/relative);auto base=std::filesystem::weakly_canonical(root);auto rel=canonical.lexically_relative(base);require(!rel.empty() && *rel.begin()!="..","Asset path escapes source mount");return canonical;}
}
