#include "owner_ui_binding.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace darkangel {namespace {void require(bool b,const char* e){if(!b)throw std::runtime_error(e);}double percent(double v,double m){return m>0&&std::isfinite(v)?std::clamp(v/m,0.,1.)*100:0;}Rml::String amount(double v,double m){return std::to_string(std::lround(std::ceil(std::max(v,0.))))+" / "+std::to_string(std::lround(std::max(m,0.)));}}
OwnerUiBinding::OwnerUiBinding(Rml::Context& context){
        auto model=context.CreateDataModel("owner");require(bool(model),"HUD data model creation failed");
        bool bound=model.Bind("downed",&this->downed)&&model.Bind("wiped",&this->wiped)&&model.Bind("reviving",&this->reviving)&&model.Bind("revive_text",&this->revive_text)&&model.Bind("revive_percent",&this->revive_percent)&&model.Bind("health_percent",&this->health_percent)&&model.Bind("stamina_percent",&this->stamina_percent)&&model.Bind("target_percent",&this->target_percent)&&model.Bind("health_text",&this->health_text)&&model.Bind("stamina_text",&this->stamina_text)&&model.Bind("target_text",&this->target_text)&&model.Bind("dead",&this->dead)&&model.Bind("staggered",&this->staggered)&&model.Bind("has_target",&this->has_target)&&model.Bind("mask",&this->mask)&&model.Bind("has_mask",&this->has_mask)&&model.Bind("target_locked",&this->target_locked)&&model.Bind("target_name",&this->target_name)&&model.Bind("crosshair",&this->crosshair)&&model.Bind("prompt",&this->prompt)&&model.Bind("has_prompt",&this->has_prompt)&&model.Bind("loading",&this->loading)&&model.Bind("loading_text",&this->loading_text)&&model.Bind("loading_percent",&this->loading_percent);
        require(bound,"HUD view model binding failed");this->handle=model.GetModelHandle();this->assign({});
}
void OwnerUiBinding::update(const OwnerUiModel& value){
    for(double number:{value.health,value.maximum_health,value.stamina,value.maximum_stamina,value.target_health,value.target_maximum_health})require(std::isfinite(number)&&number>=0&&number<=1e9,"HUD view model bounds");require(value.mask.size()<=64&&value.target_name.size()<=64&&value.prompt.size()<=64&&value.loading_text.size()<=96&&std::isfinite(value.loading_progress)&&value.revive_text.size()<=64&&std::isfinite(value.revive_progress),"HUD name bound");
    if(!(value==this->model)){this->assign(value);this->handle.DirtyAllVariables();}
}
}

namespace darkangel {
    void OwnerUiBinding::assign(const OwnerUiModel& value){
        model=value;downed=value.downed;wiped=value.wiped;revive_text=value.revive_text;reviving=!value.revive_text.empty();revive_percent=float(std::clamp(value.revive_progress,0.,1.)*100);health_percent=float(percent(value.health,value.maximum_health));stamina_percent=float(percent(value.stamina,value.maximum_stamina));target_percent=float(percent(value.target_health,value.target_maximum_health));
        health_text=amount(value.health,value.maximum_health);stamina_text=amount(value.stamina,value.maximum_stamina);target_text=amount(value.target_health,value.target_maximum_health);dead=value.health<=0;staggered=value.staggered&&!dead;has_target=value.has_target;mask=value.mask;has_mask=!value.mask.empty();target_locked=value.target_locked&&value.has_target&&!dead;target_name=value.target_name.empty()?"Target":value.target_name;crosshair=value.crosshair&&!dead;prompt=value.prompt;has_prompt=!value.prompt.empty()&&!dead&&!value.loading;loading=value.loading;loading_text=value.loading_text;loading_percent=float(std::clamp(value.loading_progress,0.,1.)*100);
    }
}
