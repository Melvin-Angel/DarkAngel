#include <darkangel/perception.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
constexpr double radians=3.141592653589793/180;
}
void validate_senses(const SensesDefinition& senses){
    require(std::isfinite(senses.eye_height)&&senses.eye_height>=0&&senses.eye_height<=4&&std::isfinite(senses.forget_seconds)&&senses.forget_seconds>=0&&senses.forget_seconds<=120,"NPC senses bounds: eye height 0 to 4 m, time to forget 0 to 120 s");
    require(senses.sensors.size()<=sensor_limit&&(!senses.everything||senses.sensors.empty()),"NPC senses: at most eight sensors, and none beside \"everything\"");
    for(const auto& sensor:senses.sensors){
        require(sensor.kind==SensorKind::Distance||sensor.kind==SensorKind::Vision||sensor.kind==SensorKind::LineOfSight,"NPC sensor kind");
        require(std::isfinite(sensor.range)&&sensor.range>0&&sensor.range<=200,"NPC sensor range bounds: above 0, up to 200 m");
        if(sensor.kind==SensorKind::Vision)require(std::isfinite(sensor.horizontal)&&sensor.horizontal>0&&sensor.horizontal<=180&&std::isfinite(sensor.vertical)&&sensor.vertical>0&&sensor.vertical<=90,"NPC vision cone bounds: horizontal half angle up to 180, vertical up to 90 degrees");
        else require(sensor.horizontal==0&&sensor.vertical==0,"Only a vision cone has angles");
    }
}
Perception::Perception(SensesDefinition senses):senses_(std::move(senses)){validate_senses(senses_);actors_.reserve(16);previous_.reserve(16);}
void Perception::sense(std::uint64_t tick,const MotorState& self,bool alive,std::span<const SensedSubject> subjects,const SightQuery& sight){
    previous_.swap(actors_);actors_.clear();if(!alive||(!senses_.everything&&senses_.sensors.empty()))return;
    const MotorVec eye{self.position.x,self.position.y+senses_.eye_height,self.position.z};const auto forget=std::uint64_t(std::llround(senses_.forget_seconds*60));
    for(const auto& subject:subjects){
        const MotorVec body{subject.motor.position.x,subject.motor.position.y+perception_body_height,subject.motor.position.z};
        // At most one sight query per subject, asked only when a sensor or the result needs it.
        int clear=-1;auto visible=[&]{if(clear<0)clear=sight&&sight(eye,body)?1:0;return clear==1;};
        const double fx=subject.motor.position.x-self.position.x,fy=subject.motor.position.y-self.position.y,fz=subject.motor.position.z-self.position.z;
        const double dx=body.x-eye.x,dy=body.y-eye.y,dz=body.z-eye.z,level=std::hypot(dx,dz),reach=std::sqrt(dx*dx+dy*dy+dz*dz);
        bool detected=senses_.everything;
        for(const auto& sensor:senses_.sensors){
            if(detected)break;
            if(sensor.kind==SensorKind::Distance)detected=fx*fx+fy*fy+fz*fz<=sensor.range*sensor.range;
            else if(reach<=sensor.range){
                if(sensor.kind==SensorKind::Vision){
                    const double aside=level>1e-6?std::abs(std::remainder(std::atan2(dx,dz)-self.yaw,6.283185307179586)):0,above=std::abs(std::atan2(dy,level));
                    if(aside>sensor.horizontal*radians+1e-9||above>sensor.vertical*radians+1e-9)continue;
                }
                detected=visible();
            }
        }
        const auto known=std::find_if(previous_.begin(),previous_.end(),[&](const auto& entry){return entry.id==subject.id;});
        if(detected)actors_.push_back({subject.id,subject.faction,subject.motor,subject.alive,true,visible(),known!=previous_.end()&&known->detected?known->since:tick,tick});
        else if(known!=previous_.end()&&tick-known->seen<=forget&&forget){auto lost=*known;lost.detected=false;lost.visible=false;actors_.push_back(lost);}
    }
}
}
