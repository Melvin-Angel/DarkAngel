#include <darkangel/tag_codegen.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
namespace darkangel {
namespace {
std::string symbol(const TagDefinition& tag){
 std::string result="Tag";bool separator=true;
 for(unsigned char c:tag.name){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')){if(separator)result+='_' ;result+=char(c);separator=false;}else separator=true;}
 return result+'_'+std::to_string(tag.id);
}
}
TagConstants generate_tag_constants(const TagAsset& source){
 auto dictionary=source.dictionary();if(source.id==AssetId{})throw std::invalid_argument("Tag export needs a persistent registry");
 auto fields=source.fields;std::sort(fields.begin(),fields.end(),[](const auto& a,const auto& b){return a.definition.id<b.definition.id;});std::set<AssetId> keys;for(const auto& field:fields)if(field.key==AssetId{}||!keys.insert(field.key).second)throw std::invalid_argument("Tag export persistent key identity");
 auto registry=source.id.text(),space="registry_"+registry;std::erase(space,'-');std::ostringstream cpp,luau;
 cpp<<"// Generated from a prepared .datags registry. Do not edit.\n#pragma once\n#include <darkangel/tags.hpp>\n#include <string_view>\nnamespace darkangel::generated_tags::"<<space<<" {\ninline constexpr std::string_view registry = \""<<registry<<"\";\ninline constexpr std::string_view generation = \""<<source.generation<<"\";\n";
 luau<<"--!strict\n-- Generated from a prepared .datags registry. Do not edit.\nexport type TagId = number\nlocal ids = {\n";
 for(const auto& field:fields){auto name=symbol(field.definition);cpp<<"inline constexpr TagId "<<name<<" = "<<field.definition.id<<"u;\ninline constexpr std::string_view Key_"<<field.definition.id<<" = \""<<field.key.text()<<"\";\n";luau<<"    "<<name<<" = "<<field.definition.id<<",\n";}
 cpp<<"inline bool matches(const TagDictionary& value) { return value.registry().text() == registry && value.generation() == generation; }\n}\n";
 luau<<"}\nlocal keys = {\n";for(const auto& field:fields)luau<<"    ["<<field.definition.id<<"] = \""<<field.key.text()<<"\",\n";
 luau<<"}\nreturn table.freeze({registry = \""<<registry<<"\", generation = \""<<source.generation<<"\", ids = table.freeze(ids), keys = table.freeze(keys), matches = function(registry: string, generation: string): boolean return registry == \""<<registry<<"\" and generation == \""<<source.generation<<"\" end})\n";
 return {cpp.str(),luau.str()};
}
std::filesystem::path publish_tag_constants(const TagAsset& source,const std::filesystem::path& destination){
 if(destination.empty())throw std::invalid_argument("Tag export destination missing");auto output=generate_tag_constants(source);auto root=std::filesystem::absolute(destination).lexically_normal()/source.id.text();std::filesystem::create_directories(root);auto final=root/sha256(output.cpp+output.luau);
 auto same=[](const std::filesystem::path& path,const std::string& expected){if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)!=expected.size())return false;std::ifstream stream(path,std::ios::binary);std::string bytes(expected.size(),'\0');stream.read(bytes.data(),bytes.size());return stream.good()&&bytes==expected;};
 if(std::filesystem::exists(final)){if(!same(final/"tags.hpp",output.cpp)||!same(final/"tags.luau",output.luau))throw std::runtime_error("Existing tag export conflicts with prepared generation");return final;}
 auto staging=root/(".tags-"+AssetId::random().text());if(!std::filesystem::create_directory(staging))throw std::runtime_error("Tag export staging identity conflict");
 try{auto write=[](const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file.write(bytes.data(),bytes.size());file.close();if(!file)throw std::runtime_error("Tag export write failed");};write(staging/"tags.hpp",output.cpp);write(staging/"tags.luau",output.luau);std::filesystem::rename(staging,final);}catch(...){std::filesystem::remove_all(staging);throw;}
 return final;
}
}
