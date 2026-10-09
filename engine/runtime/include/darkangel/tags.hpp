#pragma once
#include <darkangel/attributes.hpp>
#include <memory>
#include <span>
#include <vector>
namespace darkangel {
using TagId=std::uint32_t;
struct TagDefinition {TagId id{},parent{};std::string name;AttributeVisibility visibility{AttributeVisibility::Owner};};
struct TagRequirement {std::vector<TagId> all,any,none;};
class TagDictionary {
public:
 explicit TagDictionary(std::vector<TagDefinition>);
 bool contains(TagId)const;
 bool descends(TagId child,TagId ancestor)const;
 void validate(const TagRequirement&)const;
 std::span<const TagDefinition> definitions()const{return definitions_;}
private:std::vector<TagDefinition> definitions_;
};
// Each contributor owns its exact token. Removing one cannot remove another.
class OwnedTags {
public:
 explicit OwnedTags(std::shared_ptr<const TagDictionary>);
 void add(std::uint64_t token,std::span<const TagId>);
 void remove(std::uint64_t token);
 bool has(TagId,std::uint64_t excluded_token=0)const;
 bool matches(const TagRequirement&,std::uint64_t excluded_token=0)const;
 std::vector<TagId> values(AttributeVisibility audience=AttributeVisibility::Server)const;
 const TagDictionary& dictionary()const{return *dictionary_;}
private:
 struct Contribution {std::uint64_t token;std::vector<TagId> tags;};
 std::shared_ptr<const TagDictionary> dictionary_;std::vector<Contribution> contributions_;
};
}
