#include <darkangel/attributes.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
namespace darkangel {
std::size_t AttributeSet::index(AttributeId id)const{for(std::size_t i=0;i<definitions_.size();++i)if(definitions_[i].id==id)return i;throw std::invalid_argument("Unknown attribute");}
AttributeSet::AttributeSet(std::vector<AttributeDefinition> definitions):definitions_(std::move(definitions)){
 if(definitions_.empty()||definitions_.size()>64)throw std::invalid_argument("Attribute schema size");
 for(std::size_t i=0;i<definitions_.size();++i){const auto& d=definitions_[i];
  if(!d.id||d.name.empty()||d.name.size()>96||!std::isfinite(d.base)||!std::isfinite(d.minimum)||!std::isfinite(d.maximum)||d.minimum>d.maximum||d.base<d.minimum||d.base>d.maximum||static_cast<unsigned>(d.kind)>1||static_cast<unsigned>(d.visibility)>2)throw std::invalid_argument("Invalid attribute schema");
  for(std::size_t j=0;j<i;++j)if(definitions_[j].id==d.id||definitions_[j].name==d.name)throw std::invalid_argument("Duplicate attribute");
  if(d.maximum_attribute&&(d.kind!=AttributeKind::Resource||definitions_[index(d.maximum_attribute)].kind!=AttributeKind::Statistic))throw std::invalid_argument("Resource maximum must reference statistic");
  values_.push_back(d.base);
 }
 recompute();
}
double AttributeSet::value(AttributeId id)const{return values_[index(id)];}
void AttributeSet::recompute(){
 for(std::size_t i=0;i<definitions_.size();++i){const auto& d=definitions_[i];if(d.kind==AttributeKind::Resource)continue;
  double flat=0,post=0;std::map<std::string,double> channels;const AttributeModifier* override=nullptr;
  for(const auto& m:modifiers_)if(m.attribute==d.id){switch(m.kind){
   case AttributeModifierKind::Flat:flat+=m.magnitude;break;
   case AttributeModifierKind::ChannelBonus:channels[m.channel]+=m.magnitude;break;
   case AttributeModifierKind::Post:post+=m.magnitude;break;
   case AttributeModifierKind::Override:if(!override||m.priority>override->priority||(m.priority==override->priority&&m.sequence>override->sequence))override=&m;break;
  }}
  double result=d.base+flat;for(const auto& [channel,bonus]:channels)result*=std::max(0.0,1+bonus);result+=post;if(override)result=override->magnitude;
  if(!std::isfinite(result))throw std::invalid_argument("Attribute calculation overflow");values_[i]=std::clamp(result,d.minimum,d.maximum);
 }
 for(std::size_t i=0;i<definitions_.size();++i){const auto& d=definitions_[i];if(d.kind!=AttributeKind::Resource)continue;
  double maximum=d.maximum_attribute?std::min(d.maximum,value(d.maximum_attribute)):d.maximum;
  if(maximum<d.minimum)throw std::invalid_argument("Resource maximum below minimum");values_[i]=std::clamp(values_[i],d.minimum,maximum);
 }
}
void AttributeSet::add(std::span<const AttributeModifier> additions){
 if(additions.size()>256||modifiers_.size()+additions.size()>256)throw std::invalid_argument("Attribute modifier work limit");auto candidate=*this;
 for(const auto& m:additions){
  if(!m.owner||!m.sequence||!std::isfinite(m.magnitude)||static_cast<unsigned>(m.kind)>3||definitions_[index(m.attribute)].kind!=AttributeKind::Statistic||m.channel.size()>64||(m.kind==AttributeModifierKind::ChannelBonus&&m.channel.empty()))throw std::invalid_argument("Invalid statistic modifier");
  for(const auto& old:candidate.modifiers_)if(old.sequence==m.sequence)throw std::invalid_argument("Duplicate modifier sequence");candidate.modifiers_.push_back(m);
 }
 std::sort(candidate.modifiers_.begin(),candidate.modifiers_.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});candidate.recompute();*this=std::move(candidate);
}
void AttributeSet::remove_owner(std::uint64_t owner){auto candidate=*this;std::erase_if(candidate.modifiers_,[&](const auto& m){return m.owner==owner;});candidate.recompute();*this=std::move(candidate);}
void AttributeSet::replace_owned(std::span<const std::uint64_t> owners,std::span<const AttributeModifier> additions){
 if(owners.size()>128)throw std::invalid_argument("Attribute owned replacement bound");auto candidate=*this;std::erase_if(candidate.modifiers_,[&](const auto& m){return std::find(owners.begin(),owners.end(),m.owner)!=owners.end();});candidate.add(additions);*this=std::move(candidate);
}
void AttributeSet::transact(std::span<const ResourceDelta> deltas,bool reject_underflow){
 if(deltas.size()>64)throw std::invalid_argument("Resource transaction work limit");auto candidate=*this;std::vector<double> totals(definitions_.size());
 for(const auto& change:deltas){auto i=index(change.attribute);if(definitions_[i].kind!=AttributeKind::Resource||!std::isfinite(change.delta))throw std::invalid_argument("Invalid resource delta");totals[i]+=change.delta;if(!std::isfinite(totals[i]))throw std::invalid_argument("Resource delta overflow");}
 for(std::size_t i=0;i<totals.size();++i){auto next=values_[i]+totals[i];if(!std::isfinite(next)||(reject_underflow&&next<definitions_[i].minimum))throw std::invalid_argument("Insufficient resource or overflow");candidate.values_[i]=next;}
 candidate.recompute();*this=std::move(candidate);
}
}
