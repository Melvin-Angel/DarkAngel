#include <darkangel/tags.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {void checked(bool b,const char* m){if(!b)throw std::invalid_argument(m);}}
TagDictionary::TagDictionary(std::vector<TagDefinition> source):definitions_(std::move(source)){
 checked(definitions_.size()<=128,"Tag dictionary bound");std::set<TagId> ids;std::set<std::string> names;
 for(const auto& d:definitions_)checked(d.id&&!d.name.empty()&&d.name.size()<=96&&static_cast<unsigned>(d.visibility)<=2&&ids.insert(d.id).second&&names.insert(d.name).second,"Tag dictionary identity");
 for(const auto& d:definitions_){auto id=d.id;unsigned depth=0;while(id){checked(++depth<=16,"Tag hierarchy cycle/depth");auto it=std::find_if(definitions_.begin(),definitions_.end(),[&](const auto& x){return x.id==id;});checked(it!=definitions_.end(),"Tag parent missing");id=it->parent;}}
 std::sort(definitions_.begin(),definitions_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
}
bool TagDictionary::contains(TagId id)const{return std::any_of(definitions_.begin(),definitions_.end(),[&](const auto& d){return d.id==id;});}
bool TagDictionary::descends(TagId child,TagId ancestor)const{
 checked(contains(child)&&contains(ancestor),"Unknown tag query");while(child){if(child==ancestor)return true;child=std::find_if(definitions_.begin(),definitions_.end(),[&](const auto& d){return d.id==child;})->parent;}return false;
}
void TagDictionary::validate(const TagRequirement& r)const{for(const auto* list:{&r.all,&r.any,&r.none}){checked(list->size()<=16,"Tag requirement bound");std::set<TagId> unique;for(auto id:*list)checked(contains(id)&&unique.insert(id).second,"Tag requirement identity");}}
OwnedTags::OwnedTags(std::shared_ptr<const TagDictionary> dictionary){checked(bool(dictionary),"Missing tag dictionary");dictionary_=std::make_shared<const TagDictionary>(*dictionary);}
void OwnedTags::add(std::uint64_t token,std::span<const TagId> tags){
 checked(token&&tags.size()<=16&&contributions_.size()<128&&!std::any_of(contributions_.begin(),contributions_.end(),[&](const auto& c){return c.token==token;}),"Tag token/bound");std::set<TagId> unique;
 for(auto id:tags)checked(dictionary_->contains(id)&&unique.insert(id).second,"Tag contribution identity");auto candidate=contributions_;candidate.push_back({token,{tags.begin(),tags.end()}});contributions_=std::move(candidate);
}
void OwnedTags::remove(std::uint64_t token){std::erase_if(contributions_,[&](const auto& c){return c.token==token;});}
bool OwnedTags::has(TagId id,std::uint64_t excluded)const{checked(dictionary_->contains(id),"Unknown owned tag");for(const auto& c:contributions_)if(c.token!=excluded)for(auto tag:c.tags)if(dictionary_->descends(tag,id))return true;return false;}
bool OwnedTags::matches(const TagRequirement& r,std::uint64_t excluded)const{
 dictionary_->validate(r);return std::all_of(r.all.begin(),r.all.end(),[&](auto id){return has(id,excluded);})&&(r.any.empty()||std::any_of(r.any.begin(),r.any.end(),[&](auto id){return has(id,excluded);}))&&std::none_of(r.none.begin(),r.none.end(),[&](auto id){return has(id,excluded);});
}
std::vector<TagId> OwnedTags::values(AttributeVisibility audience)const{
 checked(static_cast<unsigned>(audience)<=2,"Tag audience");std::set<TagId> result;for(const auto& c:contributions_)for(auto id:c.tags){auto d=std::find_if(dictionary_->definitions().begin(),dictionary_->definitions().end(),[&](const auto& x){return x.id==id;});if(audience==AttributeVisibility::Server||d->visibility==AttributeVisibility::Public||(audience==AttributeVisibility::Owner&&d->visibility==AttributeVisibility::Owner))result.insert(id);}return {result.begin(),result.end()};
}
}
