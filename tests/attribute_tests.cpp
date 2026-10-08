#include <darkangel/attributes.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace darkangel;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
int main(){try{
 AttributeSet stats({{1,"MaxHealth",AttributeKind::Statistic,100,0,10000,0},{2,"Health",AttributeKind::Resource,100,0,10000,1},{3,"Stamina",AttributeKind::Resource,50,0,50,0},{4,"Essence",AttributeKind::Resource,30,0,30,0},{5,"Poise",AttributeKind::Statistic,10,0,1000,0},{6,"ElementalDefence",AttributeKind::Statistic,0,0,1000,0},{7,"Block",AttributeKind::Statistic,0,0,1,0}});
 AttributeModifier modifiers[]={{1,1,1,AttributeModifierKind::ChannelBonus,.2,"equipment"},{2,2,1,AttributeModifierKind::ChannelBonus,.2,"equipment"}};
 stats.add(modifiers);check(std::abs(stats.value(1)-140)<1e-9,"Same channel adds bonuses");stats.remove_owner(1);check(std::abs(stats.value(1)-120)<1e-9,"Removing one source preserves other");stats.remove_owner(2);check(stats.value(1)==100,"Recompute base after removal");
 modifiers[1].channel="spell";stats.add(modifiers);check(std::abs(stats.value(1)-144)<1e-9,"Different channels multiply");
 ResourceDelta costs[]={{3,-40},{4,-20}};stats.transact(costs,true);check(stats.value(3)==10&&stats.value(4)==10,"Atomic multi-resource cost");
 ResourceDelta overspend[]={{3,-11},{4,-1}};rejects([&]{stats.transact(overspend,true);});check(stats.value(3)==10&&stats.value(4)==10,"Failed cost changes nothing");
 ResourceDelta duplicates[]={{3,-6},{3,-6}};rejects([&]{stats.transact(duplicates,true);});check(stats.value(3)==10,"Aggregate same-resource costs before validation");
 ResourceDelta damage{2,-200};stats.transact({&damage,1},false);check(stats.value(2)==0,"Damage clamps stored health");ResourceDelta heal{2,500};stats.transact({&heal,1},false);check(std::abs(stats.value(2)-144)<1e-9,"Healing clamps to derived maximum");
 stats.remove_owner(1);stats.remove_owner(2);check(stats.value(2)==100,"Maximum change clamps resource before publication");
 AttributeModifier overrides[]={{3,3,1,AttributeModifierKind::Override,80,"",2},{4,4,1,AttributeModifierKind::Override,60,"",2}};stats.add(overrides);check(stats.value(1)==60&&stats.value(2)==60,"Override tie uses stable latest sequence");stats.remove_owner(4);check(stats.value(1)==80&&stats.value(2)==60,"Removing modifier never restores spent health");
 AttributeModifier invalid{9,9,2,AttributeModifierKind::Flat,10};rejects([&]{stats.add({&invalid,1});});check(stats.value(2)==60,"Resource cannot receive temporary modifier");
 ResourceDelta nan{3,std::numeric_limits<double>::quiet_NaN()};rejects([&]{stats.transact({&nan,1},false);});check(stats.value(3)==10,"Nonfinite transaction preserved");
 AttributeModifier formula[]={{5,5,5,AttributeModifierKind::Flat,5},{6,6,5,AttributeModifierKind::ChannelBonus,.5,"buff"},{7,7,5,AttributeModifierKind::Post,2}};stats.add(formula);check(stats.value(5)==24.5,"Flat then channel then post order");
 AttributeModifier duplicate{8,5,5,AttributeModifierKind::Flat,100};rejects([&]{stats.add({&duplicate,1});});check(stats.value(5)==24.5,"Duplicate application sequence preserved");
 AttributeModifier overflow[]={{8,8,5,AttributeModifierKind::Flat,std::numeric_limits<double>::max()},{9,9,5,AttributeModifierKind::Flat,std::numeric_limits<double>::max()}};rejects([&]{stats.add(overflow);});check(stats.value(5)==24.5,"Calculation overflow is atomic");
 stats.remove_owner(5);stats.remove_owner(5);check(stats.value(5)==17,"Repeated owned cleanup is idempotent");
 std::cout<<"Attribute formulas, owned modifiers, atomic costs and resource clamp checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
